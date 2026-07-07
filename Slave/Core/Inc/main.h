/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define KEY2_Pin GPIO_PIN_2
#define KEY2_GPIO_Port GPIOE
#define KEY3_Pin GPIO_PIN_3
#define KEY3_GPIO_Port GPIOE
#define KEY4_Pin GPIO_PIN_4
#define KEY4_GPIO_Port GPIOE
#define KEY5_Pin GPIO_PIN_5
#define KEY5_GPIO_Port GPIOE
#define KEY6_Pin GPIO_PIN_6
#define KEY6_GPIO_Port GPIOE
#define NFC_MOSI_Pin GPIO_PIN_0
#define NFC_MOSI_GPIO_Port GPIOA
#define SPI1_CS_Pin GPIO_PIN_4
#define SPI1_CS_GPIO_Port GPIOC
#define LED1_Pin GPIO_PIN_8
#define LED1_GPIO_Port GPIOE
#define LED2_Pin GPIO_PIN_9
#define LED2_GPIO_Port GPIOE
#define LED3_Pin GPIO_PIN_10
#define LED3_GPIO_Port GPIOE
#define LED4_Pin GPIO_PIN_11
#define LED4_GPIO_Port GPIOE
#define LED5_Pin GPIO_PIN_12
#define LED5_GPIO_Port GPIOE
#define LED6_Pin GPIO_PIN_13
#define LED6_GPIO_Port GPIOE
#define LED7_Pin GPIO_PIN_14
#define LED7_GPIO_Port GPIOE
#define LED8_Pin GPIO_PIN_15
#define LED8_GPIO_Port GPIOE
#define NFC_GND_Pin GPIO_PIN_10
#define NFC_GND_GPIO_Port GPIOB
#define NFC_MISO_Pin GPIO_PIN_13
#define NFC_MISO_GPIO_Port GPIOB
#define NFC_RST_Pin GPIO_PIN_14
#define NFC_RST_GPIO_Port GPIOB
#define NFC_NSS_Pin GPIO_PIN_15
#define NFC_NSS_GPIO_Port GPIOB
#define NFC_SCK_Pin GPIO_PIN_9
#define NFC_SCK_GPIO_Port GPIOD
#define BEEP_Pin GPIO_PIN_4
#define BEEP_GPIO_Port GPIOB
#define TEMP_Pin GPIO_PIN_0
#define TEMP_GPIO_Port GPIOE
#define KEY1_Pin GPIO_PIN_1
#define KEY1_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */
#define DATA_Pin TEMP_Pin
#define DATA_GPIO_Port TEMP_GPIO_Port
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
