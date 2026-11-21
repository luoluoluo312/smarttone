/* ============================================================================
 *  song.c   —— 曲目播放调度
 *
 *  模块：06_audio_song
 *  说明：三段式状态机（发声中 / 间隙中 / 待下一音），全部基于毫秒时间戳。
 * ========================================================================== */

#include "song.h"
#include "audio.h"
#include "audio_config.h"
#include "stm32f1xx_hal.h"

/* ---------------------------------------------------------------- 内部状态 */
#define SONG_PHASE_NOTE   0U
#define SONG_PHASE_GAP    1U

static struct {
    SongState      state;
    const SongDef *def;
    uint16_t       idx;        /* 下一格的下标 */
    uint16_t       cur;        /* 正在播放那一格的下标 */
    uint8_t        phase;
    uint8_t        speed;      /* 0=0.5x 1=1.0x 2=2.0x */
    uint32_t       t_next;     /* 下一次动作的绝对时刻 */
    VoiceId        vid;        /* 正在发声的标识 */
    uint32_t       total_ms;   /* 整曲总时长（已按速度缩放） */
    uint32_t       elapsed_ms; /* 已播放时长，暂停时不累加，同样按速度缩放 */
    uint32_t       last_ms;    /* 上次 Tick 的时刻，用于累加 */
    TimbreId       saved_timbre;
    bool           restore_timbre;
    uint16_t       gain;       /* 本曲固定增益（‰）；0 = 跟随全局音量 */
} s_song;

/* Dual-track cursors store unscaled grid time; both share elapsed_ms. */
typedef struct {
    uint32_t start_ms;
    uint16_t idx;
    VoiceId vid;
    bool started;
} song_track_t;
static song_track_t s_tracks[2] = {{.vid = AUDIO_VOICE_INVALID}, {.vid = AUDIO_VOICE_INVALID}};

static bool song_has_accompaniment(void)
{
    return s_song.def != NULL && s_song.def->accompaniment_count != 0U;
}

static void track_voice_off(song_track_t *track)
{
    if (track->vid >= 0) { (void)Audio_NoteOff(track->vid); }
    track->vid = AUDIO_VOICE_INVALID;
}

static void tracks_reset(void)
{
    for (uint8_t i = 0U; i < 2U; ++i) {
        track_voice_off(&s_tracks[i]);
        s_tracks[i].idx = 0U;
        s_tracks[i].start_ms = 0U;
        s_tracks[i].started = false;
    }
}

/* 把曲谱上的毫秒时值换算成"当前速度下真实要等多久"。
 * 慢速时值翻倍，快速时值减半，音高完全不受影响。 */
static uint32_t song_scaled(uint32_t ms)
{
    switch (s_song.speed) {
    case 0U:  return ms * 2UL;
    case 2U:  return ms / 2UL;
    default:  return ms;
    }
}

static void song_restore_timbre(void)
{
    if (s_song.restore_timbre) {
        Audio_SetTimbre(s_song.saved_timbre);
        s_song.restore_timbre = false;
    }
}

/* ------------------------------------------------------------------ 初始化 */
void Song_Init(void)
{
    Sample_Stop();
    s_song.state      = SONG_IDLE;
    s_song.def        = NULL;
    s_song.idx        = 0U;
    s_song.cur        = 0U;
    s_song.phase      = SONG_PHASE_GAP;
    s_song.t_next     = 0UL;
    s_song.vid        = AUDIO_VOICE_INVALID;
    s_song.speed      = 1U;
    s_song.total_ms   = 0UL;
    s_song.elapsed_ms = 0UL;
    s_song.last_ms    = 0UL;
    s_song.gain       = 0U;
    s_song.restore_timbre = false;
    for (uint8_t i = 0U; i < 2U; ++i) {
        s_tracks[i] = (song_track_t){.vid = AUDIO_VOICE_INVALID};
    }
}

/* ------------------------------------------------------------ 整曲总时长 */
static uint32_t song_calc_total_ms(const SongDef *def)
{
    uint32_t total = 0UL;
    uint16_t i;

    if (def == NULL) {
        return 0UL;
    }
    if (def->sample != NULL) { return song_scaled((def->sample->samples + 15U) / 16U); }
    if (def->accompaniment_count != 0U) {
        uint32_t length[2] = {0U, 0U};
        const SongNote *tracks[2] = {def->notes, def->accompaniment};
        uint16_t counts[2] = {def->count, def->accompaniment_count};
        for (uint8_t t = 0U; t < 2U; ++t) {
            for (i = 0U; i < counts[t]; ++i) {
                length[t] += tracks[t][i].dur_ms;
                if (tracks[t][i].note != SONG_NOTE_REST) { length[t] += SONG_ARTICULATION_GAP_MS; }
            }
        }
        return song_scaled(length[0] > length[1] ? length[0] : length[1]);
    }
    for (i = 0U; i < def->count; i++) {
        total += song_scaled((uint32_t)def->notes[i].dur_ms);
        if (def->notes[i].note != SONG_NOTE_REST) {
            total += song_scaled(SONG_ARTICULATION_GAP_MS);
        }
    }
    return total;
}

/* ------------------------------------------------------------ 起下一个音 */
static void song_start_next(uint32_t now)
{
    const SongNote *n;
    uint16_t hz;
    VoiceId  id = AUDIO_VOICE_INVALID;

    if (s_song.def == NULL) {
        s_song.state = SONG_IDLE;
        return;
    }

    /* 到达结尾 */
    if (s_song.idx >= s_song.def->count) {
        if (s_song.def->loop) {
            s_song.idx = 0U;
            s_song.elapsed_ms = 0U;
        } else {
            s_song.state = SONG_IDLE;
            s_song.vid   = AUDIO_VOICE_INVALID;
            song_restore_timbre();
            return;
        }
    }

    s_song.cur = s_song.idx;
    n = &s_song.def->notes[s_song.idx];
    s_song.idx++;

    /* 休止符 */
    if (n->note == SONG_NOTE_REST) {
        s_song.vid    = AUDIO_VOICE_INVALID;
        s_song.phase  = SONG_PHASE_GAP;
        s_song.t_next = now + song_scaled((uint32_t)n->dur_ms);
        return;
    }

    /* 非 0 的 freq_hz 优先：直接发声，不做移调，也不套用音区 */
    if (n->freq_hz != 0U) {
        hz = n->freq_hz;
    } else {
        /* 套用当前移调；音区用曲谱自己的 */
        hz = Audio_GetFreqOfNote(n->note, (OctaveId)n->octave, Audio_GetTranspose());
    }

    /* 声部满时这一个音不响，但时间照走，保证节拍不乱 */
    if (hz != 0U) {
        if (s_song.gain != 0U) {
            (void)Audio_NoteOnPitchGain(hz, s_song.gain, AUDIO_PRIO_DEFAULT, &id);
        } else {
            (void)Audio_NoteOnPitch(hz, &id);
        }
    }

    s_song.vid    = id;
    s_song.phase  = SONG_PHASE_NOTE;
    s_song.t_next = now + song_scaled((uint32_t)n->dur_ms);
}

/* -------------------------------------------------------------- 停止发声 */
static void song_voice_off(void)
{
    if (s_song.vid >= 0) {
        (void)Audio_NoteOff(s_song.vid);
    }
    s_song.vid = AUDIO_VOICE_INVALID;
}

/* Finish expired slots against a shared timeline, without adding main-loop delay. */
static void track_tick(song_track_t *track, const SongNote *notes, uint16_t count,
                       uint32_t elapsed, uint8_t priority)
{
    if (track->vid >= 0 && !Audio_IsVoiceActive(track->vid)) { track->vid = AUDIO_VOICE_INVALID; }
    while (track->idx < count) {
        const SongNote *n = &notes[track->idx];
        uint32_t note_end = track->start_ms + n->dur_ms;
        uint32_t slot_end = note_end + (n->note != SONG_NOTE_REST ? SONG_ARTICULATION_GAP_MS : 0U);
        if (elapsed >= song_scaled(slot_end)) {
            track_voice_off(track);
            track->start_ms = slot_end;
            ++track->idx;
            track->started = false;
            continue;
        }
        if (elapsed >= song_scaled(note_end)) {
            track_voice_off(track);
        } else if (!track->started && n->note != SONG_NOTE_REST) {
            uint16_t hz = n->freq_hz != 0U ? n->freq_hz :
                Audio_GetFreqOfNote(n->note, (OctaveId)n->octave, Audio_GetTranspose());
            if (s_song.gain != 0U) {
                (void)Audio_NoteOnPitchGain(hz, s_song.gain, priority, &track->vid);
            } else {
                (void)Audio_NoteOnPitchEx(hz, priority, &track->vid);
            }
            track->started = true; /* A stolen or naturally decayed note is not retriggered. */
        }
        return;
    }
    track_voice_off(track);
}

static void song_dual_tick(void)
{
    if (s_song.elapsed_ms >= s_song.total_ms) {
        tracks_reset();
        if (s_song.def->loop && s_song.total_ms != 0U) {
            s_song.elapsed_ms %= s_song.total_ms;
        } else {
            s_song.elapsed_ms = s_song.total_ms;
            s_song.state = SONG_IDLE;
            song_restore_timbre();
            return;
        }
    }
    track_tick(&s_tracks[0], s_song.def->notes, s_song.def->count,
               s_song.elapsed_ms, AUDIO_PRIO_MELODY);
    track_tick(&s_tracks[1], s_song.def->accompaniment, s_song.def->accompaniment_count,
               s_song.elapsed_ms, AUDIO_PRIO_TRACK2);
}

/* ------------------------------------------------------------------ 播放 */
bool Song_Play(const SongDef *def)
{
    return Song_PlayGain(def, 0U);
}

bool Song_PlayGain(const SongDef *def, uint16_t permille)
{
    if ((def == NULL) || (def->sample == NULL && (def->notes == NULL || def->count == 0U)) ||
        (def->timbre > TIMBRE_COUNT) ||
        (def->accompaniment_count != 0U && def->accompaniment == NULL)) {
        return false;
    }

    Song_Stop();
    if (def->timbre != 0U) {
        s_song.saved_timbre = Audio_GetTimbre();
        s_song.restore_timbre = true;
        Audio_SetTimbre((TimbreId)(def->timbre - 1U));
    }

    s_song.def        = def;
    s_song.idx        = 0U;
    s_song.cur        = 0U;
    s_song.state      = SONG_PLAYING;
    s_song.total_ms   = song_calc_total_ms(def);
    s_song.elapsed_ms = 0UL;
    s_song.last_ms    = HAL_GetTick();
    s_song.gain       = (permille > AUDIO_DUTY_PERMILLE_5) ? AUDIO_DUTY_PERMILLE_5
                                                           : permille;

    if (def->sample != NULL) {
        if (!Sample_Play(def->sample, s_song.gain, s_song.speed)) { Song_Stop(); return false; }
        return true;
    }
    if (song_has_accompaniment()) { song_dual_tick(); }
    else { song_start_next(HAL_GetTick()); }
    return true;
}

/* ------------------------------------------------------------------ 暂停 */
void Song_Pause(void)
{
    if (s_song.state != SONG_PLAYING) {
        return;
    }
    if (s_song.def->sample != NULL) {
        Sample_Pause(true);
        s_song.elapsed_ms = song_scaled(Sample_PositionMs());
        s_song.state = SONG_PAUSED;
        return;
    }
    song_voice_off();
    if (song_has_accompaniment()) {
        for (uint8_t i = 0U; i < 2U; ++i) {
            track_voice_off(&s_tracks[i]);
            s_tracks[i].started = false;
        }
        s_song.state = SONG_PAUSED;
        return;
    }

    /* 回退一格，恢复时把这一格重新播一遍 */
    s_song.idx   = s_song.cur;
    s_song.state = SONG_PAUSED;
}

/* ------------------------------------------------------------------ 恢复 */
void Song_Resume(void)
{
    if (s_song.state != SONG_PAUSED) {
        return;
    }
    s_song.state = SONG_PLAYING;
    s_song.last_ms = HAL_GetTick();
    if (s_song.def->sample != NULL) { Sample_Pause(false); return; }
    if (song_has_accompaniment()) { song_dual_tick(); }
    else { song_start_next(s_song.last_ms); }
}

/* ------------------------------------------------------------------ 停止 */
void Song_Stop(void)
{
    Sample_Stop();
    song_voice_off();
    tracks_reset();
    song_restore_timbre();
    s_song.state = SONG_IDLE;
    s_song.phase = SONG_PHASE_GAP;
    s_song.idx   = 0U;
    s_song.cur   = 0U;
    s_song.t_next = 0UL;
    s_song.gain  = 0U;
}

/* ------------------------------------------------------------------ 查询 */
SongState Song_GetState(void)
{
    return s_song.state;
}

const SongDef *Song_GetCurrent(void)
{
    return s_song.def;
}

void Song_SetSpeed(uint8_t level)
{
    if (level > 2U) {
        return;
    }
    uint8_t old_speed = s_song.speed;
    s_song.speed = level;
    if (s_song.def != NULL) {
        uint32_t old_total = s_song.total_ms;
        s_song.total_ms = song_calc_total_ms(s_song.def);
        if (s_song.def->sample != NULL) {
            if (old_speed != level) { Sample_SetSpeed(level); }
            s_song.elapsed_ms = song_scaled(Sample_PositionMs());
            return;
        }
        if (old_total != 0U && old_speed != level) {
            if (song_has_accompaniment()) {
                /* Rates are exact powers of two; odd total lengths must not
                 * introduce an extra seek error at every encoder step. */
                if (old_speed > level) { s_song.elapsed_ms <<= old_speed - level; }
                else { s_song.elapsed_ms >>= level - old_speed; }
            } else {
                s_song.elapsed_ms = (uint32_t)((uint64_t)s_song.elapsed_ms * s_song.total_ms / old_total);
            }
        }
        if (song_has_accompaniment() && old_speed != level) {
            tracks_reset();
            if (s_song.state == SONG_PLAYING) { song_dual_tick(); }
        }
    }
}

uint8_t Song_GetSpeed(void)
{
    return s_song.speed;
}

/* ------------------------------------------------------------ 播放进度
 *
 * 进度按毫秒累计计算，暂停时不增长。
 * 循环播放时会到 100 后从 0 重新开始。
 */
uint8_t Song_GetProgress(void)
{
    uint32_t pct;

    if (s_song.total_ms == 0UL) {
        return 0U;
    }
    if (s_song.state == SONG_IDLE) {
        return 0U;
    }

    pct = (s_song.elapsed_ms * 100UL) / s_song.total_ms;
    if (pct > 100UL) {
        pct = 100UL;
    }
    return (uint8_t)pct;
}

uint32_t Song_GetElapsedMs(void)
{
    return s_song.elapsed_ms;
}

uint32_t Song_GetTotalMs(void)
{
    return s_song.total_ms;
}

/* ------------------------------------------------------------ 周期调度 */
void Song_Tick(void)
{
    uint32_t now;
    uint32_t dt;

    now = HAL_GetTick();

    /* 无论什么状态都要更新 last_ms，否则恢复播放时会一次性跳一大段 */
    dt = now - s_song.last_ms;
    s_song.last_ms = now;

    if (s_song.state != SONG_PLAYING) {
        return;
    }

    if (s_song.def->sample != NULL) {
        Sample_Service();
        s_song.elapsed_ms = song_scaled(Sample_PositionMs());
        if (Sample_Finished() || Sample_Failed()) {
            Sample_Stop();
            s_song.state = SONG_IDLE;
            song_restore_timbre();
        }
        return;
    }
    s_song.elapsed_ms += dt;
    if (song_has_accompaniment()) {
        song_dual_tick();
        return;
    }
    if (s_song.vid >= 0 && !Audio_IsVoiceActive(s_song.vid)) {
        s_song.vid = AUDIO_VOICE_INVALID;
    }

    if ((int32_t)(now - s_song.t_next) < 0) {
        return;
    }

    /* 当前音到时：先停唱，再决定是进间隙还是直接下一格 */
    if (s_song.phase == SONG_PHASE_NOTE) {
        song_voice_off();

        if (SONG_ARTICULATION_GAP_MS > 0U) {
            s_song.phase  = SONG_PHASE_GAP;
            s_song.t_next = now + song_scaled(SONG_ARTICULATION_GAP_MS);
            return;
        }
    }

    song_start_next(now);
}
