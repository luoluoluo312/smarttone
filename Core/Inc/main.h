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
#include "stm32f1xx_hal.h"

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
#define TFT_CS_Pin GPIO_PIN_5
#define TFT_CS_GPIO_Port GPIOB
#define TFT_DC_Pin GPIO_PIN_10
#define TFT_DC_GPIO_Port GPIOB
#define TFT_RES_Pin GPIO_PIN_11
#define TFT_RES_GPIO_Port GPIOB
#define BUZZER1_Pin GPIO_PIN_0
#define BUZZER1_GPIO_Port GPIOA
#define BUZZER2_Pin GPIO_PIN_1
#define BUZZER2_GPIO_Port GPIOB
#define W25Q64_CS_Pin GPIO_PIN_0
#define W25Q64_CS_GPIO_Port GPIOB
#define FUNC_Pin GPIO_PIN_8
#define FUNC_GPIO_Port GPIOA

#define NOTE_DO_Pin GPIO_PIN_1
#define NOTE_DO_GPIO_Port GPIOA
#define NOTE_RE_Pin GPIO_PIN_2
#define NOTE_RE_GPIO_Port GPIOA
#define NOTE_MI_Pin GPIO_PIN_3
#define NOTE_MI_GPIO_Port GPIOA
#define NOTE_FA_Pin GPIO_PIN_4
#define NOTE_FA_GPIO_Port GPIOA
#define NOTE_SOL_Pin GPIO_PIN_11
#define NOTE_SOL_GPIO_Port GPIOA
#define NOTE_LA_Pin GPIO_PIN_10
#define NOTE_LA_GPIO_Port GPIOA
#define NOTE_SI_Pin GPIO_PIN_9
#define NOTE_SI_GPIO_Port GPIOA

#define LED_DO_Pin GPIO_PIN_12
#define LED_DO_GPIO_Port GPIOB
#define LED_RE_Pin GPIO_PIN_14
#define LED_RE_GPIO_Port GPIOB
#define LED_MI_Pin GPIO_PIN_15
#define LED_MI_GPIO_Port GPIOB
#define LED_FA_Pin GPIO_PIN_12
#define LED_FA_GPIO_Port GPIOA
#define LED_SOL_Pin GPIO_PIN_15
#define LED_SOL_GPIO_Port GPIOA
#define LED_LA_Pin GPIO_PIN_3
#define LED_LA_GPIO_Port GPIOB
#define LED_SI_Pin GPIO_PIN_4
#define LED_SI_GPIO_Port GPIOB
#define LED_FUNC_Pin GPIO_PIN_13
#define LED_FUNC_GPIO_Port GPIOC
#define ENC_A_Pin GPIO_PIN_6
#define ENC_A_GPIO_Port GPIOB
#define ENC_B_Pin GPIO_PIN_7
#define ENC_B_GPIO_Port GPIOB
#define ENC_SW_Pin GPIO_PIN_13
#define ENC_SW_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
