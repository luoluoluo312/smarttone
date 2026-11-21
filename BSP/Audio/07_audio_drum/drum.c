/* ============================================================================
 *  drum.c   —— 打击乐实现
 *
 *  模块：07_audio_drum
 *  说明：每个鼓件是一张"频率轨迹表"，每格代表一小段时长内的音高。
 *        播放时按毫秒推进，逐格改变同一路 PWM 的频率，不重新申请声部。
 * ========================================================================== */

#include "drum.h"
#include "audio.h"
#include "stm32f1xx_hal.h"

/* ------------------------------------------------------------ 轨迹表结构 */
typedef struct {
    uint16_t freq_hz;   /* 该段要发的频率 */
    uint16_t dur_ms;    /* 该段持续时长 */
} DrumStep;

typedef struct {
    const char     *name;
    const DrumStep *steps;
    uint8_t         count;
} DrumDef;

/* ---------------------------------------------------------------- 低音鼓
 * 从 160Hz 快速降到 100Hz，模拟鼓皮张力释放后的音高下滑。
 * 蜂鸣器低频响应弱，实际听到的是"咚"的冲击感而不是低音。
 */
static const DrumStep s_kick[] = {
    { 160U, 4U },
    { 130U, 4U },
    { 110U, 6U },
    { 100U, 20U }
};

/* ---------------------------------------------------------------- 小军鼓
 * 两个频率快速交替，制造出宽带的"沙沙"噪音成分。
 */
static const DrumStep s_snare[] = {
    { 700U,  4U },
    { 1300U, 4U },
    { 700U,  4U },
    { 1300U, 4U },
    { 900U,  8U }
};

/* ------------------------------------------------------------ 踩镲（闭合）
 * 3000Hz 接近蜂鸣器 2.7kHz 的谐振点，短促一下最像金属片。
 */
static const DrumStep s_hat_closed[] = {
    { 3000U, 18U }
};

/* ------------------------------------------------------------ 踩镲（打开） */
static const DrumStep s_hat_open[] = {
    { 3000U, 30U },
    { 2800U, 30U }
};

/* ---------------------------------------------------------------- 通鼓
 * 连续下滑，比低音鼓慢、音高更高，形成"嗵"的滑动感。
 */
static const DrumStep s_tom[] = {
    { 450U, 4U },
    { 410U, 4U },
    { 370U, 4U },
    { 330U, 8U },
    { 300U, 12U }
};

/* ---------------------------------------------------------------- 鼓件表 */
static const DrumDef s_drum_defs[DRUM_COUNT] = {
    { "Kick",   s_kick,       (uint8_t)(sizeof(s_kick)       / sizeof(s_kick[0])) },
    { "Snare",  s_snare,      (uint8_t)(sizeof(s_snare)      / sizeof(s_snare[0])) },
    { "HatC",   s_hat_closed, (uint8_t)(sizeof(s_hat_closed) / sizeof(s_hat_closed[0])) },
    { "HatO",   s_hat_open,   (uint8_t)(sizeof(s_hat_open)   / sizeof(s_hat_open[0])) },
    { "Tom",    s_tom,        (uint8_t)(sizeof(s_tom)        / sizeof(s_tom[0])) }
};

/* ---------------------------------------------------------------- 播放状态 */
static struct {
    const DrumDef *def;
    uint8_t        idx;
    uint32_t       t_next;
    VoiceId        vid;
    bool           active;
} s_drum;

/* ------------------------------------------------------------------ 初始化 */
void Drum_Init(void)
{
    s_drum.def    = NULL;
    s_drum.idx    = 0U;
    s_drum.t_next = 0UL;
    s_drum.vid    = AUDIO_VOICE_INVALID;
    s_drum.active = false;
}

/* ------------------------------------------------------------------ 停止 */
void Drum_Stop(void)
{
    if (s_drum.active && (s_drum.vid >= 0)) {
        (void)Audio_NoteOff(s_drum.vid);
    }
    s_drum.vid    = AUDIO_VOICE_INVALID;
    s_drum.active = false;
    s_drum.def    = NULL;
    s_drum.idx    = 0U;
}

/* ------------------------------------------------------------------ 敲击 */
bool Drum_Play(DrumId id)
{
    VoiceId v = AUDIO_VOICE_INVALID;

    if ((uint8_t)id >= (uint8_t)DRUM_COUNT) {
        return false;
    }

    /* 同一时刻只允许一个鼓件，避免两只蜂鸣器都被鼓占住 */
    Drum_Stop();

    /* 最低优先级：声部不够时鼓让位给琴音 */
    if (Audio_NoteOnPitchEx(s_drum_defs[id].steps[0].freq_hz,
                            AUDIO_PRIO_LOWEST, &v) != AUDIO_OK) {
        return false;
    }

    s_drum.def    = &s_drum_defs[id];
    s_drum.idx    = 0U;
    s_drum.vid    = v;
    s_drum.active = true;
    s_drum.t_next = HAL_GetTick() + s_drum_defs[id].steps[0].dur_ms;

    return true;
}

bool Drum_IsPlaying(void)
{
    return s_drum.active;
}

/* -------------------------------------------------------------- 周期推进 */
void Drum_Tick(void)
{
    uint8_t  next;
    uint32_t now;

    if (!s_drum.active || (s_drum.def == NULL)) {
        return;
    }

    if (!Audio_IsVoiceActive(s_drum.vid)) {
        s_drum.vid = AUDIO_VOICE_INVALID;
        s_drum.active = false;
        return;
    }
    now = HAL_GetTick();
    if ((int32_t)(now - s_drum.t_next) < 0) {
        return;
    }

    next = (uint8_t)(s_drum.idx + 1U);

    /* 最后一格放完，收声 */
    if (next >= s_drum.def->count) {
        Drum_Stop();
        return;
    }

    s_drum.idx = next;

    /* 就在同一路 PWM 上改频率，不重新申请声部 */
    if (Audio_Retune(s_drum.vid, s_drum.def->steps[next].freq_hz) != AUDIO_OK) {
        /* 声部已被更高优先级抢占，或标识失效，直接收掉 */
        Drum_Stop();
        return;
    }

    s_drum.t_next = now + s_drum.def->steps[next].dur_ms;
}

/* ------------------------------------------------------------------ 名称 */
const char *Drum_GetName(DrumId id)
{
    if ((uint8_t)id >= (uint8_t)DRUM_COUNT) {
        return "?";
    }
    return s_drum_defs[id].name;
}
