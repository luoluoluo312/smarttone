/* ============================================================================
 *  audio_pitch.h   —— 音高换算（纯计算，不依赖 HAL）
 *
 *  模块：02_audio_pitch
 *  说明：负责"音符 + 音区 + 移调 -> 实际频率 -> 定时器 ARR"这条链路。
 *        不含任何硬件访问，可以单独做单元测试。
 * ========================================================================== */

#ifndef AUDIO_PITCH_H
#define AUDIO_PITCH_H

#include "audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- 计数时基
 *
 * 定时器每秒计数的次数，由 audio_config.h 的预分频决定。
 * 放在这里是因为 ARR 的换算依赖它，而本模块不包含 HAL 头文件。
 */
#define AUDIO_TICK_HZ           1000000UL

/* -------------------------------------------------------------- 基础频率
 *
 * 按竞赛官方对照表取整，三个音区严格相差一倍。
 *   低音区 131 147 165 175 196 220 247
 *   中音区 262 294 330 349 392 440 494
 *   高音区 523 587 659 698 784 880 988
 *
 * 参数非法时返回 0。
 */
uint16_t AudioPitch_GetBaseFreq(uint8_t note, uint8_t octave);

/* ------------------------------------------------------------------ 移调
 *
 * 按十二平均律，每半音乘 2^(1/12)。
 * semitone 为负时做除法，正时做乘法，均为定点整数运算。
 * 注意：移调不是重新查七音表，七音表里没有半音。
 */
uint16_t AudioPitch_ApplyTranspose(uint16_t base_hz, int8_t semitone);

/* -------------------------------------------------------- 一步到位的查询
 *
 * 返回 0 表示参数非法。
 */
uint16_t AudioPitch_GetFreq(uint8_t note, uint8_t octave, int8_t semitone);

/* ------------------------------------------------------------ 频率 -> ARR
 *
 * ARR = round(AUDIO_TICK_HZ / f) - 1
 * 返回 0 表示频率非法或结果超出 16 位范围。
 */
uint32_t AudioPitch_FreqToArr(uint16_t freq_hz);

/* ------------------------------------------------------------ 合法性检查 */
bool AudioPitch_IsValidNote(uint8_t note, uint8_t octave);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PITCH_H */
