/* ============================================================================
 *  audio.c   —— 声部分配 + 音频门面
 *
 *  模块：04_audio_core
 *  说明：本文件是 H 与 W 的唯一边界。
 *        对外提供 audio.h 里的接口，对内调用 audio_pwm / audio_pitch。
 *
 *  本版新增：
 *    1) 声部优先级与抢占（支持三轨回放时保旋律）
 *    2) 音色包络（管风琴 / 钢琴 / 铃铛，靠音量衰减曲线区分）
 * ========================================================================== */

#include "audio.h"
#include "audio_config.h"
#include "audio_pitch.h"
#include "audio_pwm.h"
#include "drum.h"

/* ---------------------------------------------------------------- 声部槽位
 *
 * gen        : 代数计数器，每分配一次自增，用来区分同一槽位的不同次占用
 * id         : 最近一次发出去的标识；与传入标识不一致说明该标识已过期
 * prio       : 占用者的优先级，数值越小越重要；0 是键盘，永不被抢占
 * t_start_ms : 本次发声的起点，包络按它计算已经过了多久
 * env_lv     : 上一次算出的包络电平，用来避免重复写寄存器
 */
typedef struct {
    bool     active;
    uint8_t  note;
    uint8_t  octave;
    uint8_t  gen;
    uint8_t  prio;
    VoiceId  id;
    uint16_t freq_hz;
    uint16_t env_lv;
    uint16_t gain;          /* 本声部固定增益（‰）；0 = 跟随全局音量 */
    uint8_t  wave_mix;      /* 钢琴：0 = 亮波表，255 = 暗波表 */
    uint32_t t_start_ms;
} audio_voice_slot_t;

/* ---------------------------------------------------------------- 模块状态 */
static audio_voice_slot_t s_slot[AUDIO_VOICE_MAX];
static OctaveId           s_octave;
static VolumeId           s_volume;
static TimbreId           s_timbre;
static int8_t             s_transpose;
static bool               s_inited = false;

/* ------------------------------------------------------------ 标识编解码
 *
 * 标识 = (代数 << 1) | 槽位，取值 0~127，落在 int8_t 正数范围。
 * 代数 6 位，同一个槽位连续分配 64 次后才会回绕。
 */
#define AUDIO_ID_GEN_MASK    0x3FU

static VoiceId audio_make_id(uint8_t slot, uint8_t gen)
{
    return (VoiceId)((uint8_t)((gen & AUDIO_ID_GEN_MASK) << 1) | (slot & 0x01U));
}

static uint8_t audio_id_slot(VoiceId id)
{
    return (uint8_t)(((uint8_t)id) & 0x01U);
}

/* ------------------------------------------------------------ 音量 -> 占空比 */
static uint16_t audio_duty_of(VolumeId vol)
{
    switch (vol) {
    case VOL_1:
        return AUDIO_DUTY_PERMILLE_1;
    case VOL_2:
        return AUDIO_DUTY_PERMILLE_2;
    case VOL_3:
        return AUDIO_DUTY_PERMILLE_3;
    case VOL_4:
        return AUDIO_DUTY_PERMILLE_4;
    case VOL_5:
    default:
        return AUDIO_DUTY_PERMILLE_5;
    }
}

/* ------------------------------------------------------------ 音色定义
 *
 * 每个音色 = 波形 + 三段包络。bright_ms 对钢琴/吉他的双波表有效：
 * 在这段时间内把亮波表渐变到暗波表，模拟拨弦高次泛音先衰减。
 */
typedef struct {
    uint16_t attack_ms;
    uint16_t decay1_ms;
    uint16_t level1;
    uint16_t decay2_ms;
    uint16_t floor_lv;
    uint16_t bright_ms;
    uint8_t  wave;
} audio_timbre_t;

static const audio_timbre_t s_timbre_def[TIMBRE_COUNT] = {
    {   /* 1 钢琴：亮起音 -> 暗尾音，再缓慢衰减 */
        AUDIO_PIANO_ATTACK_MS, AUDIO_PIANO_DECAY1_MS, AUDIO_PIANO_LEVEL1,
        AUDIO_PIANO_DECAY2_MS, AUDIO_PIANO_FLOOR, AUDIO_PIANO_BRIGHT_MS,
        (uint8_t)AUDIO_WAVE_PIANO
    },
    {   /* 2 吉他：失真锯齿，亮拨弦 -> 箱体延音 */
        AUDIO_GUITAR_ATTACK_MS, AUDIO_GUITAR_DECAY1_MS, AUDIO_GUITAR_LEVEL1,
        AUDIO_GUITAR_DECAY2_MS, AUDIO_GUITAR_FLOOR, AUDIO_GUITAR_BRIGHT_MS,
        (uint8_t)AUDIO_WAVE_GUITAR
    },
    {   /* 3 手风琴：簧片持续音，慢起音、音量恒定，泛音丰富 */
        AUDIO_ORGAN_ATTACK_MS, AUDIO_ORGAN_DECAY1_MS, AUDIO_ORGAN_LEVEL1,
        AUDIO_ORGAN_DECAY2_MS, AUDIO_ORGAN_FLOOR, AUDIO_ORGAN_BRIGHT_MS,
        (uint8_t)AUDIO_WAVE_ORGAN
    },
    {   /* 4 电贝斯：低厚，快速起音后稳定，长尾 */
        8U, 220U, 800U, 1400U, 350U, 0U,
        (uint8_t)AUDIO_WAVE_BASS
    },
    {   /* 5 八音盒：极短起音 + 明亮铃音快速衰减 */
        0U, 260U, 300U, 900U, 0U, 0U,
        (uint8_t)AUDIO_WAVE_BELL
    }
};

static const audio_timbre_t *audio_timbre_now(void)
{
    return &s_timbre_def[(uint8_t)s_timbre];
}

/* ------------------------------------------------------- 按已过时间算电平
 *
 * 三段分段线性：attack 0->1000，decay1 1000->level1，decay2 level1->floor。
 * 用经过时间计算而不是"每次减一点"，主循环被屏幕刷新阻塞也不会走样。
 */
static uint16_t audio_env_level_for(uint32_t elapsed_ms, const audio_timbre_t *t)
{
    uint32_t used;

    if (t->attack_ms != 0U) {
        if (elapsed_ms < t->attack_ms) {
            return (uint16_t)((uint32_t)AUDIO_ENV_MAX_LEVEL * elapsed_ms / t->attack_ms);
        }
        elapsed_ms -= t->attack_ms;
    }

    if (t->decay1_ms != 0U) {
        if (elapsed_ms < t->decay1_ms) {
            used = (uint32_t)(AUDIO_ENV_MAX_LEVEL - t->level1) * elapsed_ms / t->decay1_ms;
            return (uint16_t)(AUDIO_ENV_MAX_LEVEL - used);
        }
        elapsed_ms -= t->decay1_ms;
    }

    if (t->decay2_ms != 0U) {
        if (elapsed_ms < t->decay2_ms) {
            used = (uint32_t)(t->level1 - t->floor_lv) * elapsed_ms / t->decay2_ms;
            return (uint16_t)(t->level1 - used);
        }
    }

    return t->floor_lv;
}

/* 钢琴的亮->暗过渡：0 = 全亮，255 = 全暗 */
static uint8_t audio_wave_mix_for(uint32_t elapsed_ms, const audio_timbre_t *t)
{
    if (t->bright_ms == 0U) {
        return 0U;
    }
    if (elapsed_ms >= t->bright_ms) {
        return 255U;
    }
    return (uint8_t)(elapsed_ms * 255U / t->bright_ms);
}

/* ---------------------------------------------------- 把包络电平写进占空比
 *
 * 最终占空比 = 本声部增益 × 包络电平 / 1000。
 * 本声部增益优先用 slot.gain（固定音量的系统提示音），
 * 为 0 时才使用全局音量档位。
 */
static void audio_apply_env_duty(uint8_t slot, uint16_t level)
{
    uint32_t base = (s_slot[slot].gain != 0U)
                        ? (uint32_t)s_slot[slot].gain
                        : (uint32_t)audio_duty_of(s_volume);
    uint32_t duty = (base * (uint32_t)level) / (uint32_t)AUDIO_ENV_MAX_LEVEL;

    if (level == 0U) {
        duty = 0UL;
    }
    AudioPwm_SetDuty(slot, (uint16_t)duty);
}

/* -------------------------------------------------------------- 选一个声部
 *
 * 1) 有空闲声部就直接用；
 * 2) 没有空闲时，找一个优先级数值更大（更不重要）的声部抢占；
 * 3) 抢不到返回 AUDIO_VOICE_MAX。
 *
 * 键盘优先级是 0，没有任何值比它更小，所以键盘永远不会被抢走。
 */
static uint8_t audio_alloc_slot(uint8_t prio)
{
    uint8_t i;
    uint8_t victim      = (uint8_t)AUDIO_VOICE_MAX;
    uint8_t victim_prio = 0U;

    for (i = 0U; i < AUDIO_VOICE_MAX; i++) {
        if (!s_slot[i].active) {
            return i;
        }
    }

    for (i = 0U; i < AUDIO_VOICE_MAX; i++) {
        if (s_slot[i].prio > prio) {
            if ((victim == (uint8_t)AUDIO_VOICE_MAX) || (s_slot[i].prio > victim_prio)) {
                victim      = i;
                victim_prio = s_slot[i].prio;
            }
        }
    }
    return victim;
}

/* ------------------------------------------------------------ 占用一个声部 */
static VoiceId audio_take_slot(uint8_t slot, uint8_t note, uint8_t octave,
                               uint16_t freq_hz, uint8_t prio, uint16_t gain)
{
    uint32_t now = HAL_GetTick();

    s_slot[slot].gen        = (uint8_t)((s_slot[slot].gen + 1U) & AUDIO_ID_GEN_MASK);
    s_slot[slot].active     = true;
    s_slot[slot].note       = note;
    s_slot[slot].octave     = octave;
    s_slot[slot].freq_hz    = freq_hz;
    s_slot[slot].prio       = prio;
    s_slot[slot].gain       = gain;
    s_slot[slot].t_start_ms = now;
    s_slot[slot].env_lv     = 0U;
    s_slot[slot].id         = audio_make_id(slot, s_slot[slot].gen);

    return s_slot[slot].id;
}

/* ---------------------------------------------------------- 通用的起音流程
 *
 * 键盘发声、回放发声、节拍器都走这里，保证优先级、包络、抢占行为一致。
 * gain 为 0 时跟随全局音量；非 0 表示本声部使用固定增益（系统提示音）。
 */
static AudioStatus audio_start_pitch_gain(uint16_t freq_hz, uint8_t prio,
                                          uint8_t note, uint8_t octave,
                                          uint16_t gain, VoiceId *out_id)
{
    uint8_t  slot;
    uint16_t lv;
    VoiceId  id;
    const audio_timbre_t *ts;

    if (!s_inited) {
        return AUDIO_ERR_BAD_PARAM;
    }
    if ((freq_hz < AUDIO_FREQ_MIN_HZ) || (freq_hz > AUDIO_FREQ_MAX_HZ)) {
        return AUDIO_ERR_BAD_PARAM;
    }
    if (gain > AUDIO_DUTY_PERMILLE_5) {
        return AUDIO_ERR_BAD_PARAM;
    }

    slot = audio_alloc_slot(prio);
    if (slot >= (uint8_t)AUDIO_VOICE_MAX) {
        return AUDIO_ERR_NO_VOICE;
    }

    /* 若这是抢占来的声部，先把原来那个音彻底停掉 */
    if (s_slot[slot].active) {
        AudioPwm_Silence(slot);
        s_slot[slot].active = false;
        s_slot[slot].id     = AUDIO_VOICE_INVALID;
        s_slot[slot].gain   = 0U;
    }

    /* 先设频率并把占空比置零，避免起音瞬间以最大音量"咔"一下 */
    if (!AudioPwm_SetTone(slot, freq_hz, 0U)) {
        return AUDIO_ERR_BAD_PARAM;
    }

    id = audio_take_slot(slot, note, octave, freq_hz, prio, gain);

    /* 波形与包络起点：起音段从 0 开始，由主循环的 Envelope_Tick 拉起来 */
    ts = audio_timbre_now();
    AudioPwm_SetWave(slot, ts->wave);
    AudioPwm_SetWaveMix(slot, 0U);
    s_slot[slot].wave_mix = 0U;
    lv = audio_env_level_for(0UL, ts);
    s_slot[slot].env_lv = lv;
    audio_apply_env_duty(slot, lv);

    if (out_id != NULL) {
        *out_id = id;
    }
    return AUDIO_OK;
}

static AudioStatus audio_start_pitch(uint16_t freq_hz, uint8_t prio,
                                     uint8_t note, uint8_t octave, VoiceId *out_id)
{
    return audio_start_pitch_gain(freq_hz, prio, note, octave, 0U, out_id);
}

/* ------------------------------------------------------------------ 初始化 */
void Audio_Init(void)
{
    uint8_t i;

    for (i = 0U; i < AUDIO_VOICE_MAX; i++) {
        s_slot[i].active     = false;
        s_slot[i].note       = 0U;
        s_slot[i].octave     = (uint8_t)OCT_MID;
        s_slot[i].gen        = 0U;
        s_slot[i].prio       = 0xFFU;
        s_slot[i].id         = AUDIO_VOICE_INVALID;
        s_slot[i].freq_hz    = 0U;
        s_slot[i].env_lv     = 0U;
        s_slot[i].gain       = 0U;
        s_slot[i].wave_mix   = 0U;
        s_slot[i].t_start_ms = 0UL;
    }

    s_octave    = OCT_MID;
    s_volume    = AUDIO_VOLUME_DEFAULT;
    s_timbre    = TIMBRE_ORGAN;
    s_transpose = 0;

    AudioPwm_Init();

    /* 子模块一并复位，上层只需要调 Audio_Init 一次 */
    Metronome_Init();
    Song_Init();
    Drum_Init();

    s_inited = true;

    /* 保险起见再静音一次 */
    Audio_AllOff();
}

/* -------------------------------------------------------------- 按音符发声
 *
 * 键盘演奏专用，固定使用最高优先级，永不被回放抢占。
 */
VoiceId Audio_NoteOn(uint8_t note)
{
    uint16_t    hz;
    VoiceId     id = AUDIO_VOICE_INVALID;
    AudioStatus st;

    if (!s_inited) {
        return (VoiceId)AUDIO_ERR_BAD_PARAM;
    }
    if (!AudioPitch_IsValidNote(note, (uint8_t)s_octave)) {
        return (VoiceId)AUDIO_ERR_BAD_NOTE;
    }

    hz = AudioPitch_GetFreq(note, (uint8_t)s_octave, s_transpose);
    if (hz == 0U) {
        return (VoiceId)AUDIO_ERR_BAD_NOTE;
    }

    st = audio_start_pitch(hz, AUDIO_PRIO_KEYBOARD, note, (uint8_t)s_octave, &id);
    if (st != AUDIO_OK) {
        return (VoiceId)st;
    }
    return id;
}

/* -------------------------------------------------------------- 按频率发声 */
AudioStatus Audio_NoteOnPitch(uint16_t freq_hz, VoiceId *out_id)
{
    return audio_start_pitch(freq_hz, AUDIO_PRIO_DEFAULT, 0xFFU,
                             (uint8_t)s_octave, out_id);
}

/* ------------------------------------------------------ 带优先级的发声 */
AudioStatus Audio_NoteOnPitchEx(uint16_t freq_hz, uint8_t priority, VoiceId *out_id)
{
    return audio_start_pitch(freq_hz, priority, 0xFFU,
                             (uint8_t)s_octave, out_id);
}

/* ------------------------------------------- 固定音量的提示音发声
 *
 * 开机琶音、菜单提示音这类系统音效使用：音量由调用者给定，
 * 不随 Audio_SetVolume 改变。permille 取值 1..500。
 */
AudioStatus Audio_NoteOnPitchGain(uint16_t freq_hz, uint16_t permille,
                                  uint8_t priority, VoiceId *out_id)
{
    if ((permille == 0U) || (permille > AUDIO_DUTY_PERMILLE_5)) {
        return AUDIO_ERR_BAD_PARAM;
    }
    return audio_start_pitch_gain(freq_hz, priority, 0xFFU,
                                  (uint8_t)s_octave, permille, out_id);
}

/* -------------------------------------------------------- 改变已发声音高
 *
 * 鼓组每推进一格就调用一次：只改频率，不释放也不重新申请声部，
 * 因此不会触发"声部已满"的失败，也不会打断优先级关系。
 */
AudioStatus Audio_Retune(VoiceId id, uint16_t freq_hz)
{
    uint8_t slot;

    if (!s_inited) {
        return AUDIO_ERR_BAD_PARAM;
    }
    if (id < 0) {
        return AUDIO_ERR_BAD_PARAM;
    }
    if ((freq_hz < AUDIO_FREQ_MIN_HZ) || (freq_hz > AUDIO_FREQ_MAX_HZ)) {
        return AUDIO_ERR_BAD_PARAM;
    }

    slot = audio_id_slot(id);

    /* 标识失效说明这路已被停止或被高优先级抢占，不能再动它 */
    if ((!s_slot[slot].active) || (s_slot[slot].id != id)) {
        return AUDIO_ERR_BAD_PARAM;
    }

    if (!AudioPwm_SetTone(slot, freq_hz, 0U)) {
        return AUDIO_ERR_BAD_PARAM;
    }

    /* 换频后把占空比按当前包络电平重新写回，避免静音 */
    s_slot[slot].freq_hz = freq_hz;
    audio_apply_env_duty(slot, s_slot[slot].env_lv);

    return AUDIO_OK;
}

/* ------------------------------------------------------------------ 停音 */
AudioStatus Audio_NoteOff(VoiceId id)
{
    uint8_t slot;

    if (!s_inited) {
        return AUDIO_ERR_BAD_PARAM;
    }
    /* VoiceId 是 int8_t，上限恒为 127，只需排除负数 */
    if (id < 0) {
        return AUDIO_ERR_BAD_PARAM;
    }

    slot = audio_id_slot(id);

    /* 标识与当前记录不符：可能早已停止，也可能已被优先级更高的音抢占。
     * 两种都属于重复释放，直接返回成功，绝不能去停别的音。 */
    if (s_slot[slot].id != id) {
        return AUDIO_OK;
    }

    AudioPwm_Silence(slot);
    s_slot[slot].active = false;
    s_slot[slot].id     = AUDIO_VOICE_INVALID;
    s_slot[slot].prio   = 0xFFU;

    return AUDIO_OK;
}

/* -------------------------------------------------------------- 全部停止 */
void Audio_AllOff(void)
{
    uint8_t i;

    for (i = 0U; i < AUDIO_VOICE_MAX; i++) {
        AudioPwm_Silence(i);
        s_slot[i].active = false;
        s_slot[i].id     = AUDIO_VOICE_INVALID;
        s_slot[i].prio   = 0xFFU;
        s_slot[i].env_lv = 0U;
        s_slot[i].gain   = 0U;
        s_slot[i].wave_mix = 0U;
    }
}

/* -------------------------------------------------------------- 状态查询 */
uint8_t Audio_GetActiveCount(void)
{
    uint8_t i;
    uint8_t n = 0U;

    for (i = 0U; i < AUDIO_VOICE_MAX; i++) {
        if (s_slot[i].active) {
            n++;
        }
    }
    return n;
}

bool Audio_IsVoiceActive(VoiceId id)
{
    uint8_t slot;

    if (id < 0) {
        return false;
    }
    slot = audio_id_slot(id);

    return (bool)(s_slot[slot].active && (s_slot[slot].id == id));
}

/* 当前有几路声部正在为键盘演奏服务。
 * 节拍器用这个判断：只要用户在手弹，就让位，不再抢只剩的那一路。 */
uint8_t Audio_GetKeyboardCount(void)
{
    uint8_t i;
    uint8_t n = 0U;

    for (i = 0U; i < AUDIO_VOICE_MAX; i++) {
        if (s_slot[i].active && (s_slot[i].prio == AUDIO_PRIO_KEYBOARD)) {
            n++;
        }
    }
    return n;
}

/* -------------------------------------------------------------- 音区设置 */
void Audio_SetOctave(OctaveId oct)
{
    if ((uint8_t)oct >= OCT_COUNT) {
        return;
    }
    /* 只影响之后新按下的音；正在响的音保持原频率 */
    s_octave = oct;
}

OctaveId Audio_GetOctave(void)
{
    return s_octave;
}

/* -------------------------------------------------------------- 音量设置 */
void Audio_SetVolume(VolumeId vol)
{
    uint8_t i;

    if ((uint8_t)vol >= VOL_COUNT) {
        return;
    }
    s_volume = vol;

    /* 对正在发声的音立即生效。占空比要乘上各自当前的包络电平，
     * 不能直接写满，否则衰减中的音会被瞬间拉回最大音量。 */
    for (i = 0U; i < AUDIO_VOICE_MAX; i++) {
        if (s_slot[i].active) {
            audio_apply_env_duty(i, s_slot[i].env_lv);
        }
    }
}

VolumeId Audio_GetVolume(void)
{
    return s_volume;
}

/* -------------------------------------------------------------- 移调设置 */
AudioStatus Audio_SetTranspose(int8_t semitone)
{
    if ((semitone < AUDIO_TRANSPOSE_MIN) || (semitone > AUDIO_TRANSPOSE_MAX)) {
        return AUDIO_ERR_BAD_PARAM;
    }
    /* 同样只影响新按下的音 */
    s_transpose = semitone;
    return AUDIO_OK;
}

int8_t Audio_GetTranspose(void)
{
    return s_transpose;
}

/* -------------------------------------------------------------- 音色设置 */
void Audio_SetTimbre(TimbreId timbre)
{
    uint8_t  i;
    uint32_t now;
    const audio_timbre_t *ts;

    if ((uint8_t)timbre >= (uint8_t)TIMBRE_COUNT) {
        return;
    }
    s_timbre = timbre;

    if (!s_inited) {
        return;
    }

    /* 正在响的音立即切到新音色：换波形、把包络起点挪到"起音刚结束"，
     * 避免半途跳变，也避免重新起音造成一个 4 ms 的凹陷。 */
    ts = audio_timbre_now();
    now = HAL_GetTick();
    for (i = 0U; i < AUDIO_VOICE_MAX; i++) {
        if (s_slot[i].active) {
            s_slot[i].t_start_ms = now - (uint32_t)ts->attack_ms;
            s_slot[i].env_lv     = AUDIO_ENV_MAX_LEVEL;
            s_slot[i].wave_mix   = 0U;
            audio_apply_env_duty(i, AUDIO_ENV_MAX_LEVEL);
            AudioPwm_SetWave(i, ts->wave);
            AudioPwm_SetWaveMix(i, 0U);
        }
    }
}

TimbreId Audio_GetTimbre(void)
{
    return s_timbre;
}

/* ------------------------------------------------------------ 包络处理
 *
 * 主循环周期调用。按每个声部自己的起点时间算电平与钢琴的亮/暗混合，
 * 与调用频率无关，主循环被阻塞也不会让衰减走样。
 */
void Audio_Envelope_Tick(void)
{
    uint8_t  i;
    const audio_timbre_t *ts;
    uint32_t now;
    uint32_t elapsed;
    uint16_t lv;
    uint8_t  mix;

    if (!s_inited) {
        return;
    }
    ts = audio_timbre_now();
    /* 管风琴恒定音量，不需要每轮计算 */
    if ((ts->attack_ms == 0U) && (ts->decay1_ms == 0U) && (ts->decay2_ms == 0U)) {
        return;
    }

    now = HAL_GetTick();

    for (i = 0U; i < AUDIO_VOICE_MAX; i++) {
        if (!s_slot[i].active) {
            continue;
        }
        elapsed = now - s_slot[i].t_start_ms;
        lv = audio_env_level_for(elapsed, ts);

        /* 电平没变就不写寄存器，避免无谓的总线操作 */
        if (lv != s_slot[i].env_lv) {
            s_slot[i].env_lv = lv;
            audio_apply_env_duty(i, lv);
        }

        mix = audio_wave_mix_for(elapsed, ts);
        if (mix != s_slot[i].wave_mix) {
            s_slot[i].wave_mix = mix;
            AudioPwm_SetWaveMix(i, mix);
        }
    }
}

/* -------------------------------------------------------------- 频率查询 */
uint16_t Audio_GetFreqOfNote(uint8_t note, OctaveId octave, int8_t transpose)
{
    return AudioPitch_GetFreq(note, (uint8_t)octave, transpose);
}

uint16_t Audio_GetCurrentFreq(uint8_t note)
{
    return AudioPitch_GetFreq(note, (uint8_t)s_octave, s_transpose);
}

/* ---------------------------------------------------------- 声部实际频率
 *
 * 录音时需要把"按下那一刻的真实频率"写进 NoteEvent。
 * 不能事后用 Audio_GetCurrentFreq() 反推——如果中途改过音区，
 * 反推出来的就不是当时听到的那个音了。
 */
uint16_t Audio_GetVoiceFreq(VoiceId id)
{
    uint8_t slot;

    if (id < 0) {
        return 0U;
    }
    slot = audio_id_slot(id);

    if ((!s_slot[slot].active) || (s_slot[slot].id != id)) {
        return 0U;
    }
    return s_slot[slot].freq_hz;
}
