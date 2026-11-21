/* ============================================================================
 *  audio.h   —— 音频模块唯一对外头文件
 *
 *  模块：04_audio_core
 *  说明：H 侧只需要包含本文件，即可使用发声、参数设置、节拍器、曲目播放
 *        的全部能力。音高计算、PWM 驱动、声部分配都是内部实现，
 *        H 不需要也不应该直接包含它们。
 * ========================================================================== */

#ifndef AUDIO_H
#define AUDIO_H

#include "audio_types.h"
#include "metronome.h"
#include "song.h"
#include "drum.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- 初始化
 *
 * 调用时机：AudioTim_Init 之后、主循环之前。
 * 内部启动 PA0 混音输出；PB1 节拍器独立使用 TIM3。
 */
void Audio_Init(void);

/* ---------------------------------------------------------------- 弹奏
 *
 * Audio_NoteOn 使用当前的音区、移调、音量发声。
 *   返回 >= 0 : 成功，返回发声标识，松开时必须凭它停止
 *   返回 <  0 : 失败（最常见是 AUDIO_ERR_NO_VOICE，两个声部都占用）
 *
 * 失败时不发声，也不要重试。上层按"没响"处理即可。
 */
VoiceId Audio_NoteOn(uint8_t note);

/* 停止指定发声。参数是 Audio_NoteOn 返回的那个标识，不是音符号。
 * 对已停止的标识重复调用是安全的（幂等）。 */
AudioStatus Audio_NoteOff(VoiceId id);

/* ------------------------------------------------------------ 指定频率发声
 *
 * 回放专用。直接给频率，不经过当前音区与移调。
 * out_id 可以为 NULL。
 */
AudioStatus Audio_NoteOnPitch(uint16_t freq_hz, VoiceId *out_id);

/* ---------------------------------------------------------------- 全局操作 */
void    Audio_AllOff(void);            /* 立即停止所有发声 */
uint8_t Audio_GetActiveCount(void);    /* 正在发声的声部数，0~2 */
uint8_t Audio_GetKeyboardCount(void);  /* 其中属于键盘演奏的声部数，节拍器用它决定要不要让位 */
bool    Audio_IsVoiceActive(VoiceId id);

/* ------------------------------------------------------------ 三个演奏参数
 *
 * 这三个值由音频模块持有，界面显示请用 Get 读取，不要另存一份。
 */
void     Audio_SetOctave(OctaveId oct);
OctaveId Audio_GetOctave(void);

/* 全局音量 1~5 档，对喇叭上的所有发声（演奏、曲目/录音回放）立即生效。
 * 节拍器 PB1 是独立蜂鸣器，不跟随这个值。 */
void     Audio_SetVolume(VolumeId vol);
VolumeId Audio_GetVolume(void);

AudioStatus Audio_SetTranspose(int8_t semitone);   /* -2 ~ +2 */
int8_t      Audio_GetTranspose(void);

/* ------------------------------------------------------------ 频率查询
 *
 * 界面若要显示"当前音符对应多少 Hz"，用这两个接口，不要自己算。
 */
uint16_t Audio_GetFreqOfNote(uint8_t note, OctaveId octave, int8_t transpose);
uint16_t Audio_GetCurrentFreq(uint8_t note);   /* 按当前音区与移调 */

/* ---------------------------------------------------- 某个声部的实际频率
 *
 * 录音时用它取"按下那一刻的真实频率"，直接填进 NoteEvent.freq_hz。
 * 标识无效或已停止时返回 0。
 *
 * 不要用 Audio_GetCurrentFreq() 反推：中途改过音区后，
 * 反推出来的频率和当时听到的不是同一个。
 */
uint16_t Audio_GetVoiceFreq(VoiceId id);

/* ------------------------------------------------------ 带优先级的发声
 *
 * 回放三轨时使用。声部不够时，本次请求的优先级若高于某个正在发声的声部，
 * 就会抢占它；抢不到则返回 AUDIO_ERR_NO_VOICE。
 *
 * priority 越小越重要，键盘是 0，永远不会被抢占。
 */
AudioStatus Audio_NoteOnPitchEx(uint16_t freq_hz, uint8_t priority, VoiceId *out_id);

/* --------------------------------------------------- 固定音量的提示音发声
 *
 * 开机琶音、菜单/确认提示音使用。permille 由调用者给定（1..500），
 * 之后即使 Audio_SetVolume 改变也不会影响这个声部。
 */
AudioStatus Audio_NoteOnPitchGain(uint16_t freq_hz, uint16_t permille,
                                  uint8_t priority, VoiceId *out_id);

/* ---------------------------------------------------- 改变已发声的音高
 *
 * 不重新分配声部，只改这一路的频率。鼓组的频率轨迹、以后的滑音和颤音
 * 都靠它。标识失效（已被停止或抢占）时返回 AUDIO_ERR_BAD_PARAM。
 */
AudioStatus Audio_Retune(VoiceId id, uint16_t freq_hz);

/* ---------------------------------------------------------------- 音色
 *
 * 1 管风琴（方波，恒定）、2 钢琴（亮/暗波表 + 分段衰减）、
 * 3 失真电吉他（削波锯齿波 + 拨弦亮起音 + 30% 延音）。详见 audio_types.h 的说明。
 */
void     Audio_SetTimbre(TimbreId timbre);
TimbreId Audio_GetTimbre(void);

/* ------------------------------------------------------------ 包络处理
 *
 * 主循环里周期调用（建议每轮或每 10ms 一次）。
 * 内部按 HAL_GetTick 计算三段包络和钢琴的亮/暗过渡，不做延时。
 * 若当前音色是管风琴（恒定），本函数直接返回。
 */
void Audio_Envelope_Tick(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
