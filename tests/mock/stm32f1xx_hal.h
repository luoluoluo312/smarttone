#ifndef MOCK_STM32F1XX_HAL_H
#define MOCK_STM32F1XX_HAL_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int unused;
} SPI_HandleTypeDef;

typedef struct {
    int unused;
} GPIO_TypeDef;

typedef enum {
    HAL_OK = 0,
    HAL_ERROR,
    HAL_BUSY,
    HAL_TIMEOUT
} HAL_StatusTypeDef;

typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET = 1
} GPIO_PinState;

extern GPIO_TypeDef mock_gpio_b;

#define GPIOB (&mock_gpio_b)
#define GPIO_PIN_4  ((uint16_t)0x0010)
#define GPIO_PIN_5  ((uint16_t)0x0020)
#define GPIO_PIN_6  ((uint16_t)0x0040)
#define GPIO_PIN_7  ((uint16_t)0x0080)
#define GPIO_PIN_8  ((uint16_t)0x0100)
#define GPIO_PIN_9  ((uint16_t)0x0200)
#define GPIO_PIN_13 ((uint16_t)0x2000)
#define GPIO_PIN_10 ((uint16_t)0x0400)
#define GPIO_PIN_11 ((uint16_t)0x0800)

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *data,
                                   uint16_t size, uint32_t timeout);
void HAL_Delay(uint32_t ms);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
uint32_t HAL_GetTick(void);


/* Timer/GPIO subset used by the real BSP in host regression tests. */
#define GPIO_PIN_0  ((uint16_t)0x0001)
#define GPIO_PIN_1  ((uint16_t)0x0002)
#define GPIO_PIN_2  ((uint16_t)0x0004)
#define GPIO_PIN_3  ((uint16_t)0x0008)
#define GPIO_PIN_12 ((uint16_t)0x1000)
extern GPIO_TypeDef mock_gpio_a;
#define GPIOA (&mock_gpio_a)
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_AF_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define GPIO_SPEED_FREQ_HIGH 1U
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_AFIO_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM3_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM4_CLK_ENABLE() ((void)0)
static inline void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *g) { (void)p; (void)g; }
typedef struct { uint32_t PSC, ARR, CCR1, CCR4, EGR, SR, DIER, polarity; } TIM_TypeDef;
typedef struct {
    uint32_t Prescaler, CounterMode, Period, ClockDivision, AutoReloadPreload;
} TIM_Base_InitTypeDef;
typedef struct { TIM_TypeDef *Instance; TIM_Base_InitTypeDef Init; } TIM_HandleTypeDef;
typedef struct { uint32_t OCMode, Pulse, OCPolarity, OCFastMode; } TIM_OC_InitTypeDef;
extern TIM_TypeDef mock_tim2, mock_tim3, mock_tim4;
#define TIM2 (&mock_tim2)
#define TIM3 (&mock_tim3)
#define TIM4 (&mock_tim4)
#define TIM4_IRQn 30
#define TIM_CHANNEL_1 0U
#define TIM_CHANNEL_4 12U
#define TIM_COUNTERMODE_UP 0U
#define TIM_CLOCKDIVISION_DIV1 0U
#define TIM_AUTORELOAD_PRELOAD_ENABLE 1U
#define TIM_AUTORELOAD_PRELOAD_DISABLE 0U
#define TIM_OCMODE_PWM1 6U
#define TIM_OCPOLARITY_HIGH 0U
#define TIM_OCPOLARITY_LOW 2U
#define TIM_OCFAST_DISABLE 0U
#define TIM_FLAG_UPDATE 1U
#define TIM_IT_UPDATE 1U
#define TIM_EVENTSOURCE_UPDATE 1U
#define RESET 0U
#define __HAL_TIM_SET_COMPARE(h,c,v) ((c)==TIM_CHANNEL_1 ? ((h)->Instance->CCR1=(v)) : ((h)->Instance->CCR4=(v)))
#define __HAL_TIM_SET_AUTORELOAD(h,v) ((h)->Instance->ARR=(v))
#define __HAL_TIM_CLEAR_FLAG(h,v) ((h)->Instance->SR &= ~(v))
#define __HAL_TIM_CLEAR_IT(h,v) __HAL_TIM_CLEAR_FLAG(h,v)
#define __HAL_TIM_GET_FLAG(h,v) ((h)->Instance->SR & (v))
#define __HAL_TIM_GET_IT_SOURCE(h,v) ((h)->Instance->DIER & (v))
static inline HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *h) {
    h->Instance->ARR=h->Init.Period; h->Instance->PSC=h->Init.Prescaler; return HAL_OK;
}
static inline HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *h) { return HAL_TIM_Base_Init(h); }
static inline HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *h, TIM_OC_InitTypeDef *oc, uint32_t c) {
    __HAL_TIM_SET_COMPARE(h,c,oc->Pulse); h->Instance->polarity=oc->OCPolarity; return HAL_OK;
}
static inline HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *h, uint32_t c) { (void)h; (void)c; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *h) { h->Instance->DIER |= TIM_IT_UPDATE; return HAL_OK; }
static inline void HAL_NVIC_SetPriority(int i, uint32_t p, uint32_t s) { (void)i; (void)p; (void)s; }
static inline void HAL_NVIC_DisableIRQ(int i) { (void)i; }
static inline void HAL_NVIC_EnableIRQ(int i) { (void)i; }
static inline uint32_t __get_PRIMASK(void) { return 0U; }
static inline void __disable_irq(void) {}
static inline void __set_PRIMASK(uint32_t p) { (void)p; }
#endif
