/* ============================================================================
 *  metronome.c   —— 节拍器实现
 *
 *  模块：05_audio_metronome
 *  说明：非阻塞设计。Metronome_Tick 每次只做时间比较，不做延时。
 * ========================================================================== */

#include "metronome.h"
#include "audio.h"
#include "audio_config.h"
#include "bsp_audio_tim.h"   /* PB1: passive, active-low buzzer */

/* Short clicks on the dedicated PB1 timer output. */
#define METRONOME_CLICK_MS    12U
#define METRONOME_ACCENT_MS   30U

/* ---------------------------------------------------------------- 内部状态 */
static struct {
    bool     running;
    uint16_t bpm;
    uint8_t  beats_per_bar; /* 每小节拍数，2~6 */
    uint8_t  beat;          /* 当前小节内的拍号，0 为强拍 */
    uint8_t  volume;        /* PB1 音量 1~3 */
    uint32_t next_ms;       /* 下一拍的绝对时刻 */
    uint32_t click_end_ms;  /* 这一拍应结束的时刻 */
    bool     click_on;
} s_metro;

#if AUDIO_METRO_VIA_SPEAKER
/* 扬声器侧的节拍声：优先级最低，弹琴时会被自动抢占让位。 */
static VoiceId s_speaker_voice;
static bool    s_speaker_on;
#endif

/* ------------------------------------------------------------ BPM -> 间隔 */
static uint32_t metronome_interval_ms(uint16_t bpm)
{
    if (bpm == 0U) {
        bpm = AUDIO_METRO_BPM_DEFAULT;
    }
    return 60000UL / (uint32_t)bpm;
}

/* ------------------------------------------------------------ 停止提示音 */
static void metronome_click_off(void)
{
    if (s_metro.click_on) {
#if AUDIO_METRO_VIA_SPEAKER
    if (s_speaker_on) {
        (void)Audio_NoteOff(s_speaker_voice);
        s_speaker_on = false;
    }
#endif
        AudioMetroBeep_Stop();
        s_metro.click_on = false;
    }
}

/* ------------------------------------------------------------------ 初始化 */
void Metronome_Init(void)
{
    /* PB1/TIM3_CH4 is owned by the metronome. */
    AudioMetroBeep_Init();

    s_metro.running      = false;
    s_metro.bpm          = AUDIO_METRO_BPM_DEFAULT;
    s_metro.beats_per_bar = AUDIO_METRO_BEATS_PER_BAR;
    s_metro.beat         = 0U;
    s_metro.volume       = METRONOME_VOLUME_DEFAULT;
    s_metro.next_ms      = 0UL;
    s_metro.click_on     = false;
    s_metro.click_end_ms = 0UL;
    AudioMetroBeep_SetVolume(s_metro.volume);
}

/* ------------------------------------------------------------------ BPM */
bool Metronome_SetBpm(uint16_t bpm)
{
    if ((bpm < AUDIO_METRO_BPM_MIN) || (bpm > AUDIO_METRO_BPM_MAX)) {
        return false;
    }
    s_metro.bpm = bpm;
    return true;
}

uint16_t Metronome_GetBpm(void)
{
    return s_metro.bpm;
}

bool Metronome_SetBeatsPerBar(uint8_t beats)
{
    if ((beats < 2U) || (beats > 6U)) {
        return false;
    }
    s_metro.beats_per_bar = beats;
    /* 改拍号后重音位置会变，把拍序归零，下一拍就是强拍 */
    s_metro.beat = 0U;
    return true;
}

uint8_t Metronome_GetBeatsPerBar(void)
{
    return s_metro.beats_per_bar;
}

bool Metronome_SetVolume(uint8_t level)
{
    if ((level < METRONOME_VOLUME_MIN) || (level > METRONOME_VOLUME_MAX)) {
        return false;
    }
    s_metro.volume = level;
    AudioMetroBeep_SetVolume(level);
    return true;
}

uint8_t Metronome_GetVolume(void)
{
    return s_metro.volume;
}

/* -------------------------------------------------------------- 启动停止 */
void Metronome_Start(void)
{
    if (s_metro.running) {
        return;
    }
    metronome_click_off();

    s_metro.beat    = 0U;
    s_metro.running = true;
    s_metro.next_ms = HAL_GetTick();     /* 立即敲第一下 */
}

void Metronome_Stop(void)
{
    s_metro.running = false;
    metronome_click_off();
}

bool Metronome_IsRunning(void)
{
    return s_metro.running;
}

/* ------------------------------------------------------------ 周期处理 */
void Metronome_Tick(void)
{
    uint32_t now = HAL_GetTick();

    /* 1) 先处理上一声提示音的结束 */
    if (s_metro.click_on && ((int32_t)(now - s_metro.click_end_ms) >= 0)) {
        metronome_click_off();
    }

    if (!s_metro.running) {
        return;
    }

    /* 2) 判断是否到了下一拍 */
    if ((int32_t)(now - s_metro.next_ms) < 0) {
        return;
    }

    s_metro.next_ms = now + metronome_interval_ms(s_metro.bpm);

    /* 3) 打一拍。
     *    这只蜂鸣器是独立的，不占 Audio 的声部，所以不需要给琴音让位，
     *    演奏和打拍可以同时进行。重音（每小节第一拍）靠响得更久来区分。 */
    {
        uint32_t duration = (s_metro.beat == 0U) ? METRONOME_ACCENT_MS
                                                 : METRONOME_CLICK_MS;

        s_metro.beat = (uint8_t)((s_metro.beat + 1U) % s_metro.beats_per_bar);

        metronome_click_off();
           uint16_t click_hz = (s_metro.beat == 1U) ? AUDIO_METRO_HZ_ACCENT
                                                    : AUDIO_METRO_HZ_NORMAL;
           AudioMetroBeep_Play(click_hz, duration);
#if AUDIO_METRO_VIA_SPEAKER
           s_speaker_on = true;
           (void)Audio_NoteOnPitchEx(click_hz, AUDIO_PRIO_LOWEST, &s_speaker_voice);
#endif
        s_metro.click_on     = true;
        s_metro.click_end_ms = now + duration;
    }
}
