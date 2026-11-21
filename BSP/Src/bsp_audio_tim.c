#include "bsp_audio_tim.h"
#include "audio_config.h"
#include "audio_pwm.h"
#include "main.h"

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

static volatile bool s_metro_on;
static volatile uint32_t s_metro_end;
static uint8_t s_metro_volume = AUDIO_METRO_VOLUME_DEFAULT;

static void audio_tim_config(TIM_TypeDef *instance, TIM_HandleTypeDef *htim,
                             uint32_t channel, uint32_t prescaler,
                             uint32_t period, uint32_t polarity, uint32_t pulse)
{
    TIM_OC_InitTypeDef oc = {0};

    /* HAL_TIM_PWM_ConfigChannel() 内部已经置了 OCxPE，比较值是预装载的：
     * 32 kHz 采样中断写 CCR 不会在载波周期中途截断脉冲。这里不要再配一次。 */
    htim->Instance = instance;
    htim->Init.Prescaler = prescaler;
    htim->Init.CounterMode = TIM_COUNTERMODE_UP;
    htim->Init.Period = period;
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(htim) != HAL_OK) {
        Error_Handler();
    }
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = pulse;
    oc.OCPolarity = polarity;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(htim, &oc, channel) != HAL_OK) {
        Error_Handler();
    }
}

void AudioTim_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();

    audio_tim_config(TIM2, &htim2, TIM_CHANNEL_1, 0U,
                     AUDIO_PWM_PERIOD - 1U, TIM_OCPOLARITY_HIGH, AUDIO_PWM_MIDPOINT);
    audio_tim_config(TIM3, &htim3, TIM_CHANNEL_4, AUDIO_PRESCALER,
                     AUDIO_ARR_DEFAULT, TIM_OCPOLARITY_LOW, 0U);

    gpio.Pin = BUZZER1_Pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BUZZER1_GPIO_Port, &gpio);
    gpio.Pin = BUZZER2_Pin;
    HAL_GPIO_Init(BUZZER2_GPIO_Port, &gpio);

    htim4.Instance = TIM4;
    htim4.Init.Prescaler = 0U;
    htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim4.Init.Period = AUDIO_TIMER_CLOCK_HZ / AUDIO_SAMPLE_HZ - 1U;
    htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim4) != HAL_OK) {
        Error_Handler();
    }
    HAL_NVIC_SetPriority(TIM4_IRQn, 1U, 0U);
}

void TIM4_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&htim4, TIM_FLAG_UPDATE) != RESET &&
        __HAL_TIM_GET_IT_SOURCE(&htim4, TIM_IT_UPDATE) != RESET) {
        __HAL_TIM_CLEAR_IT(&htim4, TIM_IT_UPDATE);
        AudioPwm_SampleIRQ();
    }
}

void AudioMetroBeep_Init(void)
{
    s_metro_on = false;
    s_metro_volume = AUDIO_METRO_VOLUME_DEFAULT;
    if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4) != HAL_OK) {
        Error_Handler();
    }
    AudioMetroBeep_Stop();
}

void AudioMetroBeep_SetVolume(uint8_t level)
{
    if ((level < AUDIO_METRO_VOLUME_MIN) || (level > AUDIO_METRO_VOLUME_MAX)) {
        return;
    }
    s_metro_volume = level;

    /* 蜂鸣器打到最大音量：占空比固定 50%（无源蜂鸣器的理论最大值）。
     * 界面上的 1/2/3 档仍可切换，但都按第 3 档驱动。 */
    s_metro_volume = AUDIO_METRO_VOLUME_MAX;
}

void AudioMetroBeep_Stop(void)
{
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    s_metro_on = false;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0U); /* Low polarity: idle high. */
    htim3.Instance->EGR = TIM_EVENTSOURCE_UPDATE;
    __set_PRIMASK(irq);
}

/* 被动蜂鸣器没有真正的幅度控制；在固定频率下用占空比近似音量。
 * 1/8、1/4、1/2 三档，默认 1/2 与原来的固定输出一致。 */
static uint32_t metro_beep_pulse(uint32_t arr, uint8_t level)
{
    (void)level;   /* 蜂鸣器固定 50% 占空比，档位不再改变波形 */
    uint32_t divisor = 2U;
    uint32_t pulse = (arr + 1U) / divisor;

    if (pulse < 1U) {
        pulse = 1U;
    }
    if (pulse > arr) {
        pulse = arr;
    }
    return pulse;
}

void AudioMetroBeep_PlayGain(uint16_t freq_hz, uint32_t duration_ms, uint8_t level)
{
    uint32_t arr = AudioPitch_FreqToArr(freq_hz);
    if (arr < 1U || duration_ms == 0U) {
        return;
    }
    if ((level < AUDIO_METRO_VOLUME_MIN) || (level > AUDIO_METRO_VOLUME_MAX)) {
        return;
    }

    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    __HAL_TIM_SET_AUTORELOAD(&htim3, arr);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, metro_beep_pulse(arr, level));
    htim3.Instance->EGR = TIM_EVENTSOURCE_UPDATE;
    s_metro_end = HAL_GetTick() + duration_ms;
    s_metro_on = true;
    __set_PRIMASK(irq);
}

void AudioMetroBeep_Play(uint16_t freq_hz, uint32_t duration_ms)
{
    AudioMetroBeep_PlayGain(freq_hz, duration_ms, s_metro_volume);
}

void AudioMetroBeep_Tick(uint32_t now_ms)
{
    if (s_metro_on && (int32_t)(now_ms - s_metro_end) >= 0) {
        AudioMetroBeep_Stop();
    }
}
