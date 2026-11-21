/* Two software voices mixed onto PA0 / TIM2_CH1. PB1 belongs to the metronome. */
#include "audio_pwm.h"
#include "audio_config.h"

extern TIM_HandleTypeDef htim4;

/* ------------------------------------------------------------ 音色波表
 *
 * 128 点、8 bit 有符号、零均值。相位累加器取最高 7 位查表。
 * PIANO 用亮/暗两张表按 mix 交叉淡化，模拟高次泛音先衰减；
 * GUITAR 也是亮/暗两张表：拨弦瞬间更亮，随后落到箱体音；
 * BELL 是旧八音盒波表，保留兼容。
 * 较轻的波表离线调整谐波相位以提高有效值，峰值仍不超过 120；
 * 钢琴亮/暗表使用共同相位调整，保持交叉淡化的一致性。
 * 全部放在 Flash（const），不占 RAM。
 */
static const int8_t s_wave_bass[128] = {
    0, 8, 16, 24, 32, 39, 47, 54, 61, 67, 73, 79,
    85, 90, 95, 99, 103, 106, 109, 112, 114, 116, 118, 119,
    119, 120, 120, 120, 119, 119, 118, 117, 115, 114, 112, 110,
    108, 106, 104, 102, 99, 96, 94, 91, 88, 84, 81, 78,
    74, 70, 66, 62, 58, 54, 49, 45, 40, 35, 31, 26,
    21, 16, 10, 5, 0, -5, -10, -16, -21, -26, -31, -35,
    -40, -45, -49, -54, -58, -62, -66, -70, -74, -78, -81, -84,
    -88, -91, -94, -96, -99, -102, -104, -106, -108, -110, -112, -114,
    -115, -117, -118, -119, -119, -120, -120, -120, -119, -119, -118, -116,
    -114, -112, -109, -106, -103, -99, -95, -90, -85, -79, -73, -67,
    -61, -54, -47, -39, -32, -24, -16, -8,
};

static const int8_t s_wave_piano_bright[128] = {
    47, 42, 36, 29, 23, 18, 12, 7, 3, -1, -3, -5,
    -5, -5, -4, -2, -1, 2, 4, 6, 9, 12, 16, 18,
    21, 26, 29, 35, 40, 47, 52, 59, 67, 74, 82, 89,
    96, 103, 108, 113, 116, 118, 119, 120, 119, 117, 114, 110,
    105, 101, 97, 92, 87, 83, 77, 72, 67, 62, 55, 49,
    42, 35, 26, 17, 7, -3, -13, -25, -34, -45, -55, -64,
    -71, -78, -84, -88, -90, -92, -93, -92, -89, -88, -85, -83,
    -81, -78, -77, -77, -77, -78, -79, -81, -83, -87, -90, -94,
    -98, -100, -103, -106, -107, -107, -106, -105, -102, -97, -93, -85,
    -78, -70, -60, -49, -39, -28, -16, -6, 6, 16, 26, 35,
    43, 49, 52, 55, 57, 56, 55, 52,
};

static const int8_t s_wave_piano_mellow[128] = {
    17, 20, 22, 25, 27, 29, 31, 32, 34, 37, 37, 39,
    41, 43, 45, 46, 49, 50, 53, 56, 59, 61, 64, 68,
    71, 75, 79, 82, 85, 89, 93, 97, 100, 104, 106, 109,
    111, 113, 114, 116, 117, 116, 116, 115, 113, 112, 109, 106,
    103, 99, 94, 89, 84, 79, 73, 67, 60, 53, 46, 39,
    31, 24, 16, 9, 2, -6, -12, -19, -26, -33, -40, -46,
    -53, -58, -63, -69, -75, -79, -84, -88, -92, -96, -99, -103,
    -106, -109, -111, -113, -116, -117, -118, -119, -120, -120, -119, -119,
    -118, -117, -116, -114, -111, -109, -106, -102, -98, -94, -90, -85,
    -80, -75, -70, -65, -59, -54, -48, -42, -36, -31, -26, -21,
    -16, -11, -6, -1, 2, 7, 10, 14,
};

static const int8_t s_wave_bell[128] = {
    59, 71, 82, 94, 105, 114, 119, 120, 116, 107, 91, 71,
    49, 24, 1, -20, -36, -48, -53, -53, -49, -40, -30, -17,
    -7, 4, 14, 23, 30, 38, 45, 53, 62, 70, 78, 85,
    90, 93, 95, 94, 91, 88, 86, 84, 85, 88, 93, 98,
    102, 106, 106, 104, 97, 87, 73, 56, 40, 24, 11, 1,
    -6, -10, -10, -7, -5, -4, -4, -7, -12, -20, -30, -42,
    -54, -66, -76, -83, -90, -94, -98, -101, -103, -104, -104, -102,
    -99, -91, -81, -69, -52, -35, -17, 0, 14, 23, 27, 26,
    19, 6, -9, -26, -44, -61, -75, -85, -92, -96, -97, -97,
    -96, -96, -96, -97, -99, -99, -97, -95, -90, -81, -70, -57,
    -42, -28, -12, 1, 15, 27, 38, 49,
};

/* 手风琴：保留簧片音源的谐波强弱，离线降低峰均比以提高持续音响度。 */
static const int8_t s_wave_organ[128] = {
    104, 111, 115, 118, 120, 117, 113, 106, 97, 86, 72, 57,
    39, 22, 4, -13, -27, -40, -50, -58, -64, -64, -62, -56,
    -49, -38, -25, -9, 7, 26, 43, 60, 76, 90, 99, 106,
    108, 105, 98, 86, 73, 56, 42, 26, 14, 5, 2, 4,
    11, 23, 37, 54, 71, 87, 99, 108, 109, 105, 97, 83,
    66, 47, 28, 11, -5, -17, -24, -28, -28, -26, -22, -18,
    -14, -12, -13, -17, -22, -29, -35, -44, -49, -55, -58, -59,
    -58, -56, -52, -50, -48, -49, -50, -54, -61, -68, -78, -87,
    -96, -103, -109, -113, -115, -115, -113, -110, -105, -102, -100, -97,
    -93, -92, -90, -85, -83, -77, -69, -62, -50, -37, -24, -8,
    7, 23, 37, 52, 65, 77, 87, 97,
};

static const int8_t s_wave_guitar_bright[128] = {
    0, 82, 111, 118, 119, 120, 120, 120, 119, 119, 118, 116,
    116, 115, 116, 116, 117, 117, 117, 117, 116, 115, 113, 111,
    110, 109, 109, 109, 110, 110, 110, 109, 107, 105, 102, 98,
    95, 93, 92, 92, 93, 93, 92, 90, 87, 82, 76, 70,
    64, 60, 57, 56, 57, 56, 55, 52, 46, 38, 29, 20,
    11, 5, 2, 0, 0, 0, -2, -5, -11, -20, -29, -38,
    -46, -52, -55, -56, -57, -56, -57, -60, -64, -70, -76, -82,
    -87, -90, -92, -93, -93, -92, -92, -93, -95, -98, -102, -105,
    -107, -109, -110, -110, -110, -109, -109, -109, -110, -111, -113, -115,
    -116, -117, -117, -117, -117, -116, -116, -115, -116, -116, -118, -119,
    -119, -120, -120, -120, -119, -118, -111, -82,
};

static const int8_t s_wave_guitar_body[128] = {
    23, 36, 48, 59, 69, 76, 82, 87, 89, 91, 91, 90,
    88, 86, 84, 81, 79, 76, 74, 73, 71, 71, 70, 70,
    71, 71, 71, 71, 71, 71, 70, 69, 68, 66, 63, 61,
    58, 55, 52, 49, 46, 43, 41, 39, 37, 35, 34, 33,
    32, 30, 29, 28, 27, 25, 23, 21, 18, 15, 12, 9,
    6, 3, 0, -3, -6, -8, -10, -12, -14, -15, -16, -17,
    -17, -18, -19, -20, -21, -22, -24, -26, -28, -31, -33, -36,
    -39, -41, -44, -46, -48, -49, -50, -51, -51, -51, -51, -51,
    -51, -51, -51, -52, -54, -57, -60, -64, -69, -74, -80, -87,
    -93, -100, -106, -111, -115, -118, -120, -120, -118, -114, -108, -100,
    -90, -79, -66, -52, -37, -22, -7, 8,
};

static struct {
    volatile uint32_t step;       /* Main loop publishes aligned single-word values. */
    volatile uint32_t amplitude;
    uint32_t phase;               /* Written only by the sample interrupt. */
    volatile uint8_t  wave;       /* audio_wave_t */
    volatile uint8_t  mix;        /* PIANO: 0 = bright table, 255 = mellow table */
} s_pwm[AUDIO_VOICE_MAX];
static bool s_ready;
/* 输出门控：无声时把 PA0 的 PWM 占空比缓降到 0，消除空闲载波底噪。 */
static uint32_t s_output_level;

#define PCM_RING_SIZE 2048U
static uint8_t s_pcm_ring[PCM_RING_SIZE];
static struct {
    volatile uint32_t read, written, total, underruns;
    volatile uint16_t gain;
    volatile bool enabled, paused;
    volatile uint8_t divider;
    uint8_t phase;
    int16_t sample;
} s_pcm;

void AudioPwm_PcmStop(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    s_pcm.enabled = false;
    s_pcm.sample = 0;
    __set_PRIMASK(mask);
}

void AudioPwm_PcmSetSpeed(uint8_t speed)
{
    if (speed > 2U) { return; }
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    s_pcm.divider = (uint8_t)(4U >> speed);
    s_pcm.phase = 0U;
    __set_PRIMASK(mask);
}

void AudioPwm_PcmBegin(uint32_t samples, uint8_t speed)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    s_pcm.enabled = false;
    s_pcm.read = s_pcm.written = s_pcm.underruns = 0U;
    s_pcm.total = samples;
    s_pcm.phase = 0U;
    s_pcm.divider = (uint8_t)(4U >> (speed <= 2U ? speed : 1U));
    s_pcm.sample = 0;
    s_pcm.gain = 500U;
    s_pcm.paused = true; /* Publish data before unpausing. */
    s_pcm.enabled = s_ready && samples != 0U;
    __set_PRIMASK(mask);
}

void AudioPwm_PcmPause(bool paused) { s_pcm.paused = paused; }
void AudioPwm_PcmGain(uint16_t gain) { s_pcm.gain = gain > 558U ? 558U : gain; }
bool AudioPwm_PcmActive(void) { return s_pcm.enabled; }
uint32_t AudioPwm_PcmPosition(void) { return s_pcm.read; }
uint32_t AudioPwm_PcmUnderruns(void) { return s_pcm.underruns; }

uint16_t AudioPwm_PcmWritable(uint8_t **buffer)
{
    uint32_t written = s_pcm.written;
    uint32_t free = PCM_RING_SIZE - (written - s_pcm.read);
    uint32_t contiguous = PCM_RING_SIZE - written % PCM_RING_SIZE;
    uint32_t remaining = s_pcm.total - written;
    /* Batch small reads, but allow a short tail at ring wrap or end of file. */
    if (free < 256U && remaining > free) { free = 0U; }
    if (free > contiguous) { free = contiguous; }
    if (free > remaining) { free = remaining; }
    *buffer = &s_pcm_ring[written % PCM_RING_SIZE];
    return s_pcm.enabled ? (uint16_t)free : 0U;
}

void AudioPwm_PcmCommit(uint16_t length)
{
    /* Ring contents must reach RAM before the IRQ sees the published cursor. */
    __asm volatile ("" ::: "memory");
    s_pcm.written += length;
}

void AudioPwm_Init(void)
{
    AudioPwm_PcmStop();
    s_ready = false;
    HAL_NVIC_DisableIRQ(TIM4_IRQn);
    for (uint8_t i = 0U; i < AUDIO_VOICE_MAX; ++i) {
        s_pwm[i].step = 0U;
        s_pwm[i].amplitude = 0U;
        s_pwm[i].phase = 0U;
        s_pwm[i].wave = AUDIO_WAVE_SQUARE;
        s_pwm[i].mix = 0U;
    }
    s_output_level = 0U;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0U);
    if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK) {
        return;
    }
    __HAL_TIM_CLEAR_FLAG(&htim4, TIM_FLAG_UPDATE);
    s_ready = true;
    HAL_NVIC_EnableIRQ(TIM4_IRQn);
    if (HAL_TIM_Base_Start_IT(&htim4) != HAL_OK) {
        HAL_NVIC_DisableIRQ(TIM4_IRQn);
        s_ready = false;
    }
}

bool AudioPwm_IsReady(uint8_t voice)
{
    return voice < AUDIO_VOICE_MAX && s_ready;
}

bool AudioPwm_SetTone(uint8_t voice, uint16_t freq_hz, uint16_t permille_on)
{
    if (!AudioPwm_IsReady(voice) || freq_hz < AUDIO_FREQ_MIN_HZ ||
        freq_hz > AUDIO_FREQ_MAX_HZ) {
        return false;
    }
    /* Q32 phase increment; division is outside the interrupt. */
    s_pwm[voice].step = (uint32_t)(((uint64_t)freq_hz << 32) / AUDIO_SAMPLE_HZ);
    AudioPwm_SetDuty(voice, permille_on);
    return true;
}

void AudioPwm_SetDuty(uint8_t voice, uint16_t permille_on)
{
    if (!AudioPwm_IsReady(voice)) {
        return;
    }
    if (permille_on > 500U) {
        permille_on = 500U;
    }
    /* The legacy duty parameter now controls amplitude, not pulse width.
     * Round to nearest count so the 1..5 volume steps and the envelope
     * floor stay linear; truncation would silently drop a step at low volume. */
    s_pwm[voice].amplitude = (AUDIO_SPEAKER_PEAK * permille_on + 250U) / 500U;
}

void AudioPwm_SetWave(uint8_t voice, uint8_t wave)
{
    if (!AudioPwm_IsReady(voice)) {
        return;
    }
    if (wave > (uint8_t)AUDIO_WAVE_BASS) {
        wave = (uint8_t)AUDIO_WAVE_GUITAR;
    }
    s_pwm[voice].wave = wave;
}

void AudioPwm_SetWaveMix(uint8_t voice, uint8_t mix)
{
    if (!AudioPwm_IsReady(voice)) {
        return;
    }
    s_pwm[voice].mix = mix;
}

void AudioPwm_Silence(uint8_t voice)
{
    AudioPwm_SetDuty(voice, 0U);
}

uint32_t AudioPwm_GetArr(uint8_t voice)
{
    return voice < AUDIO_VOICE_MAX ? AUDIO_PWM_PERIOD - 1U : 0U;
}

void AudioPwm_SampleIRQ(void)
{
    int32_t sample = AUDIO_PWM_MIDPOINT;
    uint32_t amplitude_sum = 0U;
    bool voice_active = false;
    if (s_pcm.enabled) {
        if (!s_pcm.paused && s_pcm.read < s_pcm.total) {
            voice_active = true;
            if (s_pcm.phase == 0U) {
                uint32_t read = s_pcm.read;
                if (read < s_pcm.written) {
                    s_pcm.sample = (int16_t)s_pcm_ring[read % PCM_RING_SIZE] - 128;
                    s_pcm.read = read + 1U;
                } else {
                    s_pcm.sample = 0;
                    ++s_pcm.underruns;
                }
            }
            if (++s_pcm.phase >= s_pcm.divider) { s_pcm.phase = 0U; }
            sample += (s_pcm.sample * (int32_t)s_pcm.gain) / 500;
        }
    } else for (uint8_t i = 0U; i < AUDIO_VOICE_MAX; ++i) {
        s_pwm[i].phase += s_pwm[i].step;
        int32_t amplitude = (int32_t)s_pwm[i].amplitude;
        if (amplitude == 0) {
            continue;
        }
        voice_active = true;
        amplitude_sum += (uint32_t)amplitude;
        if (s_pwm[i].wave == AUDIO_WAVE_SQUARE) {
            sample += (s_pwm[i].phase & 0x80000000UL) ? amplitude : -amplitude;
        } else {
            uint8_t idx = (uint8_t)(s_pwm[i].phase >> 25);   /* 128 点波表 */
            int32_t v;
            if (s_pwm[i].wave == AUDIO_WAVE_PIANO) {
                uint32_t mix = s_pwm[i].mix;
                v = ((int32_t)s_wave_piano_bright[idx] * (int32_t)(256U - mix) +
                     (int32_t)s_wave_piano_mellow[idx] * (int32_t)mix) >> 8;
            } else if (s_pwm[i].wave == AUDIO_WAVE_ORGAN) {
                v = (int32_t)s_wave_organ[idx];
            } else if (s_pwm[i].wave == AUDIO_WAVE_GUITAR) {
                uint32_t mix = s_pwm[i].mix;
                v = ((int32_t)s_wave_guitar_bright[idx] * (int32_t)(256U - mix) +
                     (int32_t)s_wave_guitar_body[idx] * (int32_t)mix) >> 8;
            }
            else if (s_pwm[i].wave == AUDIO_WAVE_BASS) {
                v = (int32_t)s_wave_bass[idx];
            } else {
                v = (int32_t)s_wave_bell[idx];
            }
            sample += (v * amplitude + 64) >> 7;
        }
    }
    /* 双声部叠加超过 PWM 量程时按总振幅回缩，单声部仍可接近满幅。 */
    if (amplitude_sum > (AUDIO_PWM_MIDPOINT - 1U)) {
        int32_t deviation = sample - AUDIO_PWM_MIDPOINT;
        sample = AUDIO_PWM_MIDPOINT +
                 (deviation * (int32_t)(AUDIO_PWM_MIDPOINT - 1U)) /
                 (int32_t)amplitude_sum;
    }
    if (sample < 0) {
        sample = 0;
    } else if (sample > (int32_t)(AUDIO_PWM_PERIOD - 1U)) {
        sample = (int32_t)(AUDIO_PWM_PERIOD - 1U);
    }

    /* 空闲时缓降到 0；起音时缓升回中点，避免直接切换造成爆音。 */
    if (voice_active) {
        if (s_output_level < AUDIO_PWM_MIDPOINT) {
            s_output_level += 2U;
            if (s_output_level > AUDIO_PWM_MIDPOINT) {
                s_output_level = AUDIO_PWM_MIDPOINT;
            }
        }
    } else if (s_output_level > 0U) {
        s_output_level = (s_output_level > 2U) ? (s_output_level - 2U) : 0U;
    }

    if (s_output_level == 0U) {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0U);
        return;
    }
    if (s_output_level < AUDIO_PWM_MIDPOINT) {
        sample = (sample * (int32_t)s_output_level) / (int32_t)AUDIO_PWM_MIDPOINT;
    }
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, (uint32_t)sample);
}
