#include "melody_maker.h"
#include "audio.h"
#include "bsp_notes.h"
#include "bsp_tft.h"
#include "record.h"

#include <string.h>
#include <stdio.h>

/* Rhythm units are eighth notes: every template totals eight units (4/4). */
static const uint8_t s_rhythm[6][6] = {
    {2,2,4,0,0,0}, {1,1,2,4,0,0}, {4,2,2,0,0,0},
    {2,1,1,2,2,0}, {2,2,2,2,0,0}, {1,1,1,1,2,2}
};
static const uint8_t s_weights[3][6] = {
    {35,10,35,5,10,5}, {5,22,5,23,20,25}, {30,25,25,5,10,5}
};
static const uint8_t s_bpm_min[3] = {65,110,80};
static const uint8_t s_bpm_max[3] = {85,140,105};
static const char *const s_names[5] = {"PIANO","GUITAR","ACCORDION","BASS","MUSIC BOX"};
static const char *const s_styles[3] = {"CALM","HAPPY","DREAM"};
static const char *const s_note_names[7] = {"DO","RE","MI","FA","SO","LA","SI"};
static const char *const s_actions[3] = {"REPLAY","NEW SONG","BACK"};
static const int16_t s_action_x[3] = {1,43,97};
static const int16_t s_action_w[3] = {40,52,30};

static melody_event_t s_events[MELODY_MAX_EVENTS];
static melody_info_t s_info;
static VoiceId s_voice = AUDIO_VOICE_INVALID;
static uint32_t s_begin, s_generation;
static uint8_t s_next, s_action;
static bool s_active, s_exit;
static TimbreId s_saved_timbre;
static OctaveId s_saved_octave;
static int8_t s_saved_transpose;
static bool s_saved_metro;
static uint32_t s_draw_ms;
static uint8_t s_draw_note, s_draw_progress, s_draw_state;
static uint32_t s_draw_seconds;

static uint32_t next_random(uint32_t *state)
{
    uint32_t v = *state;
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    *state = v;
    return v;
}

static void stop_voice(void)
{
    if (s_voice >= 0) { (void)Audio_NoteOff(s_voice); }
    s_voice = AUDIO_VOICE_INVALID;
    s_info.current_note = MELODY_REST;
}

static uint8_t choose_rhythm(uint32_t *rng, uint8_t style, TimbreId timbre)
{
    uint16_t total = 0U;
    for (uint8_t i = 0U; i < 6U; ++i) {
        if (timbre != TIMBRE_MUSICBOX || i != 5U) { total += s_weights[style][i]; }
    }
    uint32_t pick = next_random(rng) % total;
    for (uint8_t i = 0U; i < 6U; ++i) {
        uint8_t w = (timbre == TIMBRE_MUSICBOX && i == 5U) ? 0U : s_weights[style][i];
        if (pick < w) { return i; }
        pick -= w;
    }
    return 0U;
}

static uint8_t walk_note(uint32_t *rng, uint8_t style, uint8_t note, uint8_t repeats)
{
    uint8_t adjacent = (style == MELODY_CALM) ? 70U : (style == MELODY_HAPPY ? 55U : 45U);
    uint8_t repeat = (style == MELODY_CALM) ? 25U : (style == MELODY_HAPPY ? 15U : 40U);
    uint32_t roll = next_random(rng) % 100U;
    int8_t step = 0;
    if (roll < adjacent || (style == MELODY_HAPPY && repeats >= 2U)) { step = 1; }
    else if (roll >= adjacent + repeat) { step = (int8_t)(2U + next_random(rng) % 2U); }
    if (next_random(rng) & 1U) { step = (int8_t)-step; }
    int8_t value = (int8_t)note + step;
    if (value < 0) { value = (int8_t)-value; }
    if (value > 6) { value = (int8_t)(12 - value); }
    return (uint8_t)value;
}

bool melody_maker_generate(TimbreId timbre, uint32_t seed)
{
    if ((uint8_t)timbre >= TIMBRE_COUNT) { return false; }
    stop_voice();
    uint32_t rng = seed ? seed : 0x6D2B79F5UL;
    uint8_t style = (uint8_t)(next_random(&rng) % 3U);
    uint16_t bpm = (uint16_t)(s_bpm_min[style] + next_random(&rng) %
                            (s_bpm_max[style] - s_bpm_min[style] + 1U));
    /* Nearest whole 4/4 bar count to 17.5 seconds; 5..10 bars, <=60 events. */
    uint8_t bars = (uint8_t)(((uint32_t)bpm * 17500U + 120000U) / 240000U);
    uint8_t octave = (timbre == TIMBRE_BASS) ? OCT_LOW :
                     (timbre == TIMBRE_MUSICBOX ? OCT_HIGH : OCT_MID);
    struct { uint8_t rhythm, note[6]; } theme[2] = {{0}};
    uint8_t note = NOTE_DO, repeats = 0U, count = 0U;
    uint32_t units = 0U;
    memset(s_events, 0, sizeof(s_events));
    for (uint8_t bar = 0U; bar < bars; ++bar) {
        bool reprise = (bar == 2U || bar == 3U ||
                        (style == MELODY_DREAM && bar > 3U && (bar & 1U) == 0U));
        uint8_t rhythm = reprise ? theme[bar & 1U].rhythm : choose_rhythm(&rng, style, timbre);
        if (bar == bars - 1U) { rhythm = (uint8_t)(next_random(&rng) % 2U); }
        if (bar < 2U) { theme[bar].rhythm = rhythm; }
        for (uint8_t j = 0U; j < 6U && s_rhythm[rhythm][j] != 0U; ++j) {
            uint32_t start = units * 30000U / bpm;
            units += s_rhythm[rhythm][j];
            uint32_t end = units * 30000U / bpm;
            if (count >= MELODY_MAX_EVENTS) { s_info.count = 0U; return false; }
            uint8_t previous = note;
            note = walk_note(&rng, style, note, repeats);
            if (reprise && next_random(&rng) % 100U < (style == MELODY_DREAM ? 85U : 70U)) {
                note = theme[bar & 1U].note[j];
            }
            bool last = (bar == bars - 1U && (j == 5U || s_rhythm[rhythm][j + 1U] == 0U));
            if (bar == 0U && j == 0U) { note = NOTE_DO; }
            if (last) { note = NOTE_DO; }
            repeats = (note == previous) ? (uint8_t)(repeats + 1U) : 0U;
            uint8_t rest_percent = (style == MELODY_DREAM) ? 16U : (style == MELODY_CALM ? 4U : 5U);
            bool rest = next_random(&rng) % 100U < rest_percent;
            if (count == 0U || last) { rest = false; }
            if (bar < 2U) { theme[bar].note[j] = note; }
            uint16_t gap = (timbre == TIMBRE_ACCORDION) ? 8U : (timbre == TIMBRE_GUITAR ? 28U : 16U);
            s_events[count].start_ms = start;
            s_events[count].duration_ms = (uint16_t)(end - start - ((!rest && !last) ? gap : 0U));
            s_events[count].note = rest ? MELODY_REST : note;
            s_events[count].octave = octave;
            ++count;
        }
    }
    s_info.elapsed_ms = 0U;
    s_info.total_ms = units * 30000U / bpm;
    s_info.bpm = bpm;
    s_info.count = count;
    s_info.bars = bars;
    s_info.style = style;
    s_info.timbre = (uint8_t)timbre;
    s_info.state = MELODY_SELECT;
    s_info.current_note = MELODY_REST;
    return true;
}

bool melody_maker_replay(uint32_t now_ms)
{
    if (s_info.count == 0U) { return false; }
    stop_voice();
    Audio_SetTimbre((TimbreId)s_info.timbre);
    Audio_SetOctave((OctaveId)s_events[0].octave);
    (void)Audio_SetTranspose(0);
    s_next = 0U;
    s_begin = now_ms;
    s_info.elapsed_ms = 0U;
    s_info.state = MELODY_PLAYING;
    s_draw_note = 0xFEU;
    s_draw_progress = s_draw_state = 0xFFU;
    s_draw_seconds = UINT32_MAX;
    return true;
}

const melody_event_t *melody_maker_events(void) { return s_events; }
void melody_maker_get_info(melody_info_t *out) { if (out != NULL) { *out = s_info; } }
bool melody_maker_active(void) { return s_active; }

void melody_maker_enter(void)
{
    if (s_active) { return; }
    s_saved_timbre = Audio_GetTimbre();
    s_saved_octave = Audio_GetOctave();
    s_saved_transpose = Audio_GetTranspose();
    s_saved_metro = Metronome_IsRunning();
    Song_Stop();
    Drum_Stop();
    record_cancel();
    record_playback_stop();
    Metronome_Stop();
    Audio_AllOff();
    bsp_notes_init();
    s_voice = AUDIO_VOICE_INVALID;
    s_info.timbre = (uint8_t)s_saved_timbre;
    s_info.count = 0U;
    s_info.state = MELODY_SELECT;
    s_info.current_note = MELODY_REST;
    s_action = 0U;
    s_exit = false;
    s_active = true;
}

void melody_maker_leave(void)
{
    if (!s_active) { return; }
    stop_voice();
    Audio_AllOff();
    Audio_SetTimbre(s_saved_timbre);
    Audio_SetOctave(s_saved_octave);
    (void)Audio_SetTranspose(s_saved_transpose);
    bsp_notes_init();
    if (s_saved_metro) { Metronome_Start(); }
    s_active = false;
    s_exit = false;
    s_info.state = MELODY_SELECT;
}

static void draw_action(uint8_t i)
{
    uint16_t bg = (s_action == i) ? TFT_COLOR_ORANGE_FILL : TFT_COLOR_BG;
    tft_fill_rect(s_action_x[i], 144, s_action_w[i], 15, bg);
    tft_draw_rect(s_action_x[i], 144, s_action_w[i], 15,
                  (s_action == i) ? TFT_COLOR_ORANGE : TFT_COLOR_CYAN_DARK);
    tft_draw_text_ascii_5x7((int16_t)(s_action_x[i] + 2), 148, s_actions[i], 1, TFT_COLOR_WHITE, bg);
}

static void draw_bars(void)
{
    tft_fill_rect(3, 62, 122, 49, TFT_COLOR_BG);
    for (uint8_t i = 0U; i < 7U; ++i) {
        int16_t x = (int16_t)(5 + 17 * i);
        int16_t h = (int16_t)(10 + 4 * i);
        bool on = (s_info.current_note == i);
        tft_fill_rect(x, (int16_t)(99 - h), 13, h, on ? TFT_COLOR_ORANGE : TFT_COLOR_PANEL);
        tft_draw_rect(x, (int16_t)(99 - h), 13, h, on ? TFT_COLOR_ORANGE : TFT_COLOR_CYAN_DARK);
        tft_draw_text_ascii_5x7(x, 103, s_note_names[i], 1, on ? TFT_COLOR_WHITE : TFT_COLOR_GRAY, TFT_COLOR_BG);
    }
}

static void draw_dynamic(void)
{
    uint8_t progress = (uint8_t)(s_info.elapsed_ms * 112U / s_info.total_ms);
    uint32_t seconds = (s_info.state == MELODY_FINISHED) ?
                       (s_info.total_ms + 999U) / 1000U : s_info.elapsed_ms / 1000U;
    if (s_draw_note != s_info.current_note) { draw_bars(); s_draw_note = s_info.current_note; }
    if (s_draw_state != s_info.state) {
        tft_fill_rect(3, 113, 122, 9, TFT_COLOR_BG);
        const char *state = (s_info.state == MELODY_FINISHED) ? "FINISHED" : "NOW PLAYING";
        tft_draw_text_ascii_5x7(31, 114, state, 1, TFT_COLOR_WHITE, TFT_COLOR_BG);
        s_draw_state = s_info.state;
    }
    if (s_draw_progress != progress) {
        tft_draw_rect(6, 125, 116, 6, TFT_COLOR_CYAN);
        tft_fill_rect(8, 127, 112, 2, TFT_COLOR_PANEL);
        if (progress != 0U) { tft_fill_rect(8, 127, progress, 2, TFT_COLOR_CYAN); }
        s_draw_progress = progress;
    }
    if (s_draw_seconds != seconds) {
        char text[24];
        (void)snprintf(text, sizeof(text), "%02lu / %02lus", (unsigned long)seconds,
                       (unsigned long)((s_info.total_ms + 999U) / 1000U));
        tft_fill_rect(26, 134, 80, 8, TFT_COLOR_BG);
        tft_draw_text_ascii_5x7(34, 134, text, 1, TFT_COLOR_WHITE, TFT_COLOR_BG);
        s_draw_seconds = seconds;
    }
}

static void draw_selection(uint8_t i)
{
    int16_t y = (int16_t)(27 + 18 * i);
    uint16_t bg = (s_info.timbre == i) ? TFT_COLOR_ORANGE_FILL : TFT_COLOR_BG;
    tft_fill_round_rect(8, y, 112, 16, bg);
    if (s_info.timbre == i) { tft_draw_round_rect(8, y, 112, 16, TFT_COLOR_ORANGE); }
    tft_draw_text_ascii_5x7(16, (int16_t)(y + 4), s_names[i], 1,
                            s_info.timbre == i ? TFT_COLOR_WHITE : TFT_COLOR_CYAN, bg);
}

void melody_maker_draw(void)
{
    tft_set_y_offset(0);
    tft_fill_screen(TFT_COLOR_BG);
    tft_draw_text_ascii_5x7(28, 6, "MELODY MAKER", 1, TFT_COLOR_WHITE, TFT_COLOR_BG);
    tft_draw_hline(3, 18, 122, TFT_COLOR_CYAN);
    if (s_info.state == MELODY_SELECT) {
        for (uint8_t i = 0U; i < TIMBRE_COUNT; ++i) { draw_selection(i); }
        tft_draw_text_ascii_5x7(22, 130, "ROTATE: SELECT", 1, TFT_COLOR_GRAY, TFT_COLOR_BG);
        tft_draw_text_ascii_5x7(22, 145, "PUSH: GENERATE", 1, TFT_COLOR_WHITE, TFT_COLOR_BG);
    } else {
        char bpm[10];
        (void)snprintf(bpm, sizeof(bpm), "%uBPM", (unsigned)s_info.bpm);
        tft_draw_text_ascii_5x7(6, 27, s_names[s_info.timbre], 1, TFT_COLOR_WHITE, TFT_COLOR_BG);
        tft_draw_text_ascii_5x7(84, 27, bpm, 1, TFT_COLOR_CYAN, TFT_COLOR_BG);
        char style[24];
        (void)snprintf(style, sizeof(style), "RANDOM STYLE: %s", s_styles[s_info.style]);
        tft_draw_text_ascii_5x7(6, 45, style, 1, TFT_COLOR_CYAN, TFT_COLOR_BG);
        s_draw_note = 0xFEU;
        s_draw_progress = s_draw_state = 0xFFU;
        s_draw_seconds = UINT32_MAX;
        draw_dynamic();
        for (uint8_t i = 0U; i < 3U; ++i) { draw_action(i); }
    }
}

void melody_maker_handle_event(bsp_encoder_event_t event)
{
    if (!s_active) { return; }
    if (s_info.state == MELODY_SELECT) {
        if (event == BSP_ENCODER_EVENT_CW || event == BSP_ENCODER_EVENT_CCW) {
            uint8_t previous = s_info.timbre;
            s_info.timbre = (uint8_t)((s_info.timbre + (event == BSP_ENCODER_EVENT_CW ? 1U : TIMBRE_COUNT - 1U)) % TIMBRE_COUNT);
            draw_selection(previous);
            draw_selection(s_info.timbre);
        } else if (event == BSP_ENCODER_EVENT_SHORT) {
            uint32_t seed = HAL_GetTick() ^ (++s_generation * 0x9E3779B9UL);
            if (melody_maker_generate((TimbreId)s_info.timbre, seed)) {
                s_action = 0U;
                s_info.state = MELODY_PLAYING;
                melody_maker_draw();
                (void)melody_maker_replay(HAL_GetTick());
            }
        } else if (event == BSP_ENCODER_EVENT_LONG) { s_exit = true; }
        return;
    }
    if (event == BSP_ENCODER_EVENT_CW || event == BSP_ENCODER_EVENT_CCW) {
        uint8_t previous = s_action;
        s_action = (uint8_t)((s_action + (event == BSP_ENCODER_EVENT_CW ? 1U : 2U)) % 3U);
        draw_action(previous);
        draw_action(s_action);
    } else if (event == BSP_ENCODER_EVENT_LONG || (event == BSP_ENCODER_EVENT_SHORT && s_action == 2U)) {
        stop_voice();
        s_info.state = MELODY_SELECT;
        melody_maker_draw();
    } else if (event == BSP_ENCODER_EVENT_SHORT) {
        if (s_action == 1U) {
            uint32_t seed = HAL_GetTick() ^ (++s_generation * 0x9E3779B9UL);
            (void)melody_maker_generate((TimbreId)s_info.timbre, seed);
            s_info.state = MELODY_PLAYING;
            melody_maker_draw();
        }
        (void)melody_maker_replay(HAL_GetTick());
    }
}

bool melody_maker_tick(uint32_t now_ms)
{
    if (!s_active) { return false; }
    if (s_exit) { return true; }
    if (s_info.state == MELODY_SELECT) { return false; }
    uint32_t elapsed = now_ms - s_begin;
    if (s_info.state == MELODY_PLAYING) {
        s_info.elapsed_ms = elapsed < s_info.total_ms ? elapsed : s_info.total_ms;
        if (s_voice >= 0 && (!Audio_IsVoiceActive(s_voice) ||
            elapsed >= s_events[s_next - 1U].start_ms + s_events[s_next - 1U].duration_ms)) {
            stop_voice();
        }
        while (s_next < s_info.count && s_events[s_next].start_ms <= elapsed) {
            const melody_event_t *e = &s_events[s_next++];
            if (e->note != MELODY_REST && elapsed < e->start_ms + e->duration_ms) {
                uint16_t hz = Audio_GetFreqOfNote(e->note, (OctaveId)e->octave, 0);
                if (Audio_NoteOnPitch(hz, &s_voice) == AUDIO_OK) { s_info.current_note = e->note; }
            }
        }
        if (elapsed >= s_info.total_ms) { stop_voice(); s_info.state = MELODY_FINISHED; }
    }
    if ((uint32_t)(now_ms - s_draw_ms) >= 40U) { s_draw_ms = now_ms; draw_dynamic(); }
    return false;
}
