/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
uint8_t  g_door_status = 0;   // 车门状态: 0=关闭, 1=打开
uint16_t g_tpms_value  = 250; // 胎压数值: 默认 250 kPa
uint8_t  g_fault_code  = 0;   // 故障码: 0=正常, 1=发动机故障等

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_Door */
osThreadId_t Task_DoorHandle;
const osThreadAttr_t Task_Door_attributes = {
  .name = "Task_Door",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_TPMS */
osThreadId_t Task_TPMSHandle;
const osThreadAttr_t Task_TPMS_attributes = {
  .name = "Task_TPMS",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for Task_Fault */
osThreadId_t Task_FaultHandle;
const osThreadAttr_t Task_Fault_attributes = {
  .name = "Task_Fault",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void App_DoorTask(void *argument);
void App_TPMSTask(void *argument);
void App_FaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of Task_Door */
  Task_DoorHandle = osThreadNew(App_DoorTask, NULL, &Task_Door_attributes);

  /* creation of Task_TPMS */
  Task_TPMSHandle = osThreadNew(App_TPMSTask, NULL, &Task_TPMS_attributes);

  /* creation of Task_Fault */
  Task_FaultHandle = osThreadNew(App_FaultTask, NULL, &Task_Fault_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
	// 1. 任务启动时，只执行一次初始化和静态界面的绘制
  OLED_Init();
  OLED_Clear();
  OLED_PrintString(0, 0, "Door :");
  OLED_PrintString(0, 2, "TPMS :");
  OLED_PrintString(0, 4, "Fault:");
  /* Infinite loop */
  for(;;)
  {
    // 2. 动态刷新车门状态
    if(g_door_status == 0){
        OLED_PrintString(6, 0, "Closed "); // 多留点空格覆盖旧字符
    } 
		else{
        OLED_PrintString(6, 0, "Opened ");
    }

    // 3. 动态刷新胎压数值 (使用你驱动里的10进制打印函数)
    OLED_PrintSignedVal(6, 2, g_tpms_value);
    OLED_PrintString(10, 2, "kPa "); 

    // 4. 动态刷新故障状态
    if(g_fault_code == 0){
        OLED_PrintString(6, 4, "OK   ");
    } 
		else{
        OLED_PrintString(6, 4, "ERROR");
    }

    // 5. 延时 100ms (10FPS 的刷新率对肉眼足够了，且不占用太多CPU)
    osDelay(100);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_App_DoorTask */
/**
* @brief Function implementing the Task_Door thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_App_DoorTask */
void App_DoorTask(void *argument)
{
  /* USER CODE BEGIN App_DoorTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END App_DoorTask */
}

/* USER CODE BEGIN Header_App_TPMSTask */
/**
* @brief Function implementing the Task_TPMS thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_App_TPMSTask */
void App_TPMSTask(void *argument)
{
  /* USER CODE BEGIN App_TPMSTask */
  /* Infinite loop */
  for(;;)
  {
    // 模拟胎压变化 (比如在 240~260 之间波动)
    g_tpms_value++;
    if(g_tpms_value > 260) g_tpms_value = 240;

    // TODO: 在这里调用 HAL_CAN_AddTxMessage 把胎压发给 IMX6ULL

    // 胎压不需要发太快，500ms 发一次即可
    osDelay(500);
  }
  /* USER CODE END App_TPMSTask */
}

/* USER CODE BEGIN Header_App_FaultTask */
/**
* @brief Function implementing the Task_Fault thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_App_FaultTask */
void App_FaultTask(void *argument)
{
  /* USER CODE BEGIN App_FaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END App_FaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

