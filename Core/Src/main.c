/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32F103C8T6 + 1.8" ST7735 TFT: SmartTone UI
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
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp_tft.h"
#include "bsp_encoder.h"
#include "bsp_func.h"
#include "bsp_notes.h"
#include "bsp_audio_tim.h"
#include "audio.h"
#include "audio_pwm.h"
#include "ui_page.h"
#include "record.h"
#include "boot_logo.h"
#include "rhythm_game.h"
#include "melody_maker.h"
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
SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();


  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */
  /* 先把背光点亮，面板上电期间状态才确定；不依赖按键。 */
  tft_backlight_init();
  tft_set_brightness(5U);
  HAL_Delay(120);   /* 等面板电源稳定，冷启动时这一步很重要 */

  if (!tft_init(&hspi1)) {
    /* 仍然失败就再降速到 1.125 MHz 重试一次 */
    __HAL_SPI_DISABLE(&hspi1);
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
    (void)HAL_SPI_Init(&hspi1);
    (void)tft_init(&hspi1);
  }
  AudioTim_Init();
  Audio_Init();
  Audio_AllOff();   /* 上电保险：任何残留声部立即静音，杜绝长响 */
  bsp_notes_init();
  rhythm_game_init();
  bsp_encoder_init();
  bsp_func_init();
  record_init(&hspi1);

  /* 外设初始化完成后先显示启动图；主菜单延迟到 5 s 启动流程结束后绘制。 */
  boot_logo_start();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint32_t now = HAL_GetTick();

    /* 启动图期间只推进 SysTick 计时，不扫描编码器、FUNC 或音符按键。 */
    if (boot_logo_active()) {
      if (boot_logo_tick(now)) {
        ui_page_init();
      }
      continue;
    }

    Audio_Envelope_Tick();
    Metronome_Tick();
    Song_Tick();
    Drum_Tick();
    if (!melody_maker_active() && !AudioPwm_PcmActive()) { bsp_notes_poll(now); }
    bsp_encoder_poll(now);
    bsp_func_poll(now);
    ui_page_tick(now);

    bsp_encoder_event_t event;
    while (bsp_encoder_take_event(&event)) {
      ui_page_handle_event(event);
    }
    switch (bsp_func_take_event()) {
      case BSP_FUNC_EVENT_SHORT: ui_page_handle_func();      break;
      case BSP_FUNC_EVENT_LONG:  ui_page_handle_func_long(); break;
      default: break;
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32; /* 72 MHz / 32 = 2.25 MHz，杜邦线上更稳 */
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /* PA15/PB3/PB4 作为 LED，需要关闭 JTAG、保留 SWD。 */
  __HAL_AFIO_REMAP_SWJ_NOJTAG();

  /* TFT 控制脚初始为高电平，DC 在驱动内部发送命令时再拉低。 */
  HAL_GPIO_WritePin(GPIOB, TFT_CS_Pin | TFT_DC_Pin | TFT_RES_Pin, GPIO_PIN_SET);
  /* W25Q64 独立片选 PB0，空闲保持高电平。 */
  HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_SET);
  /* 蜂鸣器和 LED 初始关闭。 */
  HAL_GPIO_WritePin(GPIOA, BUZZER1_Pin | LED_FA_Pin | LED_SOL_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB,
                    BUZZER2_Pin | LED_DO_Pin | LED_RE_Pin | LED_MI_Pin |
                    LED_LA_Pin | LED_SI_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOC, LED_FUNC_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : TFT_CS_Pin TFT_DC_Pin TFT_RES_Pin */
  GPIO_InitStruct.Pin = TFT_CS_Pin | TFT_DC_Pin | TFT_RES_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* W25Q64 片选 PB0，推挽输出，初始高电平。 */
  GPIO_InitStruct.Pin = W25Q64_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(W25Q64_CS_GPIO_Port, &GPIO_InitStruct);

  /* 两路蜂鸣器输出，初始低电平。 */
  GPIO_InitStruct.Pin = BUZZER1_Pin | LED_FA_Pin | LED_SOL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = BUZZER2_Pin | LED_DO_Pin | LED_RE_Pin |
                        LED_MI_Pin | LED_LA_Pin | LED_SI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LED_FUNC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* EC11 A=PB6、B=PB7、按键=PB13：上拉输入，公共端接 GND。 */
  GPIO_InitStruct.Pin = ENC_A_Pin | ENC_B_Pin | ENC_SW_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* FUNC 按键 PA8：上拉输入，按下接地。 */
  GPIO_InitStruct.Pin = FUNC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(FUNC_GPIO_Port, &GPIO_InitStruct);

  /* 7 个音符按键：PA1/2/3/4/11/10/9，上拉输入，按下为低。 */
  GPIO_InitStruct.Pin = NOTE_DO_Pin | NOTE_RE_Pin | NOTE_MI_Pin |
                        NOTE_FA_Pin | NOTE_SOL_Pin | NOTE_LA_Pin |
                        NOTE_SI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* PB1 passive buzzer is active low: idle high before display initialization. */
  HAL_GPIO_WritePin(BUZZER2_GPIO_Port, BUZZER2_Pin, GPIO_PIN_SET);

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
