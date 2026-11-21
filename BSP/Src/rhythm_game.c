/* ============================================================================
 *  rhythm_game.c —— 七轨下落式钢琴小游戏
 *
 *  设计要点
 *   - 游戏时间只由 HAL_GetTick 推导，不按帧累加；掉帧不会让节奏漂移。
 *   - 声音与判定分离：按键发声仍由 bsp_notes 调 Audio_NoteOn/Off 完成，
 *     本模块只在按键回调里做判定（错键也照常发声）。
 *   - 动画局部刷新：只擦/画可见音符、按键闪光和变化了的文字，不整屏重绘。
 *   - 不新增帧缓冲、不占新定时器、不改音频采样/PWM/录音 Flash 布局。
 * ========================================================================== */

#include "rhythm_game.h"
#include "rhythm_chart.h"

#include "bsp_tft.h"
#include "bsp_notes.h"
#include "bsp_audio_tim.h"
#include "audio.h"
#include "metronome.h"
#include "song.h"
#include "record.h"
#include "stm32f1xx_hal.h"

#include <stddef.h>

/* ---------------------------------------------------------------- 布局/参数 */
#define RG_HEADER_H        18
#define RG_TRACK_TOP       22
#define RG_TRACK_PITCH     18
#define RG_TRACK_W         16
#define RG_NOTE_W          14
#define RG_NOTE_H          6
#define RG_JUDGE_Y         94
#define RG_LANE_COUNT      7

#define RG_FALL_MS         1500U   /* 音符从顶部落到判定线的时间 */
#define RG_FALL_PX         68      /* 顶到判定线的像素距离 */
#define RG_VISIBLE_PAST_MS 60      /* 越过判定线后还画多久（判定窗口仍是 200） */

#define RG_PERFECT_MS      60
#define RG_GREAT_MS        120
#define RG_GOOD_MS         200

#define RG_SCORE_PERFECT   100U
#define RG_SCORE_GREAT     70U
#define RG_SCORE_GOOD      40U
#define RG_FINAL_MAX       100000UL
#define RG_EXTRA_PENALTY   200UL
#define RG_EXTRA_CAP       10000UL

#define RG_FRAME_MS        50U     /* 20 FPS 动画上限 */
#define RG_COUNTDOWN_MS    2700U   /* 3/2/1/GO 各约 0.8 s */

#define RG_JUDGE_NONE      0U
#define RG_JUDGE_PERFECT   1U
#define RG_JUDGE_GREAT     2U
#define RG_JUDGE_GOOD      3U
#define RG_JUDGE_MISS      4U
#define RG_JUDGE_WRONG     5U

#define RG_HEADER_COLOR    0x0841U

/* 难度只改“游戏时间流速”：低=正常，中=1.25x，高=1.5x。
 * 曲谱/判定窗口仍按真实毫秒算，所以速度越高，单位时间内要处理的音符越多。 */
#define RG_DIFF_COUNT      3U
static const uint16_t s_diff_speed[RG_DIFF_COUNT] = {100U, 125U, 150U};
static const char *const s_diff_name[RG_DIFF_COUNT] = {"LOW", "MED", "HIGH"};
static const char *const s_diff_speed_text[RG_DIFF_COUNT] = {"1.0X", "1.25X", "1.5X"};

/* ---------------------------------------------------------------- 内部状态 */
static bool     s_in_page;
static bool     s_exit_request;
static rhythm_game_state_t s_state;

static uint8_t  s_difficulty;    /* 0=LOW 1=MED 2=HIGH */

static uint8_t  s_note_count;
static uint8_t  s_note_state[RHYTHM_CHART_MAX_NOTES];  /* 0=待判 1=命中 2=MISS */
static int16_t  s_note_y[RHYTHM_CHART_MAX_NOTES];      /* -1 = 当前没画 */

static uint32_t s_chart_ms;      /* 暂停时冻结的曲谱时间 */
static uint32_t s_run_start;     /* 本次开始跑的 tick（chart_ms 的基准） */
static uint32_t s_countdown_end;
static uint32_t s_countdown_step;/* 3/2/1/0(GO)；0xFF = 还没画 */
static bool     s_countdown_resume;

static uint32_t s_points;
static uint16_t s_combo;
static uint16_t s_max_combo;
static uint16_t s_perfect;
static uint16_t s_great;
static uint16_t s_good;
static uint16_t s_miss;
static uint16_t s_extra;
static uint8_t  s_judge;
static uint32_t s_final_score;
static uint8_t  s_grade;

static uint8_t  s_pause_sel;
static uint8_t  s_result_sel;

static uint32_t s_last_draw;
static uint32_t s_time_shown;
static bool     s_score_dirty;
static bool     s_combo_dirty;
static bool     s_judge_dirty;

static uint32_t s_flash_until[RG_LANE_COUNT];
static uint8_t  s_flash_wrong[RG_LANE_COUNT];
static uint8_t  s_flash_drawn[RG_LANE_COUNT];

static uint8_t  s_saved_octave;
static int8_t   s_saved_transpose;
static uint8_t  s_saved_timbre;
static bool     s_metro_was_running;
static uint16_t s_metro_bpm;
static uint8_t  s_metro_beats;

/* ---------------------------------------------------------------- 小工具 */
static uint16_t rg_ascii_len(const char *s)
{
    uint16_t n = 0U;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

static uint16_t rg_ascii_width(const char *s, uint8_t scale)
{
    uint16_t n = rg_ascii_len(s);
    if (n == 0U) {
        return 0U;
    }
    return (uint16_t)(n * 6U * scale - scale);
}

static void rg_text(int16_t x, int16_t y, const char *s, uint8_t scale,
                    uint16_t fg, uint16_t bg)
{
    tft_draw_text_ascii_5x7(x, y, s, scale, fg, bg);
}

static void rg_text_center(int16_t y, const char *s, uint8_t scale,
                           uint16_t fg, uint16_t bg)
{
    uint16_t w = rg_ascii_width(s, scale);
    int16_t x = (int16_t)((128 - (int16_t)w) / 2);
    if (x < 0) {
        x = 0;
    }
    rg_text(x, y, s, scale, fg, bg);
}

/* 无符号十进制，返回位数；buf 至少 11 字节 */
static uint8_t rg_u32_to_dec(uint32_t value, char *buf)
{
    char tmp[11];
    uint8_t n = 0U;
    uint8_t i;
    do {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    for (i = 0U; i < n; ++i) {
        buf[i] = tmp[n - 1U - i];
    }
    buf[n] = '\0';
    return n;
}

static void rg_number(int16_t x, int16_t y, uint32_t value, uint8_t scale,
                      uint16_t fg, uint16_t bg)
{
    char buf[11];
    (void)rg_u32_to_dec(value, buf);
    rg_text(x, y, buf, scale, fg, bg);
}

/* 固定位数，前导零（分数表头用） */
static void rg_number_pad(int16_t x, int16_t y, uint32_t value, uint8_t digits,
                          uint8_t scale, uint16_t fg, uint16_t bg)
{
    char buf[11];
    char out[11];
    uint8_t n = rg_u32_to_dec(value, buf);
    uint8_t i;
    for (i = 0U; i < digits; ++i) {
        uint8_t src = (i + n >= digits) ? (uint8_t)(i + n - digits) : 0xFFU;
        out[i] = (src < n) ? buf[src] : '0';
    }
    out[digits] = '\0';
    rg_text(x, y, out, scale, fg, bg);
}

static void rg_number_center(int16_t y, uint32_t value, uint8_t scale,
                             uint16_t fg, uint16_t bg)
{
    char buf[11];
    (void)rg_u32_to_dec(value, buf);
    rg_text_center(y, buf, scale, fg, bg);
}

static int16_t rg_lane_x(uint8_t lane)
{
    return (int16_t)(1 + (int16_t)lane * RG_TRACK_PITCH);
}

static int16_t rg_note_x(uint8_t lane)
{
    return (int16_t)(rg_lane_x(lane) + 1);
}

static const char *rg_judge_text(uint8_t judge)
{
    switch (judge) {
    case RG_JUDGE_PERFECT: return "PERFECT";
    case RG_JUDGE_GREAT:   return "GREAT";
    case RG_JUDGE_GOOD:    return "GOOD";
    case RG_JUDGE_MISS:    return "MISS";
    case RG_JUDGE_WRONG:   return "WRONG";
    default:               return "";
    }
}

static uint16_t rg_judge_color(uint8_t judge)
{
    switch (judge) {
    case RG_JUDGE_PERFECT: return TFT_COLOR_CYAN;
    case RG_JUDGE_GREAT:   return 0x07E0U;
    case RG_JUDGE_GOOD:    return TFT_COLOR_YELLOW;
    case RG_JUDGE_MISS:    return 0xF800U;
    case RG_JUDGE_WRONG:   return 0xF800U;
    default:               return TFT_COLOR_GRAY;
    }
}

static uint32_t rg_running_score(void)
{
    if (s_note_count == 0U) {
        return 0U;
    }
    return (uint32_t)(((uint64_t)s_points * (uint64_t)RG_FINAL_MAX) /
                      ((uint32_t)s_note_count * 100U));
}

static uint32_t rg_final_score(void)
{
    uint32_t base = rg_running_score();
    uint32_t penalty = (uint32_t)s_extra * RG_EXTRA_PENALTY;
    if (penalty > RG_EXTRA_CAP) {
        penalty = RG_EXTRA_CAP;
    }
    return (base > penalty) ? (base - penalty) : 0U;
}

static uint8_t rg_grade_of(uint32_t score)
{
    if (score >= 95000U) return (uint8_t)RHYTHM_GRADE_S;
    if (score >= 85000U) return (uint8_t)RHYTHM_GRADE_A;
    if (score >= 70000U) return (uint8_t)RHYTHM_GRADE_B;
    if (score >= 50000U) return (uint8_t)RHYTHM_GRADE_C;
    return (uint8_t)RHYTHM_GRADE_D;
}

static char rg_grade_char(uint8_t grade)
{
    static const char s_grade_chars[] = "SABCD";
    if (grade > (uint8_t)RHYTHM_GRADE_D) {
        return 'D';
    }
    return s_grade_chars[grade];
}

static uint16_t rg_grade_color(uint8_t grade)
{
    switch (grade) {
    case RHYTHM_GRADE_S: return TFT_COLOR_CYAN;
    case RHYTHM_GRADE_A: return 0x07E0U;
    case RHYTHM_GRADE_B: return TFT_COLOR_YELLOW;
    case RHYTHM_GRADE_C: return TFT_COLOR_ORANGE;
    default:             return 0xF800U;
    }
}

static uint32_t rg_speed_pct(void)
{
    return (uint32_t)s_diff_speed[s_difficulty % RG_DIFF_COUNT];
}

static uint32_t rg_chart_now(uint32_t now_ms)
{
    /* 暂停/倒计时/结算时曲谱时间冻结；只有 PLAY 才把 now 的增量按难度倍率加进去。 */
    if (s_state != RHYTHM_STATE_PLAY) {
        return s_chart_ms;
    }
    return s_chart_ms + ((uint32_t)(now_ms - s_run_start) * rg_speed_pct()) / 100U;
}

/* ---------------------------------------------------------------- 重置 */
static void rg_reset_game(void)
{
    uint8_t i;

    s_note_count = (uint8_t)((g_rhythm_chart.count > RHYTHM_CHART_MAX_NOTES)
                                 ? RHYTHM_CHART_MAX_NOTES
                                 : g_rhythm_chart.count);
    for (i = 0U; i < RHYTHM_CHART_MAX_NOTES; ++i) {
        s_note_state[i] = 0U;
        s_note_y[i] = -1;
    }
    for (i = 0U; i < RG_LANE_COUNT; ++i) {
        s_flash_until[i] = 0U;
        s_flash_wrong[i] = 0U;
        s_flash_drawn[i] = 0U;
    }

    s_chart_ms = 0U;
    s_points = 0U;
    s_combo = 0U;
    s_max_combo = 0U;
    s_perfect = 0U;
    s_great = 0U;
    s_good = 0U;
    s_miss = 0U;
    s_extra = 0U;
    s_judge = RG_JUDGE_NONE;
    s_final_score = 0U;
    s_grade = (uint8_t)RHYTHM_GRADE_D;
    s_time_shown = 0xFFFFFFFFU;
}

/* ---------------------------------------------------------------- 首页 */
static void rg_draw_home(void)
{
    char diff_line[20];
    char speed_line[16];
    char about[20];
    char digits[11];
    const char *p;
    uint8_t i;
    uint8_t n;
    uint32_t speed = rg_speed_pct();

    tft_fill_screen(TFT_COLOR_BG);

    rg_text_center(12, "RHYTHM", 3, TFT_COLOR_CYAN, TFT_COLOR_BG);
    rg_text_center(42, "GAME", 2, TFT_COLOR_WHITE, TFT_COLOR_BG);
    rg_text_center(64, g_rhythm_chart.title, 1, TFT_COLOR_TEXT, TFT_COLOR_BG);
    rg_text_center(76, g_rhythm_chart.subtitle, 1, TFT_COLOR_GRAY, TFT_COLOR_BG);

    /* 难度行：旋转编码器循环 LOW/MED/HIGH */
    i = 0U;
    p = "DIFFICULTY ";
    while (*p != '\0') { diff_line[i++] = *p++; }
    p = s_diff_name[s_difficulty % RG_DIFF_COUNT];
    while (*p != '\0') { diff_line[i++] = *p++; }
    diff_line[i] = '\0';
    rg_text_center(88, diff_line, 1, TFT_COLOR_WHITE, TFT_COLOR_BG);
    /* 左右小箭头：提示这一行可以用旋钮左右切换，不用再猜 */
    tft_draw_line(12, 88, 8, 91, TFT_COLOR_CYAN);
    tft_draw_line(12, 94, 8, 91, TFT_COLOR_CYAN);
    tft_draw_line(116, 88, 120, 91, TFT_COLOR_CYAN);
    tft_draw_line(116, 94, 120, 91, TFT_COLOR_CYAN);

    i = 0U;
    p = "SPEED ";
    while (*p != '\0') { speed_line[i++] = *p++; }
    p = s_diff_speed_text[s_difficulty % RG_DIFF_COUNT];
    while (*p != '\0') { speed_line[i++] = *p++; }
    speed_line[i] = '\0';
    rg_text_center(98, speed_line, 1, TFT_COLOR_CYAN, TFT_COLOR_BG);

    /* 时长按当前难度换算：低 30 s / 中 24 s / 高 20 s */
    i = 0U;
    p = "ABOUT ";
    while (*p != '\0') { about[i++] = *p++; }
    n = rg_u32_to_dec((g_rhythm_chart.duration_ms * 100U + speed / 2U) /
                          speed / 1000U, digits);
    for (uint8_t j = 0U; j < n; ++j) { about[i++] = digits[j]; }
    p = " SEC";
    while (*p != '\0') { about[i++] = *p++; }
    about[i] = '\0';
    rg_text_center(108, about, 1, TFT_COLOR_GRAY, TFT_COLOR_BG);

    tft_fill_round_rect(34, 118, 60, 20, TFT_COLOR_ORANGE_FILL);
    tft_draw_round_rect(34, 118, 60, 20, TFT_COLOR_ORANGE);
    rg_text_center(122, "START", 2, TFT_COLOR_WHITE, TFT_COLOR_ORANGE_FILL);
    rg_text_center(150, "HOLD=BACK", 1, TFT_COLOR_GRAY, TFT_COLOR_BG);
}

/* ---------------------------------------------------------------- 倒计时 */
static void rg_draw_countdown_screen(void)
{
    tft_fill_screen(TFT_COLOR_BG);
    rg_text_center(14, "RHYTHM", 1, TFT_COLOR_TEXT, TFT_COLOR_BG);
    rg_text_center(30, s_countdown_resume ? "RESUME" : "GET READY",
                   1, TFT_COLOR_CYAN, TFT_COLOR_BG);
    tft_fill_round_rect(36, 56, 56, 52, TFT_COLOR_PANEL);
    tft_draw_round_rect(36, 56, 56, 52, TFT_COLOR_CYAN);
    rg_text_center(112, "PIANO", 1, TFT_COLOR_GRAY, TFT_COLOR_BG);
}

static void rg_draw_countdown_step(uint32_t step)
{
    tft_fill_round_rect(38, 58, 52, 48, TFT_COLOR_PANEL);
    if (step >= 1U) {
        char digit[2];
        digit[0] = (char)('0' + step);
        digit[1] = '\0';
        tft_draw_text_font(&font24, 52, 70, digit, TFT_COLOR_WHITE, TFT_COLOR_PANEL);
    } else {
        rg_text_center(70, "GO", 4, TFT_COLOR_YELLOW, TFT_COLOR_PANEL);
    }
}

/* ---------------------------------------------------------------- 游戏页 */
static void rg_draw_score(void)
{
    tft_fill_rect(76, 4, 40, 10, RG_HEADER_COLOR);
    rg_number_pad(76, 5, rg_running_score(), 6U, 1U,
                  TFT_COLOR_WHITE, RG_HEADER_COLOR);
}

static void rg_draw_combo(void)
{
    tft_fill_rect(44, 111, 50, 9, TFT_COLOR_BG);
    rg_number(44, 112, s_combo, 1U, TFT_COLOR_WHITE, TFT_COLOR_BG);
}

static void rg_draw_judge(void)
{
    tft_fill_rect(0, 120, 128, 16, TFT_COLOR_BG);
    if (s_judge != RG_JUDGE_NONE) {
        rg_text_center(122, rg_judge_text(s_judge), 2U,
                       rg_judge_color(s_judge), TFT_COLOR_BG);
    }
}

static void rg_draw_time(uint32_t chart_ms)
{
    uint32_t left = (g_rhythm_chart.duration_ms > chart_ms)
                        ? (g_rhythm_chart.duration_ms - chart_ms)
                        : 0U;
    uint32_t sec = (left + 999U) / 1000U;
    char buf[3];

    if (sec > 99U) {
        sec = 99U;
    }
    buf[0] = (char)('0' + (sec / 10U));
    buf[1] = (char)('0' + (sec % 10U));
    buf[2] = '\0';

    tft_fill_rect(112, 145, 16, 9, TFT_COLOR_BG);
    rg_text(112, 146, buf, 1U, TFT_COLOR_GRAY, TFT_COLOR_BG);
    s_time_shown = sec;
}

static void rg_draw_play_static(void)
{
    uint8_t i;

    tft_fill_screen(TFT_COLOR_BG);

    tft_fill_rect(0, 0, 128, RG_HEADER_H, RG_HEADER_COLOR);
    rg_text(2, 5, "RHYTHM", 1U, TFT_COLOR_WHITE, RG_HEADER_COLOR);
    rg_text(44, 5, "SCORE", 1U, TFT_COLOR_CYAN, RG_HEADER_COLOR);
    tft_draw_hline(0, RG_HEADER_H, 128, TFT_COLOR_CYAN);

    /* 七条轨道：只画边框，内部留黑，音符局部擦画 */
    for (i = 0U; i < RG_LANE_COUNT; ++i) {
        tft_draw_rect(rg_lane_x(i), RG_TRACK_TOP, RG_TRACK_W,
                      (int16_t)(RG_JUDGE_Y - RG_TRACK_TOP - 1U), TFT_COLOR_DIM);
        {
            char digit[2];
            digit[0] = (char)('1' + i);
            digit[1] = '\0';
            rg_text((int16_t)(rg_lane_x(i) + 5), 100, digit, 1U,
                    TFT_COLOR_GRAY, TFT_COLOR_BG);
        }
        s_flash_until[i] = 0U;
        s_flash_drawn[i] = 0U;
    }
    tft_draw_hline(1, RG_JUDGE_Y, 126, TFT_COLOR_CYAN);

    rg_text(2, 112, "COMBO", 1U, TFT_COLOR_GRAY, TFT_COLOR_BG);
    rg_text(84, 146, "TIME", 1U, TFT_COLOR_GRAY, TFT_COLOR_BG);

    for (i = 0U; i < RHYTHM_CHART_MAX_NOTES; ++i) {
        s_note_y[i] = -1;
    }

    s_score_dirty = false;
    s_combo_dirty = false;
    s_judge_dirty = false;
    rg_draw_score();
    rg_draw_combo();
    rg_draw_judge();
    rg_draw_time(rg_chart_now(HAL_GetTick()));
}

static void rg_draw_play_dynamic(uint32_t now_ms, uint32_t chart_ms)
{
    uint8_t i;

    for (i = 0U; i < s_note_count; ++i) {
        uint8_t track = g_rhythm_chart.notes[i].track;
        int32_t delta = (int32_t)g_rhythm_chart.notes[i].time_ms - (int32_t)chart_ms;
        bool visible = (s_note_state[i] == 0U) &&
                       (delta <= (int32_t)RG_FALL_MS) &&
                       (delta >= -(int32_t)RG_VISIBLE_PAST_MS) &&
                       (track < RG_LANE_COUNT);
        int16_t y = -1;

        if (track >= RG_LANE_COUNT) {
            continue;
        }

        if (visible) {
            y = (int16_t)((int32_t)RG_JUDGE_Y -
                          (delta * (int32_t)RG_FALL_PX) / (int32_t)RG_FALL_MS);
        }

        if (s_note_y[i] >= 0) {
            int16_t old_y = s_note_y[i];
            tft_fill_rect(rg_note_x(track),
                          (int16_t)(old_y - 3), RG_NOTE_W, RG_NOTE_H, TFT_COLOR_BG);
            if (((int16_t)(old_y - 3) <= (int16_t)RG_JUDGE_Y) &&
                ((int16_t)(old_y + 2) >= (int16_t)RG_JUDGE_Y)) {
                tft_draw_hline(rg_note_x(track),
                               RG_JUDGE_Y, RG_NOTE_W, TFT_COLOR_CYAN);
            }
            /* 音符压过轨道底边时，把被擦掉的 1 px 边框补回来 */
            if (((int16_t)(old_y - 3) <= (int16_t)(RG_JUDGE_Y - 2)) &&
                ((int16_t)(old_y + 2) >= (int16_t)(RG_JUDGE_Y - 2))) {
                tft_draw_hline(rg_note_x(track), (int16_t)(RG_JUDGE_Y - 2),
                               RG_NOTE_W, TFT_COLOR_DIM);
            }
            s_note_y[i] = -1;
        }

        if (visible && y >= 0) {
            uint16_t color = ((delta <= (int32_t)RG_GOOD_MS) &&
                              (delta >= -(int32_t)RG_GOOD_MS))
                                 ? TFT_COLOR_ORANGE
                                 : TFT_COLOR_CYAN;
            tft_fill_rect(rg_note_x(track),
                          (int16_t)(y - 3), RG_NOTE_W, RG_NOTE_H, color);
            s_note_y[i] = y;
        }
    }

    /* 按键闪光（判定线下面 2 px，不挡数字） */
    for (i = 0U; i < RG_LANE_COUNT; ++i) {
        if (s_flash_until[i] == 0U) {
            continue;
        }
        if ((int32_t)(now_ms - s_flash_until[i]) >= 0) {
            if (s_flash_drawn[i]) {
                tft_fill_rect(rg_lane_x(i), RG_JUDGE_Y + 2, RG_TRACK_W, 2,
                              TFT_COLOR_BG);
                s_flash_drawn[i] = 0U;
            }
            s_flash_until[i] = 0U;
        } else if (!s_flash_drawn[i]) {
            tft_fill_rect(rg_lane_x(i), RG_JUDGE_Y + 2, RG_TRACK_W, 2,
                          s_flash_wrong[i] ? 0xF800U : TFT_COLOR_YELLOW);
            s_flash_drawn[i] = 1U;
        }
    }

    if (s_score_dirty) {
        s_score_dirty = false;
        rg_draw_score();
    }
    if (s_combo_dirty) {
        s_combo_dirty = false;
        rg_draw_combo();
    }
    if (s_judge_dirty) {
        s_judge_dirty = false;
        rg_draw_judge();
    }

    {
        uint32_t left = (g_rhythm_chart.duration_ms > chart_ms)
                            ? (g_rhythm_chart.duration_ms - chart_ms)
                            : 0U;
        uint32_t sec = (left + 999U) / 1000U;
        if (sec != s_time_shown) {
            rg_draw_time(chart_ms);
        }
    }
}

/* ---------------------------------------------------------------- 暂停 */
static void rg_draw_pause_overlay(void)
{
    static const char *const s_opts[3] = {"CONTINUE", "RESTART", "EXIT"};
    uint8_t i;

    /* 先清掉面板边缘会露出来的 COMBO / 判定残字，避免暂停层出现半截文字 */
    tft_fill_rect(0, 111, 128, 9, TFT_COLOR_BG);
    tft_fill_rect(0, 120, 128, 16, TFT_COLOR_BG);

    tft_fill_round_rect(14, 40, 100, 88, TFT_COLOR_PANEL);
    tft_draw_round_rect(14, 40, 100, 88, TFT_COLOR_CYAN);
    rg_text_center(44, "PAUSED", 2U, TFT_COLOR_WHITE, TFT_COLOR_PANEL);

    for (i = 0U; i < 3U; ++i) {
        int16_t y = (int16_t)(64 + i * 16);
        uint16_t color = (i == s_pause_sel) ? TFT_COLOR_ORANGE : TFT_COLOR_TEXT;
        rg_text_center(y, s_opts[i], 1U, color, TFT_COLOR_PANEL);
        if (i == s_pause_sel) {
            uint16_t w = rg_ascii_width(s_opts[i], 1U);
            int16_t x = (int16_t)((128 - (int16_t)w) / 2);
            tft_fill_rect(x, (int16_t)(y + 8), w, 1, TFT_COLOR_ORANGE);
        }
    }
    rg_text_center(114, "HOLD=BACK", 1U, TFT_COLOR_GRAY, TFT_COLOR_PANEL);
}

/* ---------------------------------------------------------------- 结算 */
static void rg_draw_result_options(void)
{
    uint16_t c_retry = (s_result_sel == 0U) ? TFT_COLOR_ORANGE : TFT_COLOR_GRAY;
    uint16_t c_back  = (s_result_sel == 1U) ? TFT_COLOR_ORANGE : TFT_COLOR_GRAY;

    tft_fill_rect(0, 138, 128, 16, TFT_COLOR_BG);
    rg_text(16, 140, "RETRY", 1U, c_retry, TFT_COLOR_BG);
    rg_text(80, 140, "BACK", 1U, c_back, TFT_COLOR_BG);
    if (s_result_sel == 0U) {
        tft_fill_rect(16, 148, 30, 1, TFT_COLOR_ORANGE);
    } else {
        tft_fill_rect(80, 148, 24, 1, TFT_COLOR_ORANGE);
    }
}

static void rg_draw_count_line(int16_t y, const char *label, uint16_t value)
{
    rg_text(8, y, label, 1U, TFT_COLOR_TEXT, TFT_COLOR_BG);
    rg_number(84, y, value, 1U, TFT_COLOR_WHITE, TFT_COLOR_BG);
}

static void rg_draw_result(void)
{
    char grade[2];

    tft_fill_screen(TFT_COLOR_BG);
    rg_text_center(4, "COMPLETE", 2U, TFT_COLOR_CYAN, TFT_COLOR_BG);
    rg_text_center(18, g_rhythm_chart.title, 1U, TFT_COLOR_TEXT, TFT_COLOR_BG);
    rg_text_center(28, "SCORE", 1U, TFT_COLOR_GRAY, TFT_COLOR_BG);
    rg_number_center(38, s_final_score, 3U, TFT_COLOR_WHITE, TFT_COLOR_BG);

    grade[0] = rg_grade_char(s_grade);
    grade[1] = '\0';
    rg_text_center(60, grade, 4U, rg_grade_color(s_grade), TFT_COLOR_BG);

    rg_draw_count_line(92, "PERFECT", s_perfect);
    rg_draw_count_line(100, "GREAT", s_great);
    rg_draw_count_line(108, "GOOD", s_good);
    rg_draw_count_line(116, "MISS", s_miss);
    rg_draw_count_line(124, "MAX COMBO", s_max_combo);

    rg_draw_result_options();
}

/* ---------------------------------------------------------------- 状态切换 */
static void rg_start_countdown(uint32_t now_ms, bool resume)
{
    if (!resume) {
        rg_reset_game();
    }
    s_state = RHYTHM_STATE_COUNTDOWN;
    s_countdown_resume = resume;
    s_countdown_end = now_ms + RG_COUNTDOWN_MS;
    s_countdown_step = 0xFFU;
    s_chart_ms = resume ? s_chart_ms : 0U;
    rg_draw_countdown_screen();
    /* 立刻画出第一个数字并响一声 */
    s_countdown_step = 3U;
    rg_draw_countdown_step(3U);
    AudioMetroBeep_PlayGain(1000U, 40U, 3U);
}

static void rg_update_countdown(uint32_t now_ms)
{
    uint32_t remaining;
    uint32_t step;

    if ((int32_t)(now_ms - s_countdown_end) >= 0) {
        if (!s_countdown_resume) {
            s_chart_ms = 0U;
        }
        s_run_start = now_ms;
        s_state = RHYTHM_STATE_PLAY;
        s_last_draw = now_ms - RG_FRAME_MS;   /* 下一帧立刻画 */
        s_time_shown = 0xFFFFFFFFU;
        rg_draw_play_static();
        return;
    }

    remaining = s_countdown_end - now_ms;
    if (remaining > 1900U) {
        step = 3U;
    } else if (remaining > 1100U) {
        step = 2U;
    } else if (remaining > 300U) {
        step = 1U;
    } else {
        step = 0U;
    }

    if (step != s_countdown_step) {
        s_countdown_step = step;
        rg_draw_countdown_step(step);
        AudioMetroBeep_PlayGain((step == 0U) ? 1500U : 1000U,
                                (step == 0U) ? 60U : 40U, 3U);
    }
}

static void rg_process_misses(uint32_t chart_ms)
{
    uint8_t i;
    bool any = false;

    for (i = 0U; i < s_note_count; ++i) {
        if (s_note_state[i] != 0U) {
            continue;
        }
        if ((int32_t)(chart_ms - g_rhythm_chart.notes[i].time_ms) >
            (int32_t)(RG_GOOD_MS * rg_speed_pct() / 100U)) {
            s_note_state[i] = 2U;
            ++s_miss;
            s_combo = 0U;
            s_judge = RG_JUDGE_MISS;
            any = true;
        }
    }
    if (any) {
        s_combo_dirty = true;
        s_judge_dirty = true;
    }
}

static void rg_finish_game(void)
{
    s_final_score = rg_final_score();
    s_grade = rg_grade_of(s_final_score);
    s_state = RHYTHM_STATE_RESULT;
    s_result_sel = 0U;
    Audio_AllOff();
    AudioMetroBeep_Stop();
    rg_draw_result();
}

static void rg_enter_pause(uint32_t now_ms)
{
    s_chart_ms = rg_chart_now(now_ms);
    s_state = RHYTHM_STATE_PAUSE;
    s_pause_sel = 0U;
    Audio_AllOff();
    AudioMetroBeep_Stop();
    rg_draw_pause_overlay();
}

static void rg_resume_from_pause(uint32_t now_ms)
{
    s_state = RHYTHM_STATE_COUNTDOWN;
    s_countdown_resume = true;
    s_countdown_end = now_ms + RG_COUNTDOWN_MS;
    s_countdown_step = 0xFFU;
    rg_draw_countdown_screen();
    s_countdown_step = 3U;
    rg_draw_countdown_step(3U);
    AudioMetroBeep_PlayGain(1000U, 40U, 3U);
}

static void rg_restart(uint32_t now_ms)
{
    rg_reset_game();
    rg_start_countdown(now_ms, false);
}

/* ---------------------------------------------------------------- 判定 */
static void rg_judge_press(uint8_t lane, uint32_t now_ms)
{
    uint32_t chart_ms = rg_chart_now(now_ms);
    uint32_t speed = rg_speed_pct();
    int32_t good_chart = (int32_t)(RG_GOOD_MS * speed / 100U);
    int16_t best = -1;
    int32_t best_abs = good_chart + 1;
    uint8_t i;

    for (i = 0U; i < s_note_count; ++i) {
        int32_t dt;
        int32_t ad;

        if ((s_note_state[i] != 0U) || (g_rhythm_chart.notes[i].track != lane)) {
            continue;
        }
        dt = (int32_t)g_rhythm_chart.notes[i].time_ms - (int32_t)chart_ms;
        ad = (dt < 0) ? -dt : dt;
        if ((ad <= good_chart) && (ad < best_abs)) {
            best = (int16_t)i;
            best_abs = ad;
        }
    }

    s_flash_until[lane] = now_ms + 80U;
    s_flash_wrong[lane] = (best < 0) ? 1U : 0U;

    if (best >= 0) {
        uint32_t pts;
        uint32_t ad_real = ((uint32_t)best_abs * 100U) / speed;
        if (ad_real <= RG_PERFECT_MS) {
            pts = RG_SCORE_PERFECT;
            s_judge = RG_JUDGE_PERFECT;
            ++s_perfect;
        } else if (ad_real <= RG_GREAT_MS) {
            pts = RG_SCORE_GREAT;
            s_judge = RG_JUDGE_GREAT;
            ++s_great;
        } else {
            pts = RG_SCORE_GOOD;
            s_judge = RG_JUDGE_GOOD;
            ++s_good;
        }
        s_points += pts;
        s_note_state[best] = 1U;
        ++s_combo;
        if (s_combo > s_max_combo) {
            s_max_combo = s_combo;
        }
    } else {
        /* 错键/空按：照样发声（由 bsp_notes 完成），这里只扣分与断连击。 */
        ++s_extra;
        s_combo = 0U;
        s_judge = RG_JUDGE_WRONG;
    }

    s_score_dirty = true;
    s_combo_dirty = true;
    s_judge_dirty = true;
}

/* ---------------------------------------------------------------- 对外接口 */
void rhythm_game_init(void)
{
    bsp_notes_set_event_cb(rhythm_game_on_key);
    s_in_page = false;
    s_state = RHYTHM_STATE_HOME;
    s_note_count = 0U;
    s_exit_request = false;
}

void rhythm_game_enter(void)
{
    s_saved_octave = (uint8_t)Audio_GetOctave();
    s_saved_transpose = Audio_GetTranspose();
    s_saved_timbre = (uint8_t)Audio_GetTimbre();

    s_metro_was_running = Metronome_IsRunning();
    s_metro_bpm = Metronome_GetBpm();
    s_metro_beats = Metronome_GetBeatsPerBar();

    Song_Stop();
    record_cancel();
    record_playback_stop();
    Metronome_Stop();
    Audio_AllOff();
    AudioMetroBeep_Stop();

    /* 游戏固定：中音区 C4~B4、不移调、PIANO 音色；退出时恢复。 */
    Audio_SetTranspose(0);
    Audio_SetOctave(OCT_MID);
    Audio_SetTimbre(TIMBRE_PIANO);

    s_difficulty = 0U;          /* 每次进游戏从 LOW 开始；重开保留当前难度 */
    rg_reset_game();

    s_state = RHYTHM_STATE_HOME;
    s_exit_request = false;
    s_in_page = true;
    s_last_draw = HAL_GetTick();
    bsp_notes_set_event_cb(rhythm_game_on_key);
}

void rhythm_game_leave(void)
{
    Audio_AllOff();
    AudioMetroBeep_Stop();

    Audio_SetOctave((OctaveId)s_saved_octave);
    Audio_SetTranspose(s_saved_transpose);
    Audio_SetTimbre((TimbreId)s_saved_timbre);

    if (s_metro_was_running) {
        (void)Metronome_SetBpm(s_metro_bpm);
        (void)Metronome_SetBeatsPerBar(s_metro_beats);
        Metronome_Start();
    }

    s_in_page = false;
    s_state = RHYTHM_STATE_HOME;
}

bool rhythm_game_tick(uint32_t now_ms)
{
    if (!s_in_page) {
        return false;
    }
    if (s_exit_request) {
        return true;
    }

    switch (s_state) {
    case RHYTHM_STATE_COUNTDOWN:
        rg_update_countdown(now_ms);
        break;

    case RHYTHM_STATE_PLAY: {
        uint32_t chart_ms = rg_chart_now(now_ms);
        rg_process_misses(chart_ms);
        if (chart_ms >= g_rhythm_chart.duration_ms) {
            rg_finish_game();
            return false;
        }
        if ((uint32_t)(now_ms - s_last_draw) >= RG_FRAME_MS) {
            s_last_draw = now_ms;
            rg_draw_play_dynamic(now_ms, chart_ms);
        }
        break;
    }

    default:
        break;
    }

    return false;
}

void rhythm_game_draw(void)
{
    switch (s_state) {
    case RHYTHM_STATE_HOME:
        rg_draw_home();
        break;
    case RHYTHM_STATE_COUNTDOWN:
        rg_draw_countdown_screen();
        rg_draw_countdown_step((s_countdown_step <= 3U) ? s_countdown_step : 3U);
        break;
    case RHYTHM_STATE_PLAY:
        rg_draw_play_static();
        rg_draw_play_dynamic(HAL_GetTick(), rg_chart_now(HAL_GetTick()));
        break;
    case RHYTHM_STATE_PAUSE:
        rg_draw_play_static();
        rg_draw_pause_overlay();
        break;
    case RHYTHM_STATE_RESULT:
        rg_draw_result();
        break;
    default:
        break;
    }
}

void rhythm_game_handle_event(bsp_encoder_event_t event)
{
    uint32_t now_ms = HAL_GetTick();

    if (!s_in_page) {
        return;
    }

    switch (s_state) {
    case RHYTHM_STATE_HOME:
        if (event == BSP_ENCODER_EVENT_SHORT) {
            rg_start_countdown(now_ms, false);
        } else if (event == BSP_ENCODER_EVENT_LONG) {
            s_exit_request = true;
        } else if (event == BSP_ENCODER_EVENT_CW) {
            s_difficulty = (uint8_t)((s_difficulty + 1U) % RG_DIFF_COUNT);
            rg_draw_home();
        } else if (event == BSP_ENCODER_EVENT_CCW) {
            s_difficulty = (uint8_t)((s_difficulty + RG_DIFF_COUNT - 1U) % RG_DIFF_COUNT);
            rg_draw_home();
        }
        break;

    case RHYTHM_STATE_COUNTDOWN:
        if (event == BSP_ENCODER_EVENT_LONG) {
            s_exit_request = true;
        }
        break;

    case RHYTHM_STATE_PLAY:
        if ((event == BSP_ENCODER_EVENT_SHORT) || (event == BSP_ENCODER_EVENT_LONG)) {
            rg_enter_pause(now_ms);
        }
        break;

    case RHYTHM_STATE_PAUSE:
        if (event == BSP_ENCODER_EVENT_CW) {
            s_pause_sel = (uint8_t)((s_pause_sel + 1U) % 3U);
            rg_draw_pause_overlay();
        } else if (event == BSP_ENCODER_EVENT_CCW) {
            s_pause_sel = (uint8_t)((s_pause_sel + 2U) % 3U);
            rg_draw_pause_overlay();
        } else if (event == BSP_ENCODER_EVENT_SHORT) {
            if (s_pause_sel == 0U) {
                rg_resume_from_pause(now_ms);
            } else if (s_pause_sel == 1U) {
                rg_restart(now_ms);
            } else {
                s_exit_request = true;
            }
        } else if (event == BSP_ENCODER_EVENT_LONG) {
            s_exit_request = true;
        }
        break;

    case RHYTHM_STATE_RESULT:
        if (event == BSP_ENCODER_EVENT_CW || event == BSP_ENCODER_EVENT_CCW) {
            s_result_sel = (uint8_t)(s_result_sel ^ 1U);
            rg_draw_result_options();
        } else if (event == BSP_ENCODER_EVENT_SHORT) {
            if (s_result_sel == 0U) {
                rg_restart(now_ms);
            } else {
                s_exit_request = true;
            }
        } else if (event == BSP_ENCODER_EVENT_LONG) {
            s_exit_request = true;
        }
        break;

    default:
        break;
    }
}

void rhythm_game_on_key(uint8_t note, bool pressed, uint32_t now_ms)
{
    if (!s_in_page || (s_state != RHYTHM_STATE_PLAY)) {
        return;
    }
    if (!pressed || (note >= RG_LANE_COUNT)) {
        return;
    }
    rg_judge_press(note, now_ms);
}

void rhythm_game_get_stats(rhythm_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    out->state = (uint8_t)s_state;
    out->note_count = s_note_count;
    out->points = s_points;
    out->combo = s_combo;
    out->max_combo = s_max_combo;
    out->perfect = s_perfect;
    out->great = s_great;
    out->good = s_good;
    out->miss = s_miss;
    out->extra = s_extra;
    out->chart_ms = (s_state == RHYTHM_STATE_PLAY) ? rg_chart_now(HAL_GetTick())
                                                   : s_chart_ms;
    out->final_score = s_final_score;
    out->grade = s_grade;
    out->difficulty = s_difficulty;
    out->speed_pct = (uint8_t)rg_speed_pct();
}

bool rhythm_game_active(void)
{
    return s_in_page;
}
