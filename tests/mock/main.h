#ifndef MOCK_MAIN_H
#define MOCK_MAIN_H

#include "stm32f1xx_hal.h"

#define TFT_CS_Pin GPIO_PIN_5
#define TFT_CS_GPIO_Port GPIOB
#define TFT_DC_Pin GPIO_PIN_10
#define TFT_DC_GPIO_Port GPIOB
#define TFT_RES_Pin GPIO_PIN_11
#define TFT_RES_GPIO_Port GPIOB
#define W25Q64_CS_Pin GPIO_PIN_0
#define W25Q64_CS_GPIO_Port GPIOB
#define FUNC_Pin GPIO_PIN_8
#define FUNC_GPIO_Port GPIOA
#define ENC_A_Pin GPIO_PIN_6
#define ENC_A_GPIO_Port GPIOB
#define ENC_B_Pin GPIO_PIN_7
#define ENC_B_GPIO_Port GPIOB
#define NOTE_DO_Pin GPIO_PIN_1
#define NOTE_RE_Pin GPIO_PIN_2
#define NOTE_MI_Pin GPIO_PIN_3
#define NOTE_FA_Pin GPIO_PIN_4
#define NOTE_SOL_Pin GPIO_PIN_11
#define NOTE_LA_Pin GPIO_PIN_10
#define NOTE_SI_Pin GPIO_PIN_9

#define ENC_SW_Pin GPIO_PIN_13
#define ENC_SW_GPIO_Port GPIOB

#define BUZZER1_Pin GPIO_PIN_0
#define BUZZER1_GPIO_Port GPIOA
#define BUZZER2_Pin GPIO_PIN_1
#define BUZZER2_GPIO_Port GPIOB
void Error_Handler(void);
#endif
