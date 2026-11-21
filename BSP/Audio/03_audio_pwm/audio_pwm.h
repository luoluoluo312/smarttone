#ifndef AUDIO_PWM_H
#define AUDIO_PWM_H

#include "audio_types.h"

/* 每个声部的波形。SQUARE 是原来的 50% 方波；ORGAN 是 25% 窄脉冲；
 * PIANO 是亮/暗两张波表；BELL 是旧八音盒波表；GUITAR 是失真吉他
 * 亮/暗两张波表（拨弦起音 → 箱体延音）。 */
typedef enum {
    AUDIO_WAVE_SQUARE = 0,
    AUDIO_WAVE_PIANO  = 1,   /* 亮/暗两张波表，用 mix 交叉淡化 */
    AUDIO_WAVE_BELL   = 2,   /* 旧八音盒/钟琴波表，保留兼容 */
    AUDIO_WAVE_ORGAN  = 3,   /* 25% 窄脉冲，管风琴的“簧片”音色 */
    AUDIO_WAVE_GUITAR = 4,   /* 失真吉他亮/暗波表 */
    AUDIO_WAVE_BASS   = 5    /* 电贝斯：近乎纯正弦的低厚音 */
} audio_wave_t;

/* AudioTim_Init configures TIM2 (carrier) and TIM4 (sample clock) first. */
void AudioPwm_Init(void);
bool AudioPwm_IsReady(uint8_t voice);
bool AudioPwm_SetTone(uint8_t voice, uint16_t freq_hz, uint16_t permille_on);
void AudioPwm_SetDuty(uint8_t voice, uint16_t permille_on);
void AudioPwm_SetWave(uint8_t voice, uint8_t wave);
void AudioPwm_SetWaveMix(uint8_t voice, uint8_t mix);  /* 0=亮波表, 255=暗波表 */
void AudioPwm_Silence(uint8_t voice);
uint32_t AudioPwm_GetArr(uint8_t voice);
/* TIM4 IRQ only: fixed-rate integer mixing, no blocking work. */
void AudioPwm_SampleIRQ(void);

/* Exclusive 16 kHz unsigned PCM stream; producer is main, consumer is TIM4. */
void AudioPwm_PcmBegin(uint32_t samples, uint8_t speed);
void AudioPwm_PcmSetSpeed(uint8_t speed); /* 0=0.5x, 1=1x, 2=2x; pitch follows rate. */
void AudioPwm_PcmStop(void);
void AudioPwm_PcmPause(bool paused);
void AudioPwm_PcmGain(uint16_t permille);
uint16_t AudioPwm_PcmWritable(uint8_t **buffer);
void AudioPwm_PcmCommit(uint16_t length);
uint32_t AudioPwm_PcmPosition(void);
uint32_t AudioPwm_PcmUnderruns(void);
bool AudioPwm_PcmActive(void);

#endif
