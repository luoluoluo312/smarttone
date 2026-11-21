/* ============================================================================
 *  audio_types.h   —— 公共类型定义（H 与 W 共用）
 *
 *  模块：01_audio_config
 *  说明：本文件只依赖 C 标准库，不依赖 HAL。H 侧代码可以安全包含。
 *        所有跨模块共享的枚举、结构体、返回码都集中在这里，
 *        双方必须使用同一份定义，不得各自拷贝。
 * ========================================================================== */

#ifndef AUDIO_TYPES_H
#define AUDIO_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>     /* NULL */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- 容量常量
 *
 * 用 #ifndef 保护：H 侧的 bsp_notes.c 也定义了同名宏，避免重定义警告。
 */
#ifndef AUDIO_VOICE_MAX
#define AUDIO_VOICE_MAX     2       /* 两个声部；三声部曾导致声部卡死常响，待单独验证后再开 */
#endif
#ifndef NOTE_COUNT
#define NOTE_COUNT          7       /* Do ~ Si */
#endif
#ifndef OCT_COUNT
#define OCT_COUNT           3       /* 低 / 中 / 高 */
#endif
#ifndef VOL_COUNT
#define VOL_COUNT           5       /* 音量档位数（1~5 档） */
#endif

/* ------------------------------------------------------------------ 音符 */
typedef enum {
    NOTE_DO  = 0,
    NOTE_RE  = 1,
    NOTE_MI  = 2,
    NOTE_FA  = 3,
    NOTE_SOL = 4,
    NOTE_LA  = 5,
    NOTE_SI  = 6
} NoteId;

/* ------------------------------------------------------------------ 音区 */
typedef enum {
    OCT_LOW  = 0,   /* 131 ~ 247 Hz */
    OCT_MID  = 1,   /* 262 ~ 494 Hz  上电默认 */
    OCT_HIGH = 2    /* 523 ~ 988 Hz */
} OctaveId;

/* ------------------------------------------------------------------ 音量
 *
 * 1~5 档，全局生效；界面固定显示 1~5，音频模块内部换算成占空比。
 * 只有喇叭声部（演奏、曲目/录音回放）跟随这个值，节拍器 PB1 不跟随。
 */
typedef enum {
    VOL_1 = 0,
    VOL_2 = 1,
    VOL_3 = 2,
    VOL_4 = 3,
    VOL_5 = 4
} VolumeId;

/* -------------------------------------------------------------- 发声标识
 *
 * 按下时返回，松开时凭它停止。高 6 位是代数、最低位是声部号。
 * 代数每分配一次自增，因此同一个声部号被复用后，旧标识不会被误认。
 * 对上层来说是黑盒，只需要原样保存、原样传回。
 */
typedef int8_t VoiceId;
#define AUDIO_VOICE_INVALID   ((VoiceId)-1)

/* ---------------------------------------------------------------- 返回码 */
typedef enum {
    AUDIO_OK           =  0,
    AUDIO_ERR_NO_VOICE = -1,   /* 两个声部都在使用中 */
    AUDIO_ERR_BAD_NOTE = -2,   /* 音符或音区越界 */
    AUDIO_ERR_BAD_PARAM= -3    /* 其他参数非法 / 未初始化 */
} AudioStatus;

/* -------------------------------------------------------------- 录音事件
 *
 * 注意：NoteEvent 的唯一定义在 H 侧的 BSP/Inc/record.h，本文件不再重复定义。
 * 录音是 H 的领域，音频模块只提供频率（Audio_GetVoiceFreq），不使用该结构体。
 *
 * 双方的约定（record.h 中已有 12 字节静态断言，改坏会编译失败）：
 *   note u8 | octave u8 | freq_hz u16 | start_ms u32 | dur_ms u16  = 12 字节
 */

/* -------------------------------------------------------------- 移调范围 */
#define AUDIO_TRANSPOSE_MIN   (-2)
#define AUDIO_TRANSPOSE_MAX   ( 2)

/* -------------------------------------------------------------- 音色
 *
 * 三个音色由"波形表 + 分段包络"区分，不再只是包络差异：
 *   ORGAN  25% 窄脉冲，恒定音量，像管风琴持续音
 *   PIANO  亮波表起音、0.4 s 内过渡到暗波表，再衰减
 *   GUITAR 失真削波锯齿波，亮起音 → 箱体延音，30% 持续
 *
 * 具体参数见 audio_config.h 与 04_audio_core/audio.c 的音色表。
 */
#define TIMBRE_COUNT 5

typedef enum {
    TIMBRE_PIANO     = 0,   /* 1 钢琴 */
TIMBRE_GUITAR    = 1,   /* 2 吉他 */
TIMBRE_ACCORDION = 2,   /* 3 手风琴 */
TIMBRE_BASS      = 3,   /* 4 电贝斯 */
TIMBRE_MUSICBOX  = 4,   /* 5 八音盒 */
TIMBRE_ORGAN     = 2    /* 兼容旧名：上电默认 = 手风琴 */
} TimbreId;

/* ------------------------------------------------------------ 声部优先级
 *
 * 数值越小优先级越高。声部不够时，高优先级可以抢占低优先级的声部，
 * 但永远抢不到键盘（键盘是 0）。
 *
 * 建议用法：
 *   键盘演奏   0
 *   轨道 1 旋律 1
 *   轨道 2     2
 *   轨道 3 / 节拍器 3
 */
#define AUDIO_PRIO_KEYBOARD   0U
#define AUDIO_PRIO_MELODY     1U
#define AUDIO_PRIO_TRACK2     2U
#define AUDIO_PRIO_LOWEST     3U
#define AUDIO_PRIO_DEFAULT    AUDIO_PRIO_MELODY

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_TYPES_H */
