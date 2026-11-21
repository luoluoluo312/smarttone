#ifndef BSP_AUDIO_TIM_H
#define BSP_AUDIO_TIM_H

#include <stdint.h>

/* PA0/TIM2_CH1: speaker carrier; PB1/TIM3_CH4: active-low passive buzzer.
 * TIM4 supplies the sample clock without configuring any TIM4 GPIO pins. */
void AudioTim_Init(void);
void AudioMetroBeep_Init(void);

/* PB1 无源蜂鸣器音量：1=轻，2=中，3=响。
 * 只改 PWM 占空比，不改频率/ARR；默认第 3 档（原来的 50% 占空比）。 */
void AudioMetroBeep_SetVolume(uint8_t level);

/* 用当前设置的音量敲一下。 */
void AudioMetroBeep_Play(uint16_t freq_hz, uint32_t duration_ms);

/* 用指定音量敲一下：游戏倒计时等“固定档提示音”用，不受节拍器音量影响。 */
void AudioMetroBeep_PlayGain(uint16_t freq_hz, uint32_t duration_ms, uint8_t level);
void AudioMetroBeep_Stop(void);
void AudioMetroBeep_Tick(uint32_t now_ms); /* SysTick: bounded click duration. */

#endif
