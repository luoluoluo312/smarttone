/* ============================================================================
 *  audio_config.h   —— 硬件映射与可调参数（W 内部使用）
 *
 *  模块：01_audio_config
 *  说明：所有引脚、定时器通道、驱动极性、音量档位、频率范围都集中在这里。
 *        换板子或换蜂鸣器模块时，只改本文件，不动其他任何 .c。
 * ========================================================================== */

#ifndef AUDIO_CONFIG_H
#define AUDIO_CONFIG_H

#include "stm32f1xx_hal.h"
#include "audio_types.h"
#include "audio_pitch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PA0 -> 1k/10nF RC -> 4.7uF -> HXJ8002 -> one 8-ohm speaker.
 * PB1/TIM3_CH4 is exclusively the passive metronome buzzer (active low).
 * Both APB1 timers run at 72 MHz with the current clock tree. */
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
#define AUDIO_TIMER_CLOCK_HZ    72000000UL
#define AUDIO_PRESCALER         (AUDIO_TIMER_CLOCK_HZ / AUDIO_TICK_HZ - 1U)
#define AUDIO_ARR_DEFAULT       999U
#define AUDIO_SAMPLE_HZ         32000UL
#define AUDIO_PWM_HZ            281250UL
#define AUDIO_PWM_PERIOD        (AUDIO_TIMER_CLOCK_HZ / AUDIO_PWM_HZ)
#define AUDIO_PWM_MIDPOINT      (AUDIO_PWM_PERIOD / 2U)
/* Per-voice peak in PWM counts. 127 is the largest safe single-voice swing
 * around the 128-count midpoint; two voices are normalized in the IRQ. */
#define AUDIO_SPEAKER_PEAK      127U
_Static_assert(AUDIO_SPEAKER_PEAK < AUDIO_PWM_MIDPOINT,
               "Single-voice peak exceeds PWM range");

/* ------------------------------------------------------------ 音量档位
 *
 * 保留原接口的 0..500 参数范围，底层按比例转换为软件声部振幅。
 * 500 对应 AUDIO_SPEAKER_PEAK；不再改变载波的频率或音频脉宽。
 *
 * 每档约增加 3 dB，约为满幅的 25% / 35% / 50% / 71% / 100%。
 * 第 5 档保持原最高输出；双声部仍按总振幅回缩，避免叠加削顶。
 */
#define AUDIO_DUTY_PERMILLE_1       125U
#define AUDIO_DUTY_PERMILLE_2       177U
#define AUDIO_DUTY_PERMILLE_3       250U
#define AUDIO_DUTY_PERMILLE_4       354U
#define AUDIO_DUTY_PERMILLE_5       500U

/* 上电默认音量：中间档 */
#define AUDIO_VOLUME_DEFAULT        VOL_3

/* 开机/菜单提示音固定 300‰，不跟随演奏音量档位。
 * 300‰ 在 127 峰值下约为 76 个计数，避免提示音比演奏还刺耳。 */
#define AUDIO_SFX_PERMILLE          300U

/* 节拍器 PB1 蜂鸣器音量：1=轻 2=中 3=响；默认保持原来的 50% 占空比（第 3 档）。
 * 只调占空比，不改频率/ARR，也不占 Audio 声部。 */
/* 节拍器是否同时从 PA0 扬声器发声：PB1 串了 1k 限流电阻，蜂鸣器本身很轻，\n * 走功放才有足够音量。1=扬声器+蜂鸣器一起响，0=只用蜂鸣器。 */
#define AUDIO_METRO_VIA_SPEAKER     0

#define AUDIO_METRO_VOLUME_MIN      1U
#define AUDIO_METRO_VOLUME_MAX      3U
#define AUDIO_METRO_VOLUME_DEFAULT  3U

/* ------------------------------------------------------------ 频率范围
 *
 * 三个音区乘以最大移调后的实际范围约为 116 ~ 1109 Hz。
 * 伴奏允许到C2附近（65Hz），覆盖谱中的D#2/E2/F#2低音；超出范围仍拒绝。
 */
#define AUDIO_FREQ_MIN_HZ           65U
#define AUDIO_FREQ_MAX_HZ           4000U

/* ------------------------------------------------------------ 节拍器 */
#define AUDIO_METRO_BPM_DEFAULT     100U
#define AUDIO_METRO_BPM_MIN         40U
#define AUDIO_METRO_BPM_MAX         240U
#define AUDIO_METRO_CLICK_MS        55U     /* 每下提示音的长度 */
#define AUDIO_METRO_HZ_NORMAL       3000U   /* 弱拍频率 */
#define AUDIO_METRO_HZ_ACCENT       4000U   /* 每小节第一拍 */
#define AUDIO_METRO_BEATS_PER_BAR   4U

/* ------------------------------------------------------------ 音色
 *
 * 三个音色：1 管风琴、2 钢琴、3 失真电吉他。
 *
 * 包络分三段，全部是相对 AUDIO_ENV_MAX_LEVEL 的比例：
 *   起音 attack_ms：0 -> 1000
 *   第一段 decay1_ms：1000 -> level1
 *   第二段 decay2_ms：level1 -> floor
 * 之后保持 floor（0 表示衰减到静音）。
 *
 * bright_ms 只对钢琴有效：从"亮波表"线性过渡到"暗波表"的时间，
 * 模拟钢琴高次泛音先衰减；0 表示不混合，直接用单一波表。
 *
 * 波形表本身在 03_audio_pwm/audio_pwm.c。
 */
#define AUDIO_ENV_MAX_LEVEL         1000U

/* 1 管风琴：25% 窄脉冲，音量恒定；key_vibrato 只给管风琴加 ±2.5% 颤音 */
#define AUDIO_ORGAN_ATTACK_MS       0U
#define AUDIO_ORGAN_DECAY1_MS       0U
#define AUDIO_ORGAN_LEVEL1          1000U
#define AUDIO_ORGAN_DECAY2_MS       0U
#define AUDIO_ORGAN_FLOOR           1000U
#define AUDIO_ORGAN_BRIGHT_MS       0U

/* 2 钢琴：亮波表起音，0.4 s 过渡到暗波表，先快速掉到 35%，再长尾衰减到 0。
 * 比原来更“拨弦”：起音后 180 ms 就明显变暗变轻，和持续音/短铃分开。 */
#define AUDIO_PIANO_ATTACK_MS       2U
#define AUDIO_PIANO_DECAY1_MS       180U
#define AUDIO_PIANO_LEVEL1          350U
#define AUDIO_PIANO_DECAY2_MS       2000U
#define AUDIO_PIANO_FLOOR           0U
#define AUDIO_PIANO_BRIGHT_MS       400U

/* 3 失真电吉他：削波锯齿波（亮/暗两张表），快速起音后掉到 62%，
 * 再落到 30% 持续；按住有失真延音，短按也有明显的拨弦声。 */
#define AUDIO_GUITAR_ATTACK_MS      2U
#define AUDIO_GUITAR_DECAY1_MS      160U
#define AUDIO_GUITAR_LEVEL1         620U
#define AUDIO_GUITAR_DECAY2_MS      900U
#define AUDIO_GUITAR_FLOOR          300U
#define AUDIO_GUITAR_BRIGHT_MS      130U

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_CONFIG_H */
