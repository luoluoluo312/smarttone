/* ============================================================================
 *  audio_pitch.c   —— 音高换算实现
 *
 *  模块：02_audio_pitch
 *  说明：纯整数运算，无浮点、无 HAL 依赖，可离线验证。
 * ========================================================================== */

#include "audio_pitch.h"

/* ------------------------------------------------------------ 官方频率表 */
static const uint16_t s_note_hz[OCT_COUNT][NOTE_COUNT] = {
    /* 低音区 */ { 131U, 147U, 165U, 175U, 196U, 220U, 247U },
    /* 中音区 */ { 262U, 294U, 330U, 349U, 392U, 440U, 494U },
    /* 高音区 */ { 523U, 587U, 659U, 698U, 784U, 880U, 988U }
};

/* 2^(1/12) = 1.0594631 的 Q16 定点表示：1.0594631 * 65536 = 69433 */
#define AUDIO_SEMITONE_Q16      69433UL

/* --------------------------------------------------------------- 基础频率 */
uint16_t AudioPitch_GetBaseFreq(uint8_t note, uint8_t octave)
{
    if ((note >= NOTE_COUNT) || (octave >= OCT_COUNT)) {
        return 0U;
    }
    return s_note_hz[octave][note];
}

/* ------------------------------------------------------------------- 移调 */
uint16_t AudioPitch_ApplyTranspose(uint16_t base_hz, int8_t semitone)
{
    uint32_t v = (uint32_t)base_hz;

    if (v == 0UL) {
        return 0U;
    }

    /* 升调：乘以 2^(1/12)，逐半音累乘，最多两次 */
    while (semitone > 0) {
        v = ((v * AUDIO_SEMITONE_Q16) + 32768UL) >> 16;   /* +0.5 做四舍五入 */
        semitone--;
    }

    /* 降调：除以 2^(1/12)，逐半音累除 */
    while (semitone < 0) {
        v = ((v << 16) + (AUDIO_SEMITONE_Q16 / 2UL)) / AUDIO_SEMITONE_Q16;
        semitone++;
    }

    if (v > 65535UL) {
        v = 65535UL;
    }
    return (uint16_t)v;
}

/* ----------------------------------------------------------- 一步到位查询 */
uint16_t AudioPitch_GetFreq(uint8_t note, uint8_t octave, int8_t semitone)
{
    uint16_t base = AudioPitch_GetBaseFreq(note, octave);

    if (base == 0U) {
        return 0U;
    }
    if ((semitone < AUDIO_TRANSPOSE_MIN) || (semitone > AUDIO_TRANSPOSE_MAX)) {
        return 0U;
    }
    return AudioPitch_ApplyTranspose(base, semitone);
}

/* ------------------------------------------------------------- 频率 -> ARR
 *
 * 例：f = 440Hz -> ticks = 1000000/440 = 2272.7 -> 2273 -> ARR = 2272
 *     实际输出 = 1000000 / 2273 = 440.0Hz
 */
uint32_t AudioPitch_FreqToArr(uint16_t freq_hz)
{
    uint32_t f = (uint32_t)freq_hz;
    uint32_t ticks;

    if (f == 0UL) {
        return 0UL;
    }

    /* 四舍五入取整，减少音准误差 */
    ticks = (AUDIO_TICK_HZ + (f / 2UL)) / f;

    if (ticks < 2UL) {
        return 0UL;                 /* 频率过高，无法产生 PWM */
    }
    if (ticks > 65536UL) {
        return 0UL;                 /* 超出 16 位 ARR 范围 */
    }
    return ticks - 1UL;
}

/* ------------------------------------------------------------- 合法性检查 */
bool AudioPitch_IsValidNote(uint8_t note, uint8_t octave)
{
    return (bool)((note < NOTE_COUNT) && (octave < OCT_COUNT));
}
