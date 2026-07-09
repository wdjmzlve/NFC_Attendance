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
#include "rc522.h"
#include "app_tasks.h"
#include <stdio.h>
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

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

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
  /* RC522 mutex: shared between CardRead and Serial tasks */
  rc522MutexHandle = osMutexNew(NULL);
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* Serial command semaphore: ISR releases, Task_Serial acquires */
  s_cmdSem = osSemaphoreNew(1, 0, NULL);
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* Card info queue: CardRead task -> Display task */
  cardQueueHandle = osMessageQueueNew(4, sizeof(CardInfo_t), NULL);
  /* Key event queue: KeyScan task -> Display task */
  keyQueueHandle  = osMessageQueueNew(8, sizeof(KeyMsg_t), NULL);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* Display task: clock + settings UI + card info display */
  {
      static const osThreadAttr_t displayTask_attr = {
          .name = "Task_Display",
          .stack_size = 256 * 4,
          .priority = osPriorityNormal,
      };
      osThreadNew(Task_Display, NULL, &displayTask_attr);
  }

  /* Key scan task: polls GPIO keys, sends KeyMsg_t to Display task via queue */
  {
      static const osThreadAttr_t keyScanTask_attr = {
          .name = "Task_KeyScan",
          .stack_size = 128 * 4,
          .priority = osPriorityNormal,
      };
      osThreadNew(Task_KeyScan, NULL, &keyScanTask_attr);
  }

  /* Card read task: RC522 polling */
  {
      static const osThreadAttr_t cardReadTask_attr = {
          .name = "Task_CardRead",
          .stack_size = 256 * 4,
          .priority = osPriorityNormal,
      };
      osThreadNew(Task_CardRead, NULL, &cardReadTask_attr);
  }

  /* Serial command processing task: USART1 host protocol */
  {
      static const osThreadAttr_t serialTask_attr = {
          .name = "Task_Serial",
          .stack_size = 256 * 4,
          .priority = osPriorityNormal,
      };
      osThreadNew(Task_Serial, NULL, &serialTask_attr);
  }
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

  /* Hardware initialization now handled by Task_Display and Task_CardRead.
   * This default task is kept idle to preserve the CubeMX task framework. */

  /* Infinite loop */
  for(;;)
  {
    osDelay(1000);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

