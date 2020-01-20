#include "system.h"
#include "FreeRTOS.h"
#include "task.h"
#include "commander.h"
#include "relative_localization.h"
#include "num.h"
#include "param.h"
#include "debug.h"
#include <stdlib.h> // random
#include "lpsTwrTag.h" // UWBNum

static bool isInit;
static bool onGround = true;
static uint8_t keepFlying = 0;
static setpoint_t setpoint;
static float_t relaVarInCtrl[NumUWB][STATE_DIM_rl];

static float relaCtrl_p = 2.0f;
static float relaCtrl_i = 0.0001f;
static float relaCtrl_d = 0.01f;

static void setHoverSetpoint(setpoint_t *setpoint, float vx, float vy, float z, float yawrate)
{
  setpoint->mode.z = modeAbs;
  setpoint->position.z = z;
  setpoint->mode.yaw = modeVelocity;
  setpoint->attitudeRate.yaw = yawrate;
  setpoint->mode.x = modeVelocity;
  setpoint->mode.y = modeVelocity;
  setpoint->velocity.x = vx;
  setpoint->velocity.y = vy;
  setpoint->velocity_body = true;
  commanderSetSetpoint(setpoint, 3);
}

static void flyRandomIn1meter(void){
  float_t randomYaw = (rand() / (float)RAND_MAX) * 6.28f; // 0-2pi rad
  float_t randomVel = (rand() / (float)RAND_MAX); // 0-1 m/s
  float_t vxBody = randomVel * cosf(randomYaw);
  float_t vyBody = randomVel * sinf(randomYaw);
  for (int i=1; i<100; i++) {
    setHoverSetpoint(&setpoint, vxBody, vyBody, 0.5, 0);
    vTaskDelay(M2T(10));
  }
  for (int i=1; i<100; i++) {
    setHoverSetpoint(&setpoint, -vxBody, -vyBody, 0.5, 0);
    vTaskDelay(M2T(10));
  }
}

static float_t targetX;
static float_t targetY;
static float PreErr_x = 0;
static float PreErr_y = 0;
static float IntErr_x = 0;
static float IntErr_y = 0;
static uint32_t PreTime;
static void formation0asCenter(float_t tarX, float_t tarY){
  if(relaVarInCtrl[0][STATE_rlX]==0)
    flyRandomIn1meter(); // crazyflie 0 keeps random flight
  else{
    float dt = (float)(xTaskGetTickCount()-PreTime)/configTICK_RATE_HZ;
    PreTime = xTaskGetTickCount();
    if(dt > 1) // skip the first run of the EKF
      return;
    // pid control for formation flight
    float err_x = -(tarX - relaVarInCtrl[0][STATE_rlX]);
    float err_y = -(tarY - relaVarInCtrl[0][STATE_rlY]);
    float pid_vx = relaCtrl_p * err_x;
    float pid_vy = relaCtrl_p * err_y;
    float dx = (err_x - PreErr_x) / dt;
    float dy = (err_y - PreErr_y) / dt;
    PreErr_x = err_x;
    PreErr_y = err_y;
    pid_vx += relaCtrl_d * dx;
    pid_vy += relaCtrl_d * dy;
    IntErr_x += err_x * dt;
    IntErr_y += err_y * dt;
    pid_vx += relaCtrl_i * constrain(IntErr_x, -0.5, 0.5);
    pid_vy += relaCtrl_i * constrain(IntErr_y, -0.5, 0.5);
    pid_vx = constrain(pid_vx, -1, 1);
    pid_vy = constrain(pid_vy, -1, 1);  
    setHoverSetpoint(&setpoint, pid_vx, pid_vy, 0.5, 0);
  }
}

void relativeControlTask(void* arg)
{
  static uint32_t ctrlTick;
  systemWaitStart();

  // 
  // vTaskDelay(10000);
  // PreTime = xTaskGetTickCount();
  while(1) {
    vTaskDelay(10);
    // uint32_t osTick = xTaskGetTickCount();


        // setHoverSetpoint(&setpoint, pid_vx, pid_vy, 0.4, 0);
        if(relativeInfoRead((float_t *)relaVarInCtrl) && keepFlying){
          // take off
          if(onGround){
            for (int i=0; i<5; i++) {
              setHoverSetpoint(&setpoint, 0, 0, 0.3f, 0);
              vTaskDelay(M2T(100));
            }
            onGround = false;
            ctrlTick = xTaskGetTickCount();
          }

          // control loop
          // setHoverSetpoint(&setpoint, 0, 0, 0.5, 0); // hover
          if(xTaskGetTickCount() - ctrlTick < 10000){
            flyRandomIn1meter(); // random flight within first 10 seconds
            targetX = relaVarInCtrl[0][STATE_rlX];
            targetY = relaVarInCtrl[0][STATE_rlY];
          }
          else
          {
            formation0asCenter(targetX, targetY);
          }

        }else{
          // landing procedure
          if(!onGround){
            for (int i=1; i<5; i++) {
              setHoverSetpoint(&setpoint, 0, 0, 0.3f-(float)i*0.05f, 0);
              vTaskDelay(M2T(10));
            }
            onGround = true;
          } 
        }
  }
}

void relativeControlInit(void)
{
  if (isInit)
    return;
  xTaskCreate(relativeControlTask,"relative_Control",2*configMINIMAL_STACK_SIZE, NULL,3,NULL );
  isInit = true;
}

PARAM_GROUP_START(relative_ctrl)
PARAM_ADD(PARAM_UINT8, keepFlying, &keepFlying)
PARAM_ADD(PARAM_FLOAT, relaCtrl_p, &relaCtrl_p)
PARAM_ADD(PARAM_FLOAT, relaCtrl_i, &relaCtrl_i)
PARAM_ADD(PARAM_FLOAT, relaCtrl_d, &relaCtrl_d)
PARAM_GROUP_STOP(relative_ctrl)