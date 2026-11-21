#include "ui_page.h"

#include "boot_logo.h"
#include "bsp_tft.h"
#include "bsp_notes.h"
#include "ds_room.h"
#include "record.h"
#include "audio.h"
#include "audio_config.h"
#include "rhythm_game.h"
#include "melody_maker.h"

#include <stddef.h>

/* ---------- 视觉常量 ---------- */
#define UI_BG            0x0862U
#define UI_GRID          0x10A3U
#define UI_PANEL         0x08C5U
#define UI_PANEL_TOP     0x10E4U
#define UI_PANEL_BOTTOM  0x08C5U
#define UI_CYAN          0x07FFU
#define UI_CYAN_DARK     0x04D5U
#define UI_WHITE         0xFFFFU
#define UI_TEXT          0xE71CU
#define UI_GRAY          0x8410U
#define UI_DIM           0x2945U
#define UI_RED           0xF800U
#define UI_RED_DARK      0x7800U
#define UI_REC_TOP       0x9000U
#define UI_REC_BOTTOM    0x3000U
#define UI_REC_DARK      0x5000U
#define UI_REC_LINE      0xFBADU
#define UI_ORANGE        0xFB08U
#define UI_ORANGE_TOP    0xFB08U
#define UI_ORANGE_BOTTOM 0x8200U
#define UI_ORANGE_FILL   0x6182U
#define UI_YELLOW        0xFFE0U
#define UI_SHADOW        0x0000U
#define UI_HIGHLIGHT     0x2D7FU
#define UI_BREATH_MS     60U
#define UI_VALUE_MS      80U
#define UI_SLIDER_MS     120U
#define UI_POPUP_MS      450U
#define UI_SLIDE_FRAME   45U
#define UI_SLIDE_PX      4

#define UI_MAIN_ROW0     44
#define UI_MAIN_PITCH    16
#define UI_MAIN_COUNT    10U
#define UI_MAIN_DS_ROOM  9U
#define UI_MAIN_VISIBLE  7U
#define UI_LIB_ROW0      48
#define UI_LIB_PITCH     18
#define UI_LIB_VISIBLE   5U
static uint8_t song_visible_count(void);   /* 前置声明，见曲库段落 */

/* 曲库列表里"内置曲目"的当前数量（可能因为被移除而少于编译进去的总数） */
#define UI_LIB_DEMO      (song_visible_count())

static ui_page_state_t s_ui;
static uint32_t s_now;
static uint8_t s_main_top;      /* 主菜单滚动窗口的第一项下标 */

static uint8_t s_breath;
static uint32_t s_breath_last;

static bool s_value_flash;
static uint32_t s_value_flash_start;

static bool s_popup;
static uint32_t s_popup_until;

static bool s_slide_active;
static uint32_t s_slide_start;
static int16_t s_slide_offset;
static bool s_content_only;

static bool s_slider_anim;
static uint8_t s_slider_index;
static uint8_t s_slider_from;
static uint8_t s_slider_to;
static uint32_t s_slider_start;
static uint32_t s_slider_last;

static uint32_t s_record_seconds_shown;
static uint8_t s_record_error;
static uint8_t s_record_highlight_mask;
static bool s_record_full_state;

/* 仅缓存音符高亮位图，用于判断需要局部重绘的格子。 */
static uint8_t s_perform_highlight_mask;

static uint8_t s_lib_top;
static char s_playback_name[10];
static uint16_t s_playback_events;
static bool s_playback_is_record;
static uint8_t s_playback_record;
static bool s_playback_started;   /* 回放页是否已经真正开始播放 */
static uint8_t s_playback_speed;  /* 0=慢 1=正常 2=快 */

static const char *const s_main_items[] = {
    "演奏", "音区设置", "移调设置", "节拍器", "录音", "曲库", "音乐游戏", "自动作曲", "系统设置", "音乐小屋"
};

static const tft_icon_t s_main_icons[] = {
    TFT_ICON_PERFORM, TFT_ICON_OCTAVE, TFT_ICON_TRANSPOSE,
    TFT_ICON_METRONOME, TFT_ICON_RECORD, TFT_ICON_LIBRARY,
    TFT_ICON_GAMEPAD, TFT_ICON_NOTE, TFT_ICON_SETTINGS, TFT_ICON_NOTE
};

/* 曲库里的内置曲目不再写死名字，直接读 song 模块的曲谱库，
 * 这样"列表里有的"和"能播放的"永远是同一批，不会出现点不响的条目。 */
static const char *const s_notes[] = {"DO", "RE", "MI", "FA", "SO", "LA", "SI"};

/* ------------------------------------------------------------------ 乐理
 *
 * 七个琴键固定是"大调音阶"的七个音级。移调 −2~+2 半音后，
 * 整条音阶跟着平移，调名也随之改变，这里给出每种移调对应的：
 *   s_key_roots : 主音名
 *   s_key_names : 七个音级在该调下的实际音名（与下面七个琴键一一对应）
 *
 * 移调 +1 用降 D（不是升 C），−2 用降 B，避免出现 E#/B# 这种
 * 乐理上正确但对非专业听众不友好的写法。
 */
static const char *const s_key_roots[5] = {"Bb", "B", "C", "Db", "D"};

static const char *const s_key_names[5][7] = {
    {"Bb", "C",  "D",  "Eb", "F",  "G",  "A" },   /* 降B 大调 */
    {"B",  "C#", "D#", "E",  "F#", "G#", "A#"},   /* B   大调 */
    {"C",  "D",  "E",  "F",  "G",  "A",  "B" },   /* C   大调 */
    {"Db", "Eb", "F",  "Gb", "Ab", "Bb", "C" },   /* 降D 大调 */
    {"D",  "E",  "F#", "G",  "A",  "B",  "C#" }   /* D   大调 */
};

/* 当前移调对应的调名表索引，0~4 */
static uint8_t key_index(void)
{
    int8_t t = Audio_GetTranspose();
    if (t < -2) t = -2;
    if (t > 2) t = 2;
    return (uint8_t)(t + 2);
}

/* 把演奏页原"PLAY"那块位置换成当前调性，并在下面补一行七个音级。
 * y 是第一行的顶端，第二行自动落在 y+18。bg 用于适配不同页面的底色。 */
static void draw_key_info_at(int16_t y, uint16_t bg)
{
    uint8_t idx = key_index();
    const char *root = s_key_roots[idx];
    uint8_t root_len = (root[1] != '\0') ? 2U : 1U;
    uint16_t root_w = (uint16_t)root_len * 12U;                 /* 放大 2 倍，每字 12 像素 */
    uint16_t cn_w = tft_font_text_width(&font12, "大调");
    int16_t x = (int16_t)((128 - (int16_t)(root_w + 4U + cn_w)) / 2);

    /* 第一行：主音 + "大调" */
    tft_fill_rect(22, (int16_t)(y - 2), 84, 18, bg);
    tft_draw_text_ascii_5x7(x, y, root, 2, UI_WHITE, bg);
    tft_draw_text_font(&font12, (int16_t)(x + (int16_t)root_w + 4), y,
                       "大调", UI_CYAN, bg);

    /* 第二行：七个音级，字宽 6 像素，等间距居中 */
    {
        uint16_t total = 0U;
        uint8_t i;
        int16_t cx;

        for (i = 0U; i < 7U; ++i) {
            const char *n = s_key_names[idx][i];
            total = (uint16_t)(total + ((n[1] != '\0') ? 12U : 6U) + 6U);
        }
        cx = (int16_t)((128 - (int16_t)total + 6) / 2);

        tft_fill_rect(4, (int16_t)(y + 16), 120, 12, bg);
        for (i = 0U; i < 7U; ++i) {
            const char *n = s_key_names[idx][i];
            uint16_t nw = ((n[1] != '\0') ? 12U : 6U);
            tft_draw_text_ascii_5x7(cx, (int16_t)(y + 18), n, 1, UI_GRAY, bg);
            cx = (int16_t)(cx + (int16_t)nw + 6);
        }
    }
}

static void draw_key_info(void)
{
    draw_key_info_at(48, UI_PANEL);
}

/* ---------- 基础绘制 ---------- */

static void ui_text12(int16_t x, int16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    tft_draw_text_font(&font12, x, y, text, fg, bg);
}

static void ui_text24_center(int16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    uint16_t w = tft_font_text_width(&font24, text);
    tft_draw_text_font(&font24, (int16_t)((128 - w) / 2), y, text, fg, bg);
}

static void ui_text24_at(int16_t x, int16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    tft_draw_text_font(&font24, x, y, text, fg, bg);
}

static void ui_draw_bg(void)
{
    tft_fill_screen(UI_BG);
    for (int16_t x = 0; x < 128; x = (int16_t)(x + 8)) {
        tft_draw_vline(x, 0, 160, UI_GRID);
    }
    for (int16_t y = 0; y < 160; y = (int16_t)(y + 8)) {
        tft_draw_hline(0, y, 128, UI_GRID);
    }
}

static void ui_draw_header(void)
{
#if UI_ENABLE_GRADIENTS
    tft_fill_gradient_rect(0, 0, 128, 20, 0x0841U, UI_CYAN_DARK, false);
#else
    tft_fill_rect(0, 0, 128, 20, 0x0841U);
#endif
    tft_fill_round_rect(2, 1, 108, 18, 0x0841U);

    if (s_ui.page == UI_PAGE_RECORD) {
        /* 录音页参考图：左侧保留 SmartTone，右侧用 REC 状态框替换音符图标。 */
        tft_draw_text_ascii_5x7(5, 6, "SmartTone", 1, UI_WHITE, 0x0841U);
        tft_fill_round_rect(88, 2, 36, 16, 0x0841U);
        tft_draw_round_rect(88, 2, 36, 16, UI_RED);
        tft_draw_text_ascii_5x7(97, 6, "REC", 1, UI_RED, 0x0841U);
    } else {
        tft_draw_text_ascii_5x7(4, 3, "SmartTone", 2, UI_WHITE, 0x0841U);
#if UI_ENABLE_ICONS
        tft_draw_icon(112, 2, TFT_ICON_NOTE, UI_CYAN, 16);
#endif
    }
    tft_draw_hline(0, 20, 128, UI_CYAN);
}

static void ui_draw_title(const char *title)
{
    tft_fill_rect(0, 21, 128, 21, UI_BG);
    uint16_t w = tft_font_text_width(&font16, title);
    tft_draw_text_font(&font16, (int16_t)((128 - w) / 2), 24, title, UI_WHITE, UI_BG);
#if UI_ENABLE_GRADIENTS
    tft_fill_gradient_rect(4, 41, 120, 1, UI_CYAN, UI_CYAN_DARK, false);
#else
    tft_draw_hline(4, 41, 120, UI_CYAN);
#endif
}

static void draw_selected_card(int16_t x, int16_t y, int16_t w, int16_t h)
{
#if UI_ENABLE_GRADIENTS
    tft_draw_card(x, y, w, h, UI_ORANGE_TOP, UI_ORANGE_BOTTOM,
                  UI_ORANGE, UI_SHADOW, UI_WHITE);
#else
    tft_fill_round_rect(x, y, w, h, UI_ORANGE_FILL);
    tft_draw_round_rect(x, y, w, h, UI_ORANGE);
#endif
}

static void ui_draw_panel(void)
{
#if UI_ENABLE_GRADIENTS
    tft_draw_card(4, 42, 120, 116,
                  UI_PANEL_TOP, UI_PANEL_BOTTOM,
                  UI_CYAN, UI_SHADOW, UI_HIGHLIGHT);
#else
    tft_fill_round_rect(4, 42, 120, 116, UI_PANEL);
    tft_draw_round_rect(4, 42, 120, 116, UI_CYAN);
#endif
    tft_fill_round_rect(6, 44, 116, 112, UI_PANEL);
}

static void ui_draw_base(const char *title)
{
    ui_draw_bg();
    ui_draw_header();
    ui_draw_title(title);
    ui_draw_panel();
}

/* ---------- 主菜单 ---------- */

static void draw_main_row(uint8_t item, bool selected)
{
    /* 菜单超过 7 行可视区，用滚动窗口：item 是真实下标，slot 决定画在第几行。 */
    uint8_t slot = (uint8_t)(item - s_main_top);
    int16_t y = (int16_t)(UI_MAIN_ROW0 + (int16_t)slot * UI_MAIN_PITCH);
    uint16_t bg = selected ? UI_ORANGE_FILL : UI_PANEL;

    if (selected) {
        draw_selected_card(5, (int16_t)(y - 1), 118, 16);
        tft_fill_round_rect(7, y, 114, 14, UI_ORANGE_FILL);
    } else {
        /* 用面板色整行覆盖旧选中卡片，文字/图标随后重画，避免留下黑线。 */
        tft_fill_rect(5, (int16_t)(y - 1), 118, 16, UI_PANEL);
    }
#if UI_ENABLE_ICONS
    tft_draw_icon(10, y, s_main_icons[item],
                  selected ? UI_WHITE : (item == 4U ? UI_RED : UI_CYAN), 16);
#endif
    uint16_t fg = selected ? UI_WHITE : UI_TEXT;
    int16_t text_x = 31;
    if (item == UI_MAIN_DS_ROOM) {
        tft_draw_text_ascii_5x7(text_x, (int16_t)(y + 4), "DS", 1, fg, bg);
        text_x += 14;
    }
    ui_text12(text_x, (int16_t)(y + 2), s_main_items[item], fg, bg);
}

/* 选中项必须落在 [top, top+6] 里；否则把滚动窗口挪过去 */
static void main_ensure_visible(void)
{
    if (s_ui.main_sel < s_main_top) {
        s_main_top = s_ui.main_sel;
    } else if (s_ui.main_sel >= s_main_top + UI_MAIN_VISIBLE) {
        s_main_top = (uint8_t)(s_ui.main_sel - UI_MAIN_VISIBLE + 1U);
    }
}

static void draw_main_page(void)
{
    if (!s_content_only) {
        ui_draw_base("主菜单");
    }
    main_ensure_visible();
    for (uint8_t slot = 0; slot < UI_MAIN_VISIBLE; ++slot) {
        uint8_t item = (uint8_t)(s_main_top + slot);
        if (item < UI_MAIN_COUNT) {
            draw_main_row(item, item == s_ui.main_sel);
        }
    }
}

/* ---------- 音区设置 ---------- */

static void draw_octave_value(uint16_t color)
{
    char value[2];
    value[0] = (char)('1' + Audio_GetOctave());
    value[1] = '\0';
    tft_fill_rect(44, 64, 40, 30, UI_PANEL);
    ui_text24_center(66, value, color, UI_PANEL);
}

static void draw_octave_options(void)
{
    for (uint8_t i = 0; i < 3U; ++i) {
        int16_t x = (int16_t)(14 + i * 35);
        int16_t y = 112;
        bool selected = (Audio_GetOctave() == i);
        char value[2];
        value[0] = (char)('1' + i);
        value[1] = '\0';

        if (selected) {
            draw_selected_card(x, y, 30, 28);
            tft_fill_round_rect((int16_t)(x + 2), (int16_t)(y + 2), 26, 24, UI_ORANGE_FILL);
            ui_text24_at((int16_t)(x + 3), (int16_t)(y + 2), value, UI_WHITE, UI_ORANGE_FILL);
        } else {
            tft_fill_round_rect(x, y, 30, 28, UI_PANEL);
            tft_draw_round_rect(x, y, 30, 28, UI_DIM);
            ui_text24_at((int16_t)(x + 3), (int16_t)(y + 2), value, UI_TEXT, UI_PANEL);
        }
    }
}

static void draw_octave_page(void)
{
    if (!s_content_only) {
    ui_draw_base("音区设置");
    }
    /* OCTAVE 用 ASCII 2x，保持与参考图一致 */
    tft_draw_text_ascii_5x7(28, 46, "OCTAVE", 2, UI_WHITE, UI_PANEL);

    draw_octave_value(UI_CYAN);
    draw_octave_options();

    for (uint8_t i = 0; i < 5U; ++i) {
        tft_fill_rect((int16_t)(16 - i), (int16_t)(78 + i), 1,
                      (int16_t)(10 - 2 * i), UI_CYAN);
        tft_fill_rect((int16_t)(108 + i), (int16_t)(78 + i), 1,
                      (int16_t)(10 - 2 * i), UI_CYAN);
    }
}

/* ---------- 移调设置 ---------- */

static void transpose_text(char *buf)
{
    int8_t transpose = Audio_GetTranspose();
    buf[0] = (transpose >= 0) ? '+' : '-';
    int8_t magnitude = (transpose >= 0) ? transpose : (int8_t)-transpose;
    buf[1] = (char)('0' + magnitude);
    buf[2] = '\0';
}

static void draw_transpose_value(uint16_t color)
{
    char value[3];
    transpose_text(value);
    tft_fill_rect(34, 64, 60, 30, UI_PANEL);
    ui_text24_center(66, value, color, UI_PANEL);
}

static void draw_transpose_page(void)
{
    if (!s_content_only) {
    ui_draw_base("移调设置");
    }
    tft_draw_text_ascii_5x7(10, 46, "TRANSPOSE", 2, UI_WHITE, UI_PANEL);
    tft_draw_text_ascii_5x7(43, 102, "-2...+2", 1, UI_GRAY, UI_PANEL);
    for (uint8_t i = 0; i < 5U; ++i) {
        tft_fill_rect((int16_t)(14 - i), (int16_t)(76 + i), 1,
                      (int16_t)(10 - 2 * i), UI_CYAN);
        tft_fill_rect((int16_t)(112 + i), (int16_t)(76 + i), 1,
                      (int16_t)(10 - 2 * i), UI_CYAN);
    }
    draw_transpose_value(UI_CYAN);
}

/* ---------- 节拍器 ---------- */

static void draw_bpm_value(uint16_t color)
{
    char value[4];
    if (s_ui.bpm >= 100U) {
        value[0] = (char)('0' + s_ui.bpm / 100U);
        value[1] = (char)('0' + (s_ui.bpm / 10U) % 10U);
        value[2] = (char)('0' + s_ui.bpm % 10U);
        value[3] = '\0';
    } else if (s_ui.bpm >= 10U) {
        value[0] = (char)('0' + s_ui.bpm / 10U);
        value[1] = (char)('0' + s_ui.bpm % 10U);
        value[2] = '\0';
    } else {
        value[0] = (char)('0' + s_ui.bpm);
        value[1] = '\0';
    }
    /* 放大 BPM 主显示区，并保持水平居中。 */
    tft_fill_rect(14, 60, 100, 38, UI_PANEL);
    ui_text24_center(67, value, color, UI_PANEL);
    /* 黄色框表示旋钮当前调的是 BPM */
    if (s_ui.metro_focus == 0U) {
        tft_draw_rect(13, 59, 102, 40, UI_YELLOW);
    }
}

/* Keep displayed BPM within the audio module range (40..240). */
static uint16_t metronome_bpm_clamped(void)
{
    uint16_t bpm = s_ui.bpm;
    if (bpm < 40U) bpm = 40U;
    if (bpm > 240U) bpm = 240U;
    return bpm;
}

/* 拍号预设：界面上按 FUNC 在这四种之间循环 */
static const uint8_t s_metro_beats[4]     = {2U, 3U, 4U, 6U};
static const char *const s_metro_sig[4]   = {"2/4", "3/4", "4/4", "6/8"};

/* 状态行：实时读音频模块，不再写死 */
static void draw_metronome_state(void)
{
    tft_fill_rect(58, 108, 40, 14, UI_PANEL);
    if (Metronome_IsRunning()) {
        ui_text12(58, 108, "开启", UI_CYAN, UI_PANEL);
    } else {
        ui_text12(58, 108, "关闭", UI_GRAY, UI_PANEL);
    }
}

/* 拍号行：按 FUNC 切换预设 */
static void draw_metronome_sig(void)
{
    tft_fill_rect(58, 132, 46, 16, UI_PANEL);
    tft_draw_text_ascii_5x7(58, 136, s_metro_sig[s_ui.metro_sig], 1,
                            UI_YELLOW, UI_PANEL);
    if (s_ui.metro_focus == 1U) {
        tft_draw_rect(56, 131, 50, 18, UI_YELLOW);
    }
}

static void draw_metronome_page(void)
{
    if (!s_content_only) {
    ui_draw_base("节拍器");
    }
    tft_draw_text_ascii_5x7(46, 44, "BPM", 2, UI_WHITE, UI_PANEL);
    draw_bpm_value(UI_CYAN);

#if UI_ENABLE_ICONS
    tft_draw_icon(8, 106, TFT_ICON_METRONOME, UI_CYAN, 16);
#endif
    ui_text12(28, 108, "状态", UI_TEXT, UI_PANEL);
    draw_metronome_state();
    ui_text12(28, 132, "拍号", UI_TEXT, UI_PANEL);
    draw_metronome_sig();
}

/* ---------- 曲库 ---------- */

/* ------------------------------------------------------------ 曲库列表映射
 *
 * 内置曲目是编译进 Flash 的常量表，物理上删不掉。但界面上允许"移除"，
 * 做法是维护一张位掩码：被移除的曲目不进入列表，也不参与序号换算。
 * 注意：这张表在 RAM 里，**掉电复位后会恢复**（曲目本身没被擦除）。
 */
#define UI_SONG_MAX      8U

static uint8_t s_song_hidden;                  /* 位掩码：1 = 已从列表移除 */
static uint8_t s_song_map[UI_SONG_MAX];        /* 列表序号 -> 曲目下标 */
static uint8_t s_song_visible;                 /* 当前可见的曲目数 */

static void song_map_rebuild(void)
{
    uint8_t n = Song_GetBuiltinCount();
    uint8_t i;

    if (n > UI_SONG_MAX) {
        n = UI_SONG_MAX;
    }
    s_song_visible = 0U;
    for (i = 0U; i < n; ++i) {
        if ((s_song_hidden & (uint8_t)(1U << i)) == 0U) {
            s_song_map[s_song_visible] = i;
            ++s_song_visible;
        }
    }
}

static uint8_t song_visible_count(void)
{
    return s_song_visible;
}

/* 列表第 list_index 项对应的曲目，越界或不是曲目返回 NULL */
static const SongDef *song_of_list(uint8_t list_index)
{
    if (list_index >= s_song_visible) {
        return NULL;
    }
    return Song_GetBuiltin(s_song_map[list_index]);
}

static uint8_t library_total(void)
{
    return (uint8_t)(UI_LIB_DEMO + record_track_count());
}

static bool library_item(uint8_t index, const char **name, uint16_t *events)
{
    if (index < UI_LIB_DEMO) {
        const SongDef *def = song_of_list(index);
        if (def == NULL) {
            return false;
        }
        *name = def->name;
        *events = def->count;
        return true;
    }

    record_info_t info;
    if (!record_get_info((uint8_t)(index - UI_LIB_DEMO), &info)) {
        return false;
    }
    static char rec_name[9];
    rec_name[0] = 'R';
    rec_name[1] = 'E';
    rec_name[2] = 'C';
    rec_name[3] = '_';
    rec_name[4] = (char)('0' + (info.track_id / 10U) % 10U);
    rec_name[5] = (char)('0' + info.track_id % 10U);
    rec_name[6] = '\0';
    *name = rec_name;
    *events = info.event_count;
    return true;
}

static void draw_library_row(uint8_t visible_index)
{
    uint8_t index = (uint8_t)(s_lib_top + visible_index);
    if (index >= library_total()) {
        return;
    }
    int16_t y = (int16_t)(UI_LIB_ROW0 + (int16_t)visible_index * UI_LIB_PITCH);
    bool selected = (index == s_ui.library_sel);
    uint16_t bg = selected ? UI_ORANGE_FILL : UI_PANEL;
    const char *name;
    uint16_t events;
    if (!library_item(index, &name, &events)) {
        return;
    }
    (void)events;

    char number[3];
    number[0] = (char)('0' + (index + 1U) / 10U);
    number[1] = (char)('0' + (index + 1U) % 10U);
    number[2] = '\0';

    if (selected) {
        draw_selected_card(5, (int16_t)(y - 1), 118, 18);
        tft_fill_round_rect(7, y, 114, 16, bg);
    } else {
        tft_fill_round_rect(6, (int16_t)(y - 1), 116, 18, UI_PANEL);
        tft_draw_hline(10, (int16_t)(y + 17), 108, UI_DIM);
    }

#if UI_ENABLE_ICONS
    /* 内置曲目和录音使用同一个图标，列表看起来是同一类条目 */
    tft_draw_icon(10, y, TFT_ICON_NOTE,
                  selected ? UI_WHITE : UI_CYAN, 16);
#endif
    tft_draw_text_ascii_5x7(30, (int16_t)(y + 2), number, 2,
                            selected ? UI_WHITE : UI_TEXT, bg);
    tft_draw_text_ascii_5x7(54, (int16_t)(y + 5), name, 1,
                            selected ? UI_WHITE : UI_TEXT, bg);
}

static void draw_library_rows(void)
{
    tft_fill_rect(6, 46, 116, 94, UI_PANEL);
    for (uint8_t i = 0; i < UI_LIB_VISIBLE; ++i) {
        draw_library_row(i);
    }

    /* 空列表要给个提示，否则看起来像界面坏了 */
    if (library_total() == 0U) {
        ui_text12(30, 84, "暂无录音", UI_GRAY, UI_PANEL);
    }
}

static void draw_library_page(void)
{
    if (!s_content_only) {
        ui_draw_base("曲目列表");
    }
    uint8_t total = library_total();
    if (s_lib_top + UI_LIB_VISIBLE > total) {
        s_lib_top = (total > UI_LIB_VISIBLE) ? (uint8_t)(total - UI_LIB_VISIBLE) : 0U;
    }
    draw_library_rows();
}

/* ---------- 回放 ---------- */

static void draw_playback_speed_text(void);

static void draw_playback_visual(void)
{
    uint16_t icon_color = s_breath ? UI_WHITE : UI_CYAN;
#if UI_ENABLE_GRADIENTS
    tft_fill_gradient_round_rect(48, 48, 32, 32, UI_CYAN_DARK, UI_BG);
#else
    tft_fill_round_rect(48, 48, 32, 32, UI_CYAN_DARK);
#endif
    tft_draw_round_rect(48, 48, 32, 32, UI_CYAN);
    /* 图标表示"按下去会做什么"：播放中显示暂停键，暂停/结束时显示播放键 */
    tft_draw_icon(56, 56, s_ui.playing ? TFT_ICON_PAUSE : TFT_ICON_PLAY,
                  icon_color, 16);
}

static void draw_playback_state_text(void)
{
    const char *state;
    uint16_t color;

    if (!s_playback_started) {
        state = "就绪";                 /* 刚进页面，还没开始播 */
        color = UI_GRAY;
    } else if (s_ui.playing) {
        state = "播放中";
        color = UI_CYAN;
    } else if ((s_playback_is_record && !record_playback_active()) ||
               (!s_playback_is_record && Song_GetState() == SONG_IDLE)) {
        state = "已结束";               /* 播完了，再按会重播 */
        color = UI_YELLOW;
    } else {
        state = "已暂停";
        color = UI_GRAY;
    }

    tft_fill_rect(20, 82, 88, 26, UI_PANEL);
    ui_text24_center(82, state, color, UI_PANEL);
}

static void draw_playback_page(void)
{
    if (!s_content_only) {
    ui_draw_base("回放");
    }
    s_breath = 1U;
    s_breath_last = s_now;
    draw_playback_visual();
    draw_playback_state_text();

    ui_text12(10, 110, "曲目", UI_TEXT, UI_PANEL);
    tft_draw_text_ascii_5x7(38, 114, ":", 1, UI_TEXT, UI_PANEL);
    tft_draw_text_ascii_5x7(46, 114, s_playback_name, 1,
                            UI_TEXT, UI_PANEL);

    char events[4];
    uint16_t count = s_playback_events;
    if (count >= 100U) {
        events[0] = (char)('0' + count / 100U);
        events[1] = (char)('0' + (count / 10U) % 10U);
        events[2] = (char)('0' + count % 10U);
        events[3] = '\0';
    } else {
        events[0] = (char)('0' + count / 10U);
        events[1] = (char)('0' + count % 10U);
        events[2] = '\0';
    }
    /* 事件数与回放速度合并到同一行：
     * 面板内区只有 44~156，字号放大 2 倍后每个字 14 像素高，
     * 三行放不下（第三行会压到面板下边框），所以并成两行。 */
    ui_text12(8, 130, "事件", UI_TEXT, UI_PANEL);
    tft_draw_text_ascii_5x7(34, 134, ":", 1, UI_TEXT, UI_PANEL);
    tft_draw_text_ascii_5x7(40, 134, events, 1, UI_TEXT, UI_PANEL);

    draw_playback_speed_text();
}

/* 回放速度：0 档 0.5x，1 档 1.0x，2 档 2.0x。画在事件数右边同一行。 */
static void draw_playback_speed_text(void)
{
    char buf[5];
    buf[0] = (s_playback_speed == 0U) ? '0' : ((s_playback_speed == 2U) ? '2' : '1');
    buf[1] = '.';
    buf[2] = (s_playback_speed == 0U) ? '5' : '0';
    buf[3] = 'x';
    buf[4] = '\0';

    ui_text12(60, 130, "速度", UI_TEXT, UI_PANEL);
    tft_draw_text_ascii_5x7(86, 134, ":", 1, UI_TEXT, UI_PANEL);
    tft_draw_text_ascii_5x7(92, 134, buf, 1, UI_YELLOW, UI_PANEL);
}

/* ---------- 系统设置 ---------- */

static int16_t settings_row_y(uint8_t index)
{
    return (int16_t)(56 + (int16_t)index * 32);
}

static uint8_t settings_value(uint8_t index)
{
    uint8_t value;
    uint8_t max;
    if (index == 0U) {
        value = s_ui.brightness;
        max = 5U;
    } else if (index == 1U) {
        value = (uint8_t)(Audio_GetVolume() + 1U);
        max = (uint8_t)VOL_COUNT;
    } else {
        value = (uint8_t)(Audio_GetTimbre() + 1U);
        max = (uint8_t)TIMBRE_COUNT;
    }
    if (value < 1U) value = 1U;
    if (value > max) value = max;
    return value;
}

static uint8_t settings_max(uint8_t index)
{
    return (index == 0U) ? 5U : ((index == 1U) ? (uint8_t)VOL_COUNT : (uint8_t)TIMBRE_COUNT);
}

static void draw_settings_slider(uint8_t index, uint8_t value, bool selected)
{
    int16_t y = settings_row_y(index);
    uint16_t bg = selected ? UI_ORANGE_FILL : UI_PANEL;
    uint8_t max = settings_max(index);
    if (value < 1U) value = 1U;
    if (value > max) value = max;
    uint8_t fill = (uint8_t)((uint16_t)value * 20U / max);
    if (fill > 20U) fill = 20U;

    tft_draw_rect(96, (int16_t)(y + 10), 24, 7, selected ? UI_WHITE : UI_DIM);
    tft_fill_rect(98, (int16_t)(y + 12), 20, 3, bg);
    if (fill > 0U) {
#if UI_ENABLE_GRADIENTS
        tft_fill_gradient_rect(98, (int16_t)(y + 12), fill, 3, UI_CYAN, UI_ORANGE, false);
#else
        tft_fill_rect(98, (int16_t)(y + 12), fill, 3, UI_CYAN);
#endif
    }
}
static void draw_settings_row(uint8_t index)
{
    static const char *const labels[] = {"亮度", "音量", "音色"};
    bool selected = (index == s_ui.settings_sel);
    int16_t y = settings_row_y(index);
    uint16_t bg = selected ? UI_ORANGE_FILL : UI_PANEL;
    uint8_t value = settings_value(index);
    char buf[2];
    buf[0] = (char)('0' + value);
    buf[1] = '\0';

    if (selected) {
        draw_selected_card(5, (int16_t)(y - 4), 118, 26);
        tft_fill_round_rect(7, (int16_t)(y - 2), 114, 22, bg);
    } else {
        tft_fill_round_rect(6, (int16_t)(y - 4), 116, 26, UI_PANEL);
        tft_draw_hline(10, (int16_t)(y + 22), 108, UI_DIM);
    }

    ui_text12(12, (int16_t)(y + 7), labels[index],
              selected ? UI_WHITE : UI_TEXT, bg);
    ui_text24_at(64, y, buf, selected ? UI_WHITE : UI_TEXT, bg);

#if UI_ENABLE_ICONS
    tft_draw_icon(40, (int16_t)(y + 4),
                  index == 0U ? TFT_ICON_BRIGHTNESS :
                  (index == 1U ? TFT_ICON_VOLUME : TFT_ICON_TONE),
                  selected ? UI_WHITE : UI_CYAN, 16);
#endif

    draw_settings_slider(index, value, selected);
}

static void draw_settings_page(void)
{
    if (!s_content_only) {
    ui_draw_base("系统设置");
    }
    for (uint8_t i = 0; i < 3U; ++i) {
        draw_settings_row(i);
    }
}

static void settings_select_next(void)
{
    uint8_t previous = s_ui.settings_sel;
    s_slider_anim = false;
    s_ui.settings_sel = (uint8_t)((previous + 1U) % 3U);
    tft_redraw_region(5, (int16_t)(settings_row_y(previous) - 4), 119, 28, draw_settings_page);
    tft_redraw_region(5, (int16_t)(settings_row_y(s_ui.settings_sel) - 4), 119, 28, draw_settings_page);
}

/* ---------- 演奏 ---------- */

#define PERFORM_NOTE_X0     2
#define PERFORM_NOTE_Y      80
#define PERFORM_NOTE_PITCH  18
#define PERFORM_NOTE_SIZE   16
#define PERFORM_PARAM_X0    4
#define PERFORM_PARAM_Y     120
#define PERFORM_PARAM_W     38
#define PERFORM_PARAM_H     16
#define PERFORM_PARAM_PITCH 41

static uint8_t perform_octave_value(void)
{
    uint8_t octave = Audio_GetOctave();
    return (octave > OCT_HIGH) ? (uint8_t)OCT_HIGH : octave;
}

static uint8_t perform_volume_value(void)
{
    uint8_t volume = Audio_GetVolume();
    return (volume > VOL_5) ? (uint8_t)VOL_5 : volume;
}

static int8_t perform_key_value(void)
{
    int8_t key = Audio_GetTranspose();
    if (key < -2) return -2;
    if (key > 2) return 2;
    return key;
}

/* 单音符局部重绘：只覆盖 16x16 的格子，不触碰其它音符或整页。 */
static void draw_perform_note(uint8_t index, bool active)
{
    if (index >= 7U) {
        return;
    }
    int16_t x = (int16_t)(PERFORM_NOTE_X0 + index * PERFORM_NOTE_PITCH);
    int16_t y = PERFORM_NOTE_Y;
    uint16_t bg = active ? UI_YELLOW : UI_PANEL;
    uint16_t border = active ? UI_YELLOW : UI_DIM;
    uint16_t fg = active ? UI_BG : UI_TEXT;

    /* 用方角填充清掉上一状态，避免黄色圆角残留。 */
    tft_fill_rect(x, y, PERFORM_NOTE_SIZE, PERFORM_NOTE_SIZE, bg);
    tft_draw_rect(x, y, PERFORM_NOTE_SIZE, PERFORM_NOTE_SIZE, border);
    tft_draw_text_ascii_5x7((int16_t)(x + 2), (int16_t)(y + 5), s_notes[index],
                            1, fg, bg);
}

/* 单个参数块局部重绘：OCT/VOL/KEY 等宽、等间距。 */
static void draw_perform_param(uint8_t index)
{
    if (index > 2U) {
        return;
    }

    int16_t x = (int16_t)(PERFORM_PARAM_X0 + index * PERFORM_PARAM_PITCH);
    int16_t y = PERFORM_PARAM_Y;
    char text[7];
    uint8_t len;

    if (index == 0U) {
        /* V2：音区必须直接读音频模块。 */
        uint8_t octave = perform_octave_value();
        {
            static const char *const ocw[3] = { "LOW", "MID", "HIGH" };
            const char *w = ocw[(octave > 2U) ? 2U : octave];
            text[0] = w[0]; text[1] = w[1]; text[2] = w[2];
            text[3] = w[3]; text[4] = '\0';
            len = (uint8_t)((w[3] != '\0') ? 4U : 3U);
        }
    } else if (index == 1U) {
        /* V2：音量必须直接读音频模块。 */
        uint8_t volume = perform_volume_value();
        text[0] = 'V'; text[1] = 'O'; text[2] = 'L';
        text[3] = ' '; text[4] = (char)('1' + volume); text[5] = '\0';
        len = 5U;
    } else {
        /* V2：移调必须直接读音频模块。 */
        int8_t key = perform_key_value();
        text[0] = 'K'; text[1] = 'E'; text[2] = 'Y';
        text[3] = ' '; text[4] = (key < 0) ? '-' : '+';
        text[5] = (char)('0' + ((key < 0) ? -key : key));
        text[6] = '\0';
        len = 6U;
    }

    tft_fill_rect(x, y, PERFORM_PARAM_W, PERFORM_PARAM_H, UI_PANEL);
    tft_draw_rect(x, y, PERFORM_PARAM_W, PERFORM_PARAM_H, UI_CYAN);
    /* 当前调节对象画双层黄色边框，一眼能看出在调哪一项 */
    if (index == s_ui.param_sel) {
        tft_draw_rect((int16_t)(x + 1), (int16_t)(y + 1),
                      (int16_t)(PERFORM_PARAM_W - 2), (int16_t)(PERFORM_PARAM_H - 2),
                      UI_YELLOW);
    }
    int16_t tx = (int16_t)(x + (PERFORM_PARAM_W - (int16_t)len * 6) / 2);
    tft_draw_text_ascii_5x7(tx, (int16_t)(y + 4), text, 1, UI_TEXT, UI_PANEL);
}

static void draw_perform_params(void)
{
    draw_perform_param(0U);
    draw_perform_param(1U);
    draw_perform_param(2U);
}

static void draw_perform_page(void)
{
    if (!s_content_only) {
    ui_draw_base("演奏");
    }
    draw_key_info();

    /* 7 个标识按当前有效声部位图一次性初始化，之后只局部刷新变化格。 */
    uint8_t mask = bsp_notes_active_mask();
    for (uint8_t i = 0; i < 7U; ++i) {
        draw_perform_note(i, (mask & (uint8_t)(1U << i)) != 0U);
    }
    s_perform_highlight_mask = mask;

    draw_perform_params();
}

/* 演奏页周期任务：仅比较有效声部位图，局部重绘发生变化的音符格。 */
static void perform_page_tick(void)
{
    uint8_t mask = bsp_notes_active_mask();
    uint8_t changed = (uint8_t)(mask ^ s_perform_highlight_mask);
    if (changed != 0U) {
        for (uint8_t i = 0; i < 7U; ++i) {
            if ((changed & (uint8_t)(1U << i)) != 0U) {
                draw_perform_note(i, (mask & (uint8_t)(1U << i)) != 0U);
            }
        }
        s_perform_highlight_mask = mask;
    }
}

/* ---------- 录音 ---------- */

#define RECORD_CARD_X       1
#define RECORD_CARD_Y       45
#define RECORD_CARD_W       126
#define RECORD_CARD_H       102
#define RECORD_NOTE_Y       96
#define RECORD_NOTE_X0      2
#define RECORD_NOTE_PITCH   18
#define RECORD_NOTE_SIZE    16
#define RECORD_PARAM_Y      118
#define RECORD_PARAM_X0     4
#define RECORD_PARAM_W      38
#define RECORD_PARAM_H      16
#define RECORD_PARAM_PITCH  41

static const char *const s_record_notes[] = {
    "DO", "RE", "MI", "FA", "SO", "LA", "XI"
};

static void record_status_append(char *dst, uint8_t *pos, const char *src)
{
    while (*src != '\0' && *pos < 18U) {
        dst[(*pos)++] = *src++;
    }
}

static void draw_record_status(void)
{
    bool recording = record_is_recording();
    bool full = record_is_full() ||
                (s_ui.record_mode == 0U &&
                 record_track_count() >= RECORD_MAX_TRACKS);
    bool overdub_mode = (s_record_error == 0U) && !full &&
                        (s_ui.record_mode != 0U);
    char prefix[20];
    char file[7];
    uint8_t n = 0U;
    bool show_file = false;
    uint16_t dot;

    file[0] = '\0';
    if (s_record_error != 0U) {
        record_status_append(prefix, &n, (s_record_error == 2U) ? "已满" : "保存失败");
        dot = UI_ORANGE;
    } else if (full) {
        record_status_append(prefix, &n, "已满");
        dot = UI_ORANGE;
    } else if (overdub_mode) {
        record_info_t info;
        if (record_get_info(s_ui.record_sel, &info)) {
            file[0] = 'R';
            file[1] = 'E';
            file[2] = 'C';
            file[3] = '_';
            file[4] = (char)('0' + (info.track_id / 10U) % 10U);
            file[5] = (char)('0' + info.track_id % 10U);
            file[6] = '\0';
            show_file = true;
        }
        dot = recording ? UI_RED : UI_GRAY;
    } else if (recording) {
        record_status_append(prefix, &n, "录音中");
        dot = UI_RED;
    } else {
        record_status_append(prefix, &n, "新建");
        dot = UI_GRAY;
    }
    prefix[n] = '\0';
    uint16_t color = recording ? UI_WHITE : UI_TEXT;

    /* 左上角状态条：红点 + 当前模式/文件，局部重绘不破坏红色卡片。
     * “叠录”用 16px 标题字体保证笔画清楚；文件名用 5x7 ASCII 单独绘制。 */
    if (overdub_mode) {
        tft_fill_round_rect(5, 48, 84, 18, UI_REC_DARK);
        tft_fill_round_rect(7, 54, 6, 6, dot);
        tft_draw_text_font(&font16, 16, 49, "加录", color, UI_REC_DARK);
        if (show_file) {
            tft_draw_text_ascii_5x7(50, 54, file, 1, color, UI_REC_DARK);
        } else {
            ui_text12(50, 51, "无录音", color, UI_REC_DARK);
        }
    } else {
        tft_fill_round_rect(5, 49, 84, 15, UI_REC_DARK);
        tft_fill_round_rect(8, 53, 6, 6, dot);
        ui_text12(15, 51, prefix, color, UI_REC_DARK);
    }
    s_record_full_state = full;
}

static void draw_record_timer(void)
{
    uint32_t elapsed = record_elapsed_ms(s_now);
    uint8_t minutes = (uint8_t)((elapsed / 60000UL) % 100UL);
    uint8_t seconds = (uint8_t)((elapsed / 1000UL) % 60UL);
    char text[6];

    text[0] = (char)('0' + minutes / 10U);
    text[1] = (char)('0' + minutes % 10U);
    text[2] = ':';
    text[3] = (char)('0' + seconds / 10U);
    text[4] = (char)('0' + seconds % 10U);
    text[5] = '\0';

    /* 计时器移到标题行，与"录音"大标题并列靠右 */
    uint16_t color = record_is_recording() ? UI_WHITE : UI_GRAY;
    tft_fill_rect(92, 25, 34, 18, UI_BG);
    tft_draw_text_ascii_5x7(94, 27, text, 1, color, UI_BG);
    s_record_seconds_shown = elapsed / 1000UL;
}

/* 录音页音符格与演奏页保持 16x16、18 px 间距；只重绘变化的单格。 */
static void draw_record_note(uint8_t index, bool active)
{
    if (index >= 7U) {
        return;
    }
    int16_t x = (int16_t)(RECORD_NOTE_X0 + index * RECORD_NOTE_PITCH);
    int16_t y = RECORD_NOTE_Y;
    uint16_t bg = active ? UI_YELLOW : UI_PANEL;
    uint16_t border = active ? UI_YELLOW : UI_DIM;
    uint16_t fg = active ? UI_BG : UI_TEXT;

    tft_fill_rect(x, y, RECORD_NOTE_SIZE, RECORD_NOTE_SIZE, bg);
    tft_draw_rect(x, y, RECORD_NOTE_SIZE, RECORD_NOTE_SIZE, border);
    tft_draw_text_ascii_5x7((int16_t)(x + 2), (int16_t)(y + 5), s_record_notes[index],
                            1, fg, bg);
}

static uint8_t record_octave_value(void)
{
    uint8_t octave = Audio_GetOctave();
    return (octave > OCT_HIGH) ? (uint8_t)OCT_HIGH : octave;
}

static uint8_t record_volume_value(void)
{
    uint8_t volume = Audio_GetVolume();
    return (volume > VOL_5) ? (uint8_t)VOL_5 : volume;
}

static int8_t record_key_value(void)
{
    int8_t key = Audio_GetTranspose();
    if (key < -2) return -2;
    if (key > 2) return 2;
    return key;
}

/* 录音页参数块与演奏页同尺寸、同边框、同间距；数值直接读 Audio_Get*。 */
static void draw_record_param(uint8_t index)
{
    if (index > 2U) {
        return;
    }

    int16_t x = (int16_t)(RECORD_PARAM_X0 + index * RECORD_PARAM_PITCH);
    int16_t y = RECORD_PARAM_Y;
    char text[7];
    uint8_t len;

    if (index == 0U) {
        uint8_t octave = record_octave_value();
        {
            static const char *const ocw[3] = { "LOW", "MID", "HIGH" };
            const char *w = ocw[(octave > 2U) ? 2U : octave];
            text[0] = w[0]; text[1] = w[1]; text[2] = w[2];
            text[3] = w[3]; text[4] = '\0';
            len = (uint8_t)((w[3] != '\0') ? 4U : 3U);
        }
    } else if (index == 1U) {
        uint8_t volume = record_volume_value();
        text[0] = 'V'; text[1] = 'O'; text[2] = 'L';
        text[3] = ' '; text[4] = (char)('1' + volume); text[5] = '\0';
        len = 5U;
    } else {
        int8_t key = record_key_value();
        text[0] = 'K'; text[1] = 'E'; text[2] = 'Y';
        text[3] = ' '; text[4] = (key < 0) ? '-' : '+';
        text[5] = (char)('0' + ((key < 0) ? -key : key));
        text[6] = '\0';
        len = 6U;
    }

    tft_fill_rect(x, y, RECORD_PARAM_W, RECORD_PARAM_H, UI_PANEL);
    tft_draw_rect(x, y, RECORD_PARAM_W, RECORD_PARAM_H, UI_CYAN);
    /* 与演奏页一致：当前调节对象画双层黄色边框 */
    if (index == s_ui.param_sel) {
        tft_draw_rect((int16_t)(x + 1), (int16_t)(y + 1),
                      (int16_t)(RECORD_PARAM_W - 2), (int16_t)(RECORD_PARAM_H - 2),
                      UI_YELLOW);
    }
    int16_t tx = (int16_t)(x + (RECORD_PARAM_W - (int16_t)len * 6) / 2);
    tft_draw_text_ascii_5x7(tx, (int16_t)(y + 4), text, 1, UI_TEXT, UI_PANEL);
}

static void draw_record_params(void)
{
    draw_record_param(0U);
    draw_record_param(1U);
    draw_record_param(2U);
}

static void draw_record_page(void)
{
    if (!s_content_only) {
        ui_draw_base("录音");
    }

    /* 录音页专属红色霓虹主卡片。 */
    tft_draw_card(RECORD_CARD_X, RECORD_CARD_Y, RECORD_CARD_W, RECORD_CARD_H,
                  UI_REC_TOP, UI_REC_BOTTOM, UI_RED, UI_SHADOW, UI_REC_LINE);

    draw_record_status();
    draw_record_timer();

    /* 与演奏页一致：显示当前调性与七个音级，录音时也能看到自己在什么调上弹 */
    draw_key_info_at(68, UI_REC_DARK);

    uint8_t mask = bsp_notes_active_mask();
    for (uint8_t i = 0; i < 7U; ++i) {
        draw_record_note(i, (mask & (uint8_t)(1U << i)) != 0U);
    }
    s_record_highlight_mask = mask;

    draw_record_params();
}

/* 录音页周期任务：只比较有效声部位图，变化时重绘单个音符格。 */
static void record_page_tick(void)
{
    bool full = record_is_full() ||
                (s_ui.record_mode == 0U &&
                 record_track_count() >= RECORD_MAX_TRACKS);
    if (full != s_record_full_state) {
        draw_record_status();
    }

    uint8_t mask = bsp_notes_active_mask();
    uint8_t changed = (uint8_t)(mask ^ s_record_highlight_mask);
    if (changed == 0U) {
        return;
    }
    for (uint8_t i = 0; i < 7U; ++i) {
        if ((changed & (uint8_t)(1U << i)) != 0U) {
            draw_record_note(i, (mask & (uint8_t)(1U << i)) != 0U);
        }
    }
    s_record_highlight_mask = mask;
}

/* ---------- 页面分派 ---------- */

/* ------------------------------------------------------------ 欢迎页
 *
 * 开机画面不再"5 秒后自动消失"，而是变成常驻首页：图片一直保留，
 * 底部压一条信息栏写欢迎语和进入方式。长按旋钮才进主菜单，
 * 在主菜单再长按旋钮回到这里。
 */
/* 黑字描边：先画右下方的半透明投影，再用浅色把字形沿八个方向描一圈，
 * 最后填黑色字。彩图下半部分是白围裙和红裙子：
 *   - 白围裙上黑字本身对比够；
 *   - 红裙子/深色处靠白色描边把黑字"托"出来。
 * 不能用黑描边，否则黑字会和描边糊成一团。 */
#define HOME_TEXT_OUTLINE      0xFFFFU
#define HOME_TEXT_SHADOW       0x0000U
#define HOME_SHADOW_NEAR       200U     /* 紧贴字的下缘，稍浓 */
#define HOME_SHADOW_FAR        110U     /* 再外一层，淡一些，形成过渡 */

static void home_text_ascii_outlined(int16_t x, int16_t y, const char *s, uint16_t fg)
{
    static const int8_t d[8][2] = {
        {-1,-1}, {0,-1}, {1,-1}, {-1,0}, {1,0}, {-1,1}, {0,1}, {1,1}
    };
    /* 先铺阴影（与底色混合，不是实心黑块），再描边，最后填字色 */
    tft_draw_text_ascii_5x7_overlay((int16_t)(x + 1), (int16_t)(y + 2),
                                    s, 1, HOME_TEXT_SHADOW, HOME_SHADOW_NEAR,
                                    boot_logo_pixel_at, NULL);
    tft_draw_text_ascii_5x7_overlay((int16_t)(x + 2), (int16_t)(y + 3),
                                    s, 1, HOME_TEXT_SHADOW, HOME_SHADOW_FAR,
                                    boot_logo_pixel_at, NULL);
    for (uint8_t i = 0U; i < 8U; ++i) {
        tft_draw_text_ascii_5x7_overlay((int16_t)(x + d[i][0]), (int16_t)(y + d[i][1]),
                                        s, 1, HOME_TEXT_OUTLINE, 255U, boot_logo_pixel_at, NULL);
    }
    tft_draw_text_ascii_5x7_overlay(x, y, s, 1, fg, 255U, boot_logo_pixel_at, NULL);
}

static void home_text_cn_outlined(int16_t x, int16_t y, const char *s, uint16_t fg)
{
    static const int8_t d[8][2] = {
        {-1,-1}, {0,-1}, {1,-1}, {-1,0}, {1,0}, {-1,1}, {0,1}, {1,1}
    };
    tft_draw_text_font_overlay(&font12, (int16_t)(x + 1), (int16_t)(y + 2),
                               s, HOME_TEXT_SHADOW, HOME_SHADOW_NEAR,
                               boot_logo_pixel_at, NULL);
    tft_draw_text_font_overlay(&font12, (int16_t)(x + 2), (int16_t)(y + 3),
                               s, HOME_TEXT_SHADOW, HOME_SHADOW_FAR,
                               boot_logo_pixel_at, NULL);
    for (uint8_t i = 0U; i < 8U; ++i) {
        tft_draw_text_font_overlay(&font12, (int16_t)(x + d[i][0]), (int16_t)(y + d[i][1]),
                                   s, HOME_TEXT_OUTLINE, 255U, boot_logo_pixel_at, NULL);
    }
    tft_draw_text_font_overlay(&font12, x, y, s, fg, 255U, boot_logo_pixel_at, NULL);
}

static void draw_home_page(void)
{
    boot_logo_draw();                       /* 整屏背景图，不覆盖任何区域 */

    /* 黑色字 + 白色描边 + 右下方投影：白围裙上靠黑字本身，红裙子/深色处靠白描边。
     * 128 像素宽，20 个字符最多只能用 6 像素/字，这就是"最大"了。 */
    home_text_ascii_outlined(7, 108, "Welcome to SmartTone", 0x0000U);

    /* 黑色字；面板红蓝对调只影响彩色，黑/白不受影响。 */
    home_text_cn_outlined(10, 132, "长按旋钮进入主菜单", 0x0000U);
}

static const char *page_title(void)
{
    switch (s_ui.page) {
        case UI_PAGE_MAIN:      return "主菜单";
        case UI_PAGE_PERFORM:   return "演奏";
        case UI_PAGE_OCTAVE:    return "音区设置";
        case UI_PAGE_TRANSPOSE: return "移调设置";
        case UI_PAGE_METRONOME: return "节拍器";
        case UI_PAGE_RECORD:    return "录音";
        case UI_PAGE_LIBRARY:   return "曲目列表";
        case UI_PAGE_PLAYBACK:  return "回放";
        case UI_PAGE_SETTINGS:  return "系统设置";
        case UI_PAGE_RHYTHM:    return "音乐游戏";
        default:                return "主菜单";
    }
}

static void draw_current_content(void)
{
    s_content_only = true;
    switch (s_ui.page) {
        case UI_PAGE_MAIN:      draw_main_page(); break;
        case UI_PAGE_PERFORM:   draw_perform_page(); break;
        case UI_PAGE_OCTAVE:    draw_octave_page(); break;
        case UI_PAGE_TRANSPOSE: draw_transpose_page(); break;
        case UI_PAGE_METRONOME: draw_metronome_page(); break;
        case UI_PAGE_RECORD:    draw_record_page(); break;
        case UI_PAGE_LIBRARY:   draw_library_page(); break;
        case UI_PAGE_PLAYBACK:  draw_playback_page(); break;
        case UI_PAGE_SETTINGS:  draw_settings_page(); break;
        default:                draw_main_page(); break;
    }
    s_content_only = false;
}

static void draw_content_at_offset(int16_t dy)
{
    tft_set_y_offset(0);
    tft_fill_rect(6, 44, 116, 112, UI_PANEL);
    tft_set_y_offset(dy);
    draw_current_content();
    tft_set_y_offset(0);
}

static void draw_current_page(void)
{
    tft_set_y_offset(0);

    /* 欢迎页是整屏图片，不走通用框架：没有标题栏，也没有内容面板 */
    if (s_ui.page == UI_PAGE_HOME) {
        s_slide_active = false;
        draw_home_page();
        return;
    }

    /* 音乐游戏自己画整屏（首页/倒计时/七轨/暂停/结算），不走通用框架 */
    if (s_ui.page == UI_PAGE_RHYTHM) {
        s_slide_active = false;
        rhythm_game_draw();
        return;
    }

    if (s_ui.page == UI_PAGE_MELODY) {
        s_slide_active = false;
        melody_maker_draw();
        return;
    }

    if (s_ui.page == UI_PAGE_DS_ROOM) {
        s_slide_active = false;
        tft_set_color_order_swapped(true);
        ds_room_enter(s_now);
        return;
    }

    ui_draw_base(page_title());
#if UI_ENABLE_ANIMATIONS
    if (s_ui.page != UI_PAGE_MAIN && s_ui.page != UI_PAGE_SETTINGS) {
        s_slide_active = true;
        s_slide_start = s_now;
        s_slide_offset = UI_SLIDE_PX;
        draw_content_at_offset(s_slide_offset);
        return;
    }
#endif
    s_slide_active = false;
    if (s_ui.page == UI_PAGE_SETTINGS) {
        draw_current_content(); /* No offset/clear/repaint pass for a static focus page. */
    } else {
        draw_content_at_offset(0);
    }
}

static void enter_page(ui_page_id_t page)
{
    if (s_ui.page == UI_PAGE_DS_ROOM && page != UI_PAGE_DS_ROOM) {
        tft_set_color_order_swapped(false);
    }
    s_ui.page = page;
    s_value_flash = false;
    s_slider_anim = false;

    /* 节拍器是全局的：进出页面都不改变它的开关状态，
     * 只把界面上的 BPM 与拍号同步给音频模块，保证调过的设置不会丢。 */
    if (page == UI_PAGE_METRONOME) {
        (void)Metronome_SetBpm(metronome_bpm_clamped());
        (void)Metronome_SetBeatsPerBar(s_metro_beats[s_ui.metro_sig]);
    }

    draw_current_page();
}

/* ------------------------------------------------------------ 菜单提示音
 *
 * 不用曲目播放器（它依赖主循环调度，会被整屏重绘拖住导致"只响半声"），
 * 改成进页面立刻起音、70 ms 后关掉。第一个音是瞬时的。
 */
static VoiceId  s_blip_voice = AUDIO_VOICE_INVALID;
static uint32_t s_blip_end_ms;
static VoiceId  s_tick_voice = AUDIO_VOICE_INVALID;
static uint32_t s_tick_end_ms;

/* 每个条目各一个音高，风格不同（音乐游戏自己用倒计时音，实际不响这一声） */
static const uint16_t s_blip_hz[] = {
    1568U,   /* 演奏     上行明亮 */
     880U,   /* 音区     下沉 */
    1245U,   /* 移调     小三度 */
    1500U,   /* 节拍器   同音 */
    1175U,   /* 录音     低沉 */
    1318U,   /* 曲库     中高 */
    1480U,   /* 音乐游戏 中高偏亮 */
    1047U,   /* 自动作曲 */
     494U,   /* 设置     最低 */
    1175U    /* DS 音乐小屋 */
};

/* 菜单音效第二个音：第一个音响起 55 ms 后接一个上行音，形成两音动机。 */
static uint32_t s_blip2_at;
static uint16_t s_blip2_hz;
static bool     s_blip2_pending;

_Static_assert(sizeof(s_blip_hz) / sizeof(s_blip_hz[0]) == UI_MAIN_COUNT, "menu blip count");

static void menu_blip_start(void)
{
    uint8_t idx = (s_ui.main_sel >= UI_MAIN_COUNT) ? 0U : s_ui.main_sel;

    if (s_blip_voice >= 0) {
        (void)Audio_NoteOff(s_blip_voice);
        s_blip_voice = AUDIO_VOICE_INVALID;
    }
    if (s_tick_voice >= 0) {
        (void)Audio_NoteOff(s_tick_voice);
        s_tick_voice = AUDIO_VOICE_INVALID;
    }
    /* 系统提示音固定第 3 档，不跟随设置页/演奏页的全局音量。 */
    if (Audio_NoteOnPitchGain(s_blip_hz[idx], AUDIO_SFX_PERMILLE,
                              AUDIO_PRIO_MELODY, &s_blip_voice) == AUDIO_OK) {
        s_blip_end_ms = HAL_GetTick() + 70U;
    } else {
        s_blip_voice = AUDIO_VOICE_INVALID;
    }

    /* 第二音：纯四度上行，55 ms 后接上，两音合计仍在一百多毫秒内，不拖沓。 */
    if (s_blip_voice >= 0) {
        s_blip2_hz = (uint16_t)(((uint32_t)s_blip_hz[idx] * 4U) / 3U);
        s_blip2_at = HAL_GetTick() + 55U;
        s_blip2_pending = true;
    } else {
        s_blip2_pending = false;
    }
}

/* 主页面旋转编码器：每格一声短“嗒”，固定第 3 档。
 * 只有主菜单旋转会调用，子页面调参数不响，避免吵。 */
static void menu_tick_start(void)
{
    if (s_tick_voice >= 0) {
        (void)Audio_NoteOff(s_tick_voice);
        s_tick_voice = AUDIO_VOICE_INVALID;
    }
    if (s_blip_voice >= 0) {
        (void)Audio_NoteOff(s_blip_voice);
        s_blip_voice = AUDIO_VOICE_INVALID;
    }
    if (Audio_NoteOnPitchGain(1800U, AUDIO_SFX_PERMILLE,
                              AUDIO_PRIO_MELODY, &s_tick_voice) == AUDIO_OK) {
        s_tick_end_ms = HAL_GetTick() + 18U;
    } else {
        s_tick_voice = AUDIO_VOICE_INVALID;
    }
}

static void menu_blip_tick(uint32_t now)
{
    if (s_blip2_pending && ((int32_t)(now - s_blip2_at) >= 0)) {
        s_blip2_pending = false;
        if (s_blip_voice >= 0) {
            (void)Audio_NoteOff(s_blip_voice);
            s_blip_voice = AUDIO_VOICE_INVALID;
        }
        if (Audio_NoteOnPitchGain(s_blip2_hz, AUDIO_SFX_PERMILLE,
                                  AUDIO_PRIO_MELODY, &s_blip_voice) == AUDIO_OK) {
            s_blip_end_ms = now + 70U;
        } else {
            s_blip_voice = AUDIO_VOICE_INVALID;
        }
    }

    if ((s_blip_voice >= 0) && ((int32_t)(now - s_blip_end_ms) >= 0)) {
        (void)Audio_NoteOff(s_blip_voice);
        s_blip_voice = AUDIO_VOICE_INVALID;
    }
    if ((s_tick_voice >= 0) && ((int32_t)(now - s_tick_end_ms) >= 0)) {
        (void)Audio_NoteOff(s_tick_voice);
        s_tick_voice = AUDIO_VOICE_INVALID;
    }
}
static void enter_selected_main_item(void)
{
    /* 进入子界面时给一声确认提示音；游戏项要等进入后音频状态切到 PIANO 再响，
     * 否则会被 rhythm_game_enter() 里的 Audio_AllOff() 立刻掐掉。 */
    if (s_ui.main_sel != 6U && s_ui.main_sel != 7U) {
        menu_blip_start();
    }

    switch (s_ui.main_sel) {
        case 0: enter_page(UI_PAGE_PERFORM); break;
        case 1: enter_page(UI_PAGE_OCTAVE); break;
        case 2: enter_page(UI_PAGE_TRANSPOSE); break;
        case 3: enter_page(UI_PAGE_METRONOME); break;
        case 4:
            record_cancel();
            s_ui.record_mode = 0U;
            s_ui.record_sel = 0U;
            s_record_error = 0U;
            s_record_seconds_shown = 0U;
            enter_page(UI_PAGE_RECORD);
            break;
        case 5:
            s_ui.library_sel = 0U;
            s_lib_top = 0U;
            enter_page(UI_PAGE_LIBRARY);
            break;
        case 6:
            rhythm_game_enter();
            menu_blip_start();
            enter_page(UI_PAGE_RHYTHM);
            break;
        case 7:
            s_blip2_pending = false;
            s_blip_voice = s_tick_voice = AUDIO_VOICE_INVALID;
            melody_maker_enter();
            enter_page(UI_PAGE_MELODY);
            break;
        case 8:
            s_ui.settings_sel = 0U;
            enter_page(UI_PAGE_SETTINGS);
            break;
        case UI_MAIN_DS_ROOM: enter_page(UI_PAGE_DS_ROOM); break;
        default: break;
    }
}

/* ---------- 事件处理（保留原业务逻辑） ---------- */

static void redraw_main_selection(uint8_t old_sel, uint8_t old_top)
{
    /* Recompose only the list/panel; keep header/title stable and never expose blank rows. */
    if (s_main_top != old_top) {
        tft_redraw_region(4, 42, 121, 117, draw_main_page);
        return;
    }
    int16_t old_y = (int16_t)(UI_MAIN_ROW0 + (old_sel - s_main_top) * UI_MAIN_PITCH);
    int16_t new_y = (int16_t)(UI_MAIN_ROW0 + (s_ui.main_sel - s_main_top) * UI_MAIN_PITCH);
    tft_redraw_region(5, (int16_t)(old_y - 1), 119, 17, draw_main_page);
    tft_redraw_region(5, (int16_t)(new_y - 1), 119, 17, draw_main_page);
}

static void handle_main_event(bsp_encoder_event_t event)
{
    uint8_t old_sel = s_ui.main_sel;
    uint8_t old_top = s_main_top;
    if (event == BSP_ENCODER_EVENT_CW) {
        s_ui.main_sel = (uint8_t)((s_ui.main_sel + 1U) % UI_MAIN_COUNT);
        main_ensure_visible();
        redraw_main_selection(old_sel, old_top);
        menu_tick_start();
    } else if (event == BSP_ENCODER_EVENT_CCW) {
        s_ui.main_sel = (uint8_t)((s_ui.main_sel + UI_MAIN_COUNT - 1U) % UI_MAIN_COUNT);
        main_ensure_visible();
        redraw_main_selection(old_sel, old_top);
        menu_tick_start();
    } else if (event == BSP_ENCODER_EVENT_SHORT) {
        enter_selected_main_item();
    }
}

static void mark_value_flash(void)
{
#if UI_ENABLE_ANIMATIONS
    s_value_flash = true;
    s_value_flash_start = s_now;
#endif
}

static void handle_octave_event(bsp_encoder_event_t event)
{
    uint8_t old = Audio_GetOctave();
    uint8_t octave = old;
    if (event == BSP_ENCODER_EVENT_CW && old < OCT_HIGH) octave = (uint8_t)(old + 1U);
    else if (event == BSP_ENCODER_EVENT_CCW && old > OCT_LOW) octave = (uint8_t)(old - 1U);
    if (old != octave) {
        Audio_SetOctave(octave);
        draw_octave_value(UI_WHITE);
        draw_octave_options();
        mark_value_flash();
#if !UI_ENABLE_ANIMATIONS
        draw_octave_value(UI_CYAN);
#endif
    }
}

static void handle_transpose_event(bsp_encoder_event_t event)
{
    int8_t old = Audio_GetTranspose();
    int8_t transpose = old;
    if (event == BSP_ENCODER_EVENT_CW && old < 2) transpose = (int8_t)(old + 1);
    else if (event == BSP_ENCODER_EVENT_CCW && old > -2) transpose = (int8_t)(old - 1);
    if (old != transpose) {
        Audio_SetTranspose(transpose);
        draw_transpose_value(UI_WHITE);
        mark_value_flash();
#if !UI_ENABLE_ANIMATIONS
        draw_transpose_value(UI_CYAN);
#endif
    }
}

static void handle_metronome_event(bsp_encoder_event_t event)
{
    /* FUNC 选焦点：BPM → 拍号；旋转只改当前焦点，短按始终开关。 */
    if ((event == BSP_ENCODER_EVENT_CW) || (event == BSP_ENCODER_EVENT_CCW)) {
        if (s_ui.metro_focus == 0U) {
            uint16_t old = s_ui.bpm;
            if (event == BSP_ENCODER_EVENT_CW && s_ui.bpm < 240U) {
                ++s_ui.bpm;
            } else if (event == BSP_ENCODER_EVENT_CCW && s_ui.bpm > 40U) {
                --s_ui.bpm;
            }
            if (old != s_ui.bpm) {
                (void)Metronome_SetBpm(metronome_bpm_clamped());
                draw_bpm_value(UI_WHITE);
                mark_value_flash();
#if !UI_ENABLE_ANIMATIONS
                draw_bpm_value(UI_CYAN);
#endif
            }
            return;
        }

        if (event == BSP_ENCODER_EVENT_CW) {
            s_ui.metro_sig = (uint8_t)((s_ui.metro_sig + 1U) % 4U);
        } else {
            s_ui.metro_sig = (uint8_t)((s_ui.metro_sig + 3U) % 4U);
        }
        (void)Metronome_SetBeatsPerBar(s_metro_beats[s_ui.metro_sig]);
        draw_metronome_sig();
        return;
    }

    /* 短按：开启 / 关闭节拍器。这是唯一的开关入口，
     * 长按只负责退出页面，不改变开关状态。 */
    if (event == BSP_ENCODER_EVENT_SHORT) {
        if (Metronome_IsRunning()) {
            Metronome_Stop();
        } else {
            (void)Metronome_SetBpm(metronome_bpm_clamped());
            Metronome_Start();
        }
        draw_metronome_state();
    }
}

static void copy_playback_name(const char *name)
{
    uint8_t i = 0U;
    while (i < sizeof(s_playback_name) - 1U && name[i] != '\0') {
        s_playback_name[i] = name[i];
        ++i;
    }
    s_playback_name[i] = '\0';
}

static void handle_library_event(bsp_encoder_event_t event)
{
    uint8_t total = library_total();
    uint8_t old = s_ui.library_sel;

    /* 曲目和录音都被移除后列表为空，任何操作都直接忽略，避免除零 */
    if (total == 0U) {
        return;
    }
    if (s_ui.library_sel >= total) {
        s_ui.library_sel = (uint8_t)(total - 1U);
    }

    if (event == BSP_ENCODER_EVENT_CW) {
        s_ui.library_sel = (uint8_t)((s_ui.library_sel + 1U) % total);
    } else if (event == BSP_ENCODER_EVENT_CCW) {
        s_ui.library_sel = (uint8_t)((s_ui.library_sel + total - 1U) % total);
    } else if (event == BSP_ENCODER_EVENT_SHORT) {
        const char *name;
        uint16_t events;
        if (!library_item(s_ui.library_sel, &name, &events)) {
            return;
        }
        copy_playback_name(name);
        s_playback_events = events;
        s_ui.playback_track = s_ui.library_sel;
        s_playback_is_record = (s_ui.library_sel >= UI_LIB_DEMO);
        s_playback_record = (s_ui.library_sel >= UI_LIB_DEMO) ?
                            (uint8_t)(s_ui.library_sel - UI_LIB_DEMO) : 0U;

        /* 录音：进入回放页但先不播放，让用户先选速度。
         * 内置曲目：没有速度概念，直接开始播放。 */
        record_playback_stop();
        Song_Stop();
        if (s_playback_is_record) {
            s_playback_started = false;
            s_ui.playing = false;
        } else {
            if (song_of_list(s_ui.library_sel)->sample != NULL) { s_playback_speed = 1U; }
            Song_SetSpeed(s_playback_speed);
            s_playback_started = Song_Play(song_of_list(s_ui.library_sel));
            s_ui.playing = s_playback_started;
        }
        enter_page(UI_PAGE_PLAYBACK);
        return;
    } else {
        return;
    }

    if (old != s_ui.library_sel) {
        if (s_ui.library_sel < s_lib_top) {
            s_lib_top = s_ui.library_sel;
        } else if (s_ui.library_sel >= s_lib_top + UI_LIB_VISIBLE) {
            s_lib_top = (uint8_t)(s_ui.library_sel - UI_LIB_VISIBLE + 1U);
        }
        draw_library_rows();
    }
}

static void draw_playback_speed_text(void);

/* 回放页：旋转选速度（3 档，未开始播放前可选），短按开始/暂停，长按返回曲库 */
static void handle_playback_event(bsp_encoder_event_t event)
{
    if (event == BSP_ENCODER_EVENT_CW || event == BSP_ENCODER_EVENT_CCW) {
        s_playback_speed = (event == BSP_ENCODER_EVENT_CW)
                         ? (uint8_t)((s_playback_speed + 1U) % 3U)
                         : (uint8_t)((s_playback_speed + 2U) % 3U);
        if (s_playback_is_record) {
            record_playback_set_speed(s_playback_speed);
        } else {
            Song_SetSpeed(s_playback_speed);
        }
        draw_playback_speed_text();

        /* Dual-track songs and PCM change speed at their current position. */
        if (!s_playback_is_record && s_playback_started) {
            const SongDef *def = song_of_list(s_ui.playback_track);
            if (def->sample != NULL || def->accompaniment_count != 0U) {
                s_ui.playing = (Song_GetState() == SONG_PLAYING);
                draw_playback_visual();
                draw_playback_state_text();
                return;
            }
        }

        /* Other monophonic builtins keep their existing restart behavior. */
        if (!s_playback_is_record && s_playback_started) {
            bool was_playing = s_ui.playing;

            Song_Stop();
            if (Song_Play(song_of_list(s_ui.playback_track))) {
                Song_SetSpeed(s_playback_speed);
                if (was_playing) {
                    s_ui.playing = true;
                } else {
                    Song_Pause();
                    s_ui.playing = false;
                }
            } else {
                s_playback_started = false;
                s_ui.playing = false;
            }
            draw_playback_visual();
            draw_playback_state_text();
            return;
        }

        /* 已经开始播了再改速度：从头按新速度重来，避免节拍错位。
         * 若当前是暂停状态，则保持暂停、只把位置回到开头，不要意外开播。 */
        if (s_playback_started && s_playback_is_record) {
            bool was_playing = s_ui.playing;

            record_playback_stop();
            if (record_playback_start(s_playback_record, s_now)) {
                record_playback_set_speed(s_playback_speed);
                if (was_playing) {
                    s_ui.playing = true;
                } else {
                    record_playback_set_paused(true, s_now);
                    s_ui.playing = false;
                }
            } else {
                s_playback_started = false;
                s_ui.playing = false;
            }
            draw_playback_visual();
            draw_playback_state_text();
        }
        return;
    }

    if (event == BSP_ENCODER_EVENT_SHORT) {
        /* 内置曲目走 Song 播放器，短按暂停 / 继续 */
        if (!s_playback_is_record) {
            if (Song_GetState() == SONG_PLAYING) {
                Song_Pause();
                s_ui.playing = false;
            } else if (Song_GetState() == SONG_PAUSED) {
                Song_Resume();
                s_ui.playing = true;
            } else {
                Song_SetSpeed(s_playback_speed);
                s_ui.playing = Song_Play(song_of_list(s_ui.playback_track));
                s_playback_started = s_ui.playing;
            }
            draw_playback_visual();
            draw_playback_state_text();
            return;
        }

        /* 第一次短按、或上一轮已经播完，都走"从头开始"这条路。
         * 只判断 s_playback_started 是不够的：播完后它仍为 true，
         * 会导致再按只切换 s_ui.playing 却永远不会重新启动回放。 */
        if (!s_playback_started || !record_playback_active()) {
            if (s_playback_is_record) {
                if (!record_playback_start(s_playback_record, s_now)) {
                    return;
                }
                record_playback_set_speed(s_playback_speed);
            }
            s_playback_started = true;
            s_ui.playing = true;
        } else {
            s_ui.playing = !s_ui.playing;
            if (s_playback_is_record) {
                record_playback_set_paused(!s_ui.playing, s_now);
            }
        }
        draw_playback_visual();
        draw_playback_state_text();
    }
}

/* ---------- 演奏页 / 录音页：FUNC 切换调节对象，编码器加减 ---------- */

/* 只改当前选中的那一个参数；取值范围与默认值完全沿用原样。
 * 三个参数住在音频模块里，这里只负责调用 Set。 */
static void ui_adjust_selected_param(int8_t dir)
{
    switch (s_ui.param_sel) {
    case 0U: {                                  /* OCT 0..2 循环 */
        int8_t v = (int8_t)Audio_GetOctave() + dir;
        if (v < 0) v = (int8_t)OCT_HIGH;
        if (v > (int8_t)OCT_HIGH) v = 0;
        Audio_SetOctave((uint8_t)v);
        break;
    }
    case 1U: {                                  /* VOL 1..5 循环 */
        int8_t v = (int8_t)Audio_GetVolume() + dir;
        if (v < 0) v = (int8_t)VOL_5;
        if (v > (int8_t)VOL_5) v = 0;
        Audio_SetVolume((uint8_t)v);
        break;
    }
    default: {                                  /* KEY -2..+2 循环 */
        int8_t v = Audio_GetTranspose() + dir;
        if (v < -2) v = 2;
        if (v > 2) v = -2;
        Audio_SetTranspose(v);
        break;
    }
    }
}

static void handle_perform_event(bsp_encoder_event_t event)
{
    if (event != BSP_ENCODER_EVENT_CW && event != BSP_ENCODER_EVENT_CCW) {
        return;
    }
    ui_adjust_selected_param((event == BSP_ENCODER_EVENT_CW) ? 1 : -1);
    draw_perform_param(s_ui.param_sel);
    /* 改的是移调，调性和音级都会变，整块乐理信息要重画 */
    if (s_ui.param_sel == 2U) {
        draw_key_info();
    }
}

static void record_open_library(uint8_t record_index)
{
    uint8_t total = library_total();
    uint8_t index = (uint8_t)(UI_LIB_DEMO + record_index);
    if (total == 0U) {
        enter_page(UI_PAGE_LIBRARY);
        return;
    }
    if (index >= total) {
        index = (uint8_t)(total - 1U);
    }
    s_ui.library_sel = index;
    if (index < s_lib_top) {
        s_lib_top = index;
    } else if (index >= s_lib_top + UI_LIB_VISIBLE) {
        s_lib_top = (uint8_t)(index - UI_LIB_VISIBLE + 1U);
    }
    s_ui.playing = false;
    enter_page(UI_PAGE_LIBRARY);
}

static void handle_record_event(bsp_encoder_event_t event)
{
    if (event == BSP_ENCODER_EVENT_CW || event == BSP_ENCODER_EVENT_CCW) {
        if (!record_is_recording() && s_ui.record_mode != 0U) {
            uint8_t count = record_track_count();
            if (count == 0U) {
                s_ui.record_sel = 0U;
            } else {
                int8_t dir = (event == BSP_ENCODER_EVENT_CW) ? 1 : -1;
                int8_t next = (int8_t)s_ui.record_sel + dir;
                if (next < 0) next = (int8_t)(count - 1U);
                if (next >= (int8_t)count) next = 0;
                s_ui.record_sel = (uint8_t)next;
            }
            s_record_error = 0U;
            draw_record_status();
            return;
        }

        /* 新建待录及录音/叠录期间，旋转调节当前参数。 */
        s_record_error = 0U;
        ui_adjust_selected_param((event == BSP_ENCODER_EVENT_CW) ? 1 : -1);
        draw_record_param(s_ui.param_sel);
        if (s_ui.param_sel == 2U) {
            draw_key_info_at(68, UI_REC_DARK);
        }
        return;
    }

    if (event != BSP_ENCODER_EVENT_SHORT) {
        return;
    }

    if (!record_is_recording()) {
        s_record_error = 0U;
        if (s_ui.record_mode != 0U) {
            uint8_t count = record_track_count();
            record_info_t info;
            if (count == 0U) {
                s_record_error = 0U;
            } else {
                if (s_ui.record_sel >= count) {
                    s_ui.record_sel = (uint8_t)(count - 1U);
                }
                if (!record_get_info(s_ui.record_sel, &info) ||
                    info.event_count >= RECORD_MAX_EVENTS) {
                    s_record_error = 2U;
                } else if (!record_overdub_start(s_ui.record_sel, s_now)) {
                    s_record_error = 1U;
                }
            }
        } else if (!record_start(s_now)) {
            s_record_error = (record_track_count() >= RECORD_MAX_TRACKS) ? 2U : 1U;
        }
        s_record_seconds_shown = 0U;
        draw_record_status();
        draw_record_timer();
        return;
    }

    if (record_overdub_active()) {
        record_result_t result = record_overdub_stop(s_now);
        if (result == RECORD_RESULT_OK) {
            uint8_t source = s_ui.record_sel;
            s_record_error = 0U;
            record_open_library(source);
        } else if (result == RECORD_RESULT_EMPTY) {
            /* 没有新音符：结束叠录但不新增条目。 */
            s_record_error = 0U;
            s_record_seconds_shown = 0U;
            draw_record_status();
            draw_record_timer();
        } else {
            s_record_error = (result == RECORD_RESULT_FULL) ? 2U : 1U;
            draw_record_status();
            draw_record_timer();
        }
        return;
    }

    record_result_t result = record_stop_and_save(s_now);
    if (result == RECORD_RESULT_OK) {
        uint8_t source = (uint8_t)(record_track_count() - 1U);
        s_record_error = 0U;
        record_open_library(source);
    } else {
        s_record_error = 1U;
        draw_record_status();
        draw_record_timer();
    }
}
static void draw_popup(void)
{
#if UI_ENABLE_ALPHA
    tft_fill_rect_blend(8, 52, 112, 58, 0x0000U, UI_PANEL, 170U);
    tft_draw_card(28, 62, 72, 34, UI_PANEL_TOP, UI_PANEL_BOTTOM,
                  UI_CYAN, UI_SHADOW, UI_HIGHLIGHT);
    tft_fill_round_rect(30, 64, 68, 30, UI_PANEL);
    tft_draw_text_ascii_5x7(35, 74, "SAVED", 2, UI_CYAN, UI_PANEL);
#endif
}

/* 设置页：旋钮直接调当前项目的值；FUNC 切换 亮度/音量/音色。 */
static void settings_apply_step(int8_t dir)
{
    uint8_t old_value = settings_value(s_ui.settings_sel);

    if (s_ui.settings_sel == 0U) {
        int16_t b = (int16_t)s_ui.brightness + dir;
        if (b < 1) { b = 5; }
        if (b > 5) { b = 1; }
        s_ui.brightness = (uint8_t)b;
        tft_set_brightness(s_ui.brightness);
    } else if (s_ui.settings_sel == 1U) {
        int16_t v = (int16_t)Audio_GetVolume() + dir;
        int16_t n = (int16_t)VOL_COUNT;
        if (v < 0) { v = (int16_t)(n - 1); }
        if (v >= n) { v = 0; }
        Audio_SetVolume((VolumeId)v);
    } else {
        int16_t t = (int16_t)Audio_GetTimbre() + dir;
        int16_t n = (int16_t)TIMBRE_COUNT;
        if (t < 0) { t = (int16_t)(n - 1); }
        if (t >= n) { t = 0; }
        Audio_SetTimbre((TimbreId)t);
    }

    draw_settings_row(s_ui.settings_sel);
#if UI_ENABLE_ANIMATIONS
    s_slider_index = s_ui.settings_sel;
    s_slider_from = old_value;
    s_slider_to = settings_value(s_ui.settings_sel);
    s_slider_start = s_now;
    s_slider_last = 0U;
    s_slider_anim = true;
    draw_settings_slider(s_slider_index, s_slider_from, true);
#endif
}

static void handle_settings_event(bsp_encoder_event_t event)
{
    if (event == BSP_ENCODER_EVENT_CW) {
        s_slider_anim = false;
        settings_apply_step(1);
    } else if (event == BSP_ENCODER_EVENT_CCW) {
        s_slider_anim = false;
        settings_apply_step(-1);
    } else if (event == BSP_ENCODER_EVENT_SHORT) {
        /* 备用入口：旋钮短按也能切 亮度/音量/音色，
         * 万一 FUNC 键接触不良或事件被吞，仍然能操作。 */
        settings_select_next();
    }
}

void ui_page_init(void)
{
    s_ui.page = UI_PAGE_HOME;   /* 开机先停在欢迎页，长按旋钮才进主菜单 */
    s_ui.main_sel = 0U;
    s_main_top = 0U;
    s_ui.bpm = 120U;
    s_ui.library_sel = 0U;
    s_ui.playback_track = 0U;
    s_ui.playing = false;
    s_ui.settings_sel = 0U;
    s_ui.brightness = 3U;
    s_ui.param_sel = 0U;
    s_ui.record_mode = 0U;
    s_ui.record_sel = 0U;
    s_ui.metro_sig = 2U;        /* 默认 4/4 */
    s_ui.metro_focus = 0U;      /* 默认旋钮调 BPM */

    s_playback_started = false;
    s_playback_speed = 1U;      /* 默认正常速度 */
    record_playback_set_speed(1U);
    Song_SetSpeed(1U);

    /* 内置曲目默认只放出"稻香"和"C4 定时炸弹"（下标 2、3）。
     * 小星星和两只老虎（下标 0、1）默认隐藏，曲库里不再出现；
     * 想恢复就把掩码改成 0x00U。 */
    {
        uint8_t st[2] = { 0xFFU, 0xFFU };
        (void)record_setting_load(st, 2U);
        /* 首次上电扇区是 0xFF，按"没有任何曲目被删除"处理 */
        s_song_hidden = (st[0] == 0xA6U) ? st[1] : 0x00U;
    }

    song_map_rebuild();

    Audio_SetOctave(OCT_MID);
    Audio_SetTranspose(0);
    Audio_SetVolume(VOL_3);
    Audio_SetTimbre(TIMBRE_ORGAN);
    s_ui.saved_brightness = 3U;
    s_ui.saved_volume = (uint8_t)(Audio_GetVolume() + 1U);
    s_ui.saved_tone = (uint8_t)(Audio_GetTimbre() + 1U);

    s_now = HAL_GetTick();
    s_value_flash = false;
    s_popup = false;
    draw_current_page();
}

void ui_page_handle_event(bsp_encoder_event_t event)
{
    s_now = HAL_GetTick();
    if (event == BSP_ENCODER_EVENT_NONE || s_popup) {
        return;
    }

    /* 音乐游戏有自己的状态机与长按语义，事件全部交给它，不能落到下面的全局长按 */
    if (s_ui.page == UI_PAGE_RHYTHM) {
        rhythm_game_handle_event(event);
        return;
    }

    if (s_ui.page == UI_PAGE_MELODY) {
        melody_maker_handle_event(event);
        return;
    }

    if (s_ui.page == UI_PAGE_DS_ROOM &&
        (event == BSP_ENCODER_EVENT_CW || event == BSP_ENCODER_EVENT_CCW)) {
        ds_room_next_dance();
        return;
    }

    if (event == BSP_ENCODER_EVENT_LONG) {
        /* 欢迎页：长按进主菜单，并放一段上行琶音作为"进入"的提示 */
        if (s_ui.page == UI_PAGE_HOME) {
            /* 开机琶音同样固定最低档，不随全局音量变响。 */
            (void)Song_PlayGain(Song_GetBootChime(), AUDIO_SFX_PERMILLE);
            enter_page(UI_PAGE_MAIN);
            return;
        }
        /* 主菜单：长按回到欢迎页 */
        if (s_ui.page == UI_PAGE_MAIN) {
            enter_page(UI_PAGE_HOME);
            return;
        }
        if (s_ui.page == UI_PAGE_RECORD) {
            record_cancel();
            s_record_error = 0U;
            enter_page(UI_PAGE_MAIN);
            return;
        }
        if (s_ui.page == UI_PAGE_SETTINGS) {
            s_ui.saved_brightness = s_ui.brightness;
            s_ui.saved_volume = (uint8_t)(Audio_GetVolume() + 1U);
            s_ui.saved_tone = (uint8_t)(Audio_GetTimbre() + 1U);
#if UI_ENABLE_ALPHA
            draw_popup();
            s_popup = true;
            s_popup_until = s_now + UI_POPUP_MS;
#else
            enter_page(UI_PAGE_MAIN);
#endif
            return;
        }
        if (s_ui.page == UI_PAGE_PLAYBACK) {
            record_playback_stop();
            Song_Stop();
            s_playback_started = false;
            s_ui.playing = false;
            s_ui.library_sel = s_ui.playback_track;
            uint8_t total = library_total();
            if (s_ui.library_sel >= total) {
                s_ui.library_sel = (total > 0U) ? (uint8_t)(total - 1U) : 0U;
            }
            if (s_ui.library_sel < s_lib_top) {
                s_lib_top = s_ui.library_sel;
            } else if (s_ui.library_sel >= s_lib_top + UI_LIB_VISIBLE) {
                s_lib_top = (uint8_t)(s_ui.library_sel - UI_LIB_VISIBLE + 1U);
            }
            enter_page(UI_PAGE_LIBRARY);
            return;
        }
        enter_page(UI_PAGE_MAIN);
        return;
    }

    switch (s_ui.page) {
        case UI_PAGE_MAIN:      handle_main_event(event); break;
        case UI_PAGE_PERFORM:   handle_perform_event(event); break;
        case UI_PAGE_OCTAVE:    handle_octave_event(event); break;
        case UI_PAGE_TRANSPOSE: handle_transpose_event(event); break;
        case UI_PAGE_METRONOME: handle_metronome_event(event); break;
        case UI_PAGE_RECORD:    handle_record_event(event); break;
        case UI_PAGE_LIBRARY:   handle_library_event(event); break;
        case UI_PAGE_PLAYBACK:  handle_playback_event(event); break;
        case UI_PAGE_SETTINGS:  handle_settings_event(event); break;
        default: break;
    }
}

void ui_page_tick(uint32_t now_ms)
{
    s_now = now_ms;

    /* 音乐游戏：tick 返回 true 表示退出请求，恢复音频状态后回主菜单 */
    if (s_ui.page == UI_PAGE_RHYTHM) {
        if (rhythm_game_tick(now_ms)) {
            rhythm_game_leave();
            s_ui.main_sel = 6U;
            enter_page(UI_PAGE_MAIN);
        }
        return;
    }

    if (s_ui.page == UI_PAGE_MELODY) {
        if (melody_maker_tick(now_ms)) {
            melody_maker_leave();
            s_ui.main_sel = 7U;
            main_ensure_visible();
            enter_page(UI_PAGE_MAIN);
        }
        return;
    }

    menu_blip_tick(now_ms);   /* 70 ms 后自动关掉菜单提示音 */
    if (s_ui.page == UI_PAGE_DS_ROOM) {
        ds_room_tick(now_ms, bsp_notes_pressed_mask());
        return;
    }
    if (s_ui.page == UI_PAGE_PERFORM && !s_slide_active && !s_popup) {
        perform_page_tick();
    }
    /* 录音页：音符高亮只做局部重绘。 */
    if (s_ui.page == UI_PAGE_RECORD && !s_slide_active && !s_popup) {
        record_page_tick();
    }

    /* 叠录时在录音页继续推进原曲回放，新音符同时写入叠加层。 */
    if (s_ui.page == UI_PAGE_RECORD && record_overdub_active()) {
        record_playback_tick(s_now);
    }

    if (s_ui.page == UI_PAGE_RECORD && record_is_recording() && !s_slide_active) {
        /* 只有秒数变化才重绘计时，避免高频刷新拖慢音符高亮响应。 */
        uint32_t seconds = record_elapsed_ms(s_now) / 1000UL;
        if (seconds != s_record_seconds_shown) {
            draw_record_timer();
        }
    }
    if (s_ui.page == UI_PAGE_PLAYBACK && s_playback_is_record && s_ui.playing) {
        record_playback_tick(s_now);
        if (!record_playback_active()) {
            /* Keep started set so the page shows completion; inactive playback restarts on short press. */
            s_ui.playing = false;
            draw_playback_visual();
            draw_playback_state_text();
        }
    }
    if (s_ui.page == UI_PAGE_PLAYBACK && !s_playback_is_record &&
        s_ui.playing && Song_GetState() == SONG_IDLE) {
        s_ui.playing = false;
        draw_playback_visual();
        draw_playback_state_text();
    }
#if UI_ENABLE_ANIMATIONS
    if (s_popup) {
        if ((int32_t)(s_now - s_popup_until) >= 0) {
            s_popup = false;
            enter_page(UI_PAGE_MAIN);
        }
        return;
    }

    if (s_slide_active) {
        uint32_t elapsed = s_now - s_slide_start;
        if (elapsed >= UI_SLIDE_FRAME && s_slide_offset != 0) {
            s_slide_offset = 0;
            draw_content_at_offset(0);
            s_slide_active = false;
        }
        return;
    }
    if (s_ui.page == UI_PAGE_PLAYBACK &&
        (uint32_t)(s_now - s_breath_last) >= UI_BREATH_MS) {
        s_breath_last = s_now;
        s_breath = (uint8_t)!s_breath;
        draw_playback_visual();
    }

    if (s_ui.page == UI_PAGE_SETTINGS && s_slider_anim) {
        uint32_t elapsed = s_now - s_slider_start;
        if (elapsed >= UI_SLIDER_MS) {
            s_slider_anim = false;
            draw_settings_slider(s_slider_index, s_slider_to, true);
        } else if ((uint32_t)(s_now - s_slider_last) >= 20U) {
            s_slider_last = s_now;
            int16_t v = (int16_t)s_slider_from +
                ((int16_t)(s_slider_to - s_slider_from) * (int16_t)elapsed) / (int16_t)UI_SLIDER_MS;
            draw_settings_slider(s_slider_index, (uint8_t)v, true);
        }
    }
    if (s_value_flash && (uint32_t)(s_now - s_value_flash_start) >= UI_VALUE_MS) {
        s_value_flash = false;
        if (s_ui.page == UI_PAGE_OCTAVE) draw_octave_value(UI_CYAN);
        else if (s_ui.page == UI_PAGE_TRANSPOSE) draw_transpose_value(UI_CYAN);
        else if (s_ui.page == UI_PAGE_METRONOME) draw_bpm_value(UI_CYAN);
    }
#endif
}

void ui_page_handle_func(void)
{
    s_now = HAL_GetTick();
    if (s_ui.page == UI_PAGE_DS_ROOM) {
        ds_room_toggle_dance(s_now);
        return;
    }

    /* 游戏内 FUNC 不参与音区/音量/移调，也不允许落到曲库删除逻辑 */
    if (s_ui.page == UI_PAGE_RHYTHM || s_ui.page == UI_PAGE_MELODY) {
        return;
    }

    /* 录音/叠录期间 FUNC 选参数；待录时切换新建/叠录模式。 */
    if (s_ui.page == UI_PAGE_RECORD) {
        if (record_is_recording()) {
            s_ui.param_sel = (uint8_t)((s_ui.param_sel + 1U) % 3U);
            draw_record_params();
            return;
        }
        s_ui.record_mode = (uint8_t)(s_ui.record_mode ^ 1U);
        if (s_ui.record_mode != 0U) {
            uint8_t count = record_track_count();
            if (count == 0U) {
                s_ui.record_sel = 0U;
            } else if (s_ui.record_sel >= count) {
                s_ui.record_sel = (uint8_t)(count - 1U);
            }
        }
        s_record_error = 0U;
        draw_record_status();
        return;
    }

    /* 演奏页：FUNC 在 OCT → VOL → KEY 之间循环切换调节对象。 */
    if (s_ui.page == UI_PAGE_PERFORM) {
        s_ui.param_sel = (uint8_t)((s_ui.param_sel + 1U) % 3U);
        draw_perform_params();
        return;
    }

    /* 节拍器页：FUNC 在 BPM → 拍号之间切换调节焦点 */
    if (s_ui.page == UI_PAGE_METRONOME) {
        s_ui.metro_focus = (uint8_t)((s_ui.metro_focus + 1U) % 2U);
        draw_metronome_page();
        return;
    }

    /* ---- 系统设置页：FUNC 在 亮度 → 音量 → 音色 之间切换 ---- */
    if (s_ui.page == UI_PAGE_SETTINGS) {
        settings_select_next();
        return;
    }


    /* ---- 曲库页：FUNC 删除当前条目 ---- */
    if (s_ui.page != UI_PAGE_LIBRARY) {
        return;
    }
    if (library_total() == 0U) {
        return;
    }

    if (s_ui.library_sel < UI_LIB_DEMO) {
        /* 内置曲目：曲谱是编译进 Flash 的常量，物理上删不掉，
         * 所以这里做的是"从列表里移除"——把对应位打进隐藏掩码后重建映射。
         * 掉电复位后这张掩码会清空，曲目会重新出现（曲谱本身没被改动）。 */
        const SongDef *def = song_of_list(s_ui.library_sel);
        if (def == NULL) {
            return;
        }
        {
            /* 反查这首歌在编译期曲目表里的下标 */
            uint8_t i;
            uint8_t n = Song_GetBuiltinCount();
            if (n > UI_SONG_MAX) {
                n = UI_SONG_MAX;
            }
            for (i = 0U; i < n; ++i) {
                if (Song_GetBuiltin(i) == def) {
                    s_song_hidden |= (uint8_t)(1U << i);
                    break;
                }
            }
        }
        song_map_rebuild();

        /* 把新的删除状态写进 Flash，掉电后依然生效 */
        {
            uint8_t st[2] = { 0xA6U, s_song_hidden };
            (void)record_setting_save(st, 2U);
        }

        /* 如果正在播这首歌，先停掉 */
        if (!s_playback_is_record) {
            Song_Stop();
            s_playback_started = false;
            s_ui.playing = false;
        }
    } else {
        if (!record_delete((uint8_t)(s_ui.library_sel - UI_LIB_DEMO))) {
            return;
        }
    }

    uint8_t total = library_total();
    if (total == 0U) {
        s_ui.library_sel = 0U;
        s_lib_top = 0U;
    } else {
        if (s_ui.library_sel >= total) {
            s_ui.library_sel = (uint8_t)(total - 1U);
        }
        if (s_ui.library_sel < s_lib_top) {
            s_lib_top = s_ui.library_sel;
        } else if (s_ui.library_sel >= s_lib_top + UI_LIB_VISIBLE) {
            s_lib_top = (uint8_t)(s_ui.library_sel - UI_LIB_VISIBLE + 1U);
        }
        if (s_lib_top + UI_LIB_VISIBLE > total) {
            s_lib_top = (total > UI_LIB_VISIBLE) ? (uint8_t)(total - UI_LIB_VISIBLE) : 0U;
        }
    }
    draw_library_rows();
}
/* FUNC 长按：全局开关节拍器。任何页面都有效，因为节拍是要跟着人走的。 */
void ui_page_handle_func_long(void)
{
    s_now = HAL_GetTick();

    /* 游戏内不切节拍器（进入游戏时已经停掉，避免全程“哒哒”） */
    if (s_ui.page == UI_PAGE_RHYTHM || s_ui.page == UI_PAGE_MELODY) {
        return;
    }

    if (Metronome_IsRunning()) {
        Metronome_Stop();
    } else {
        (void)Metronome_SetBpm(metronome_bpm_clamped());
        (void)Metronome_SetBeatsPerBar(s_metro_beats[s_ui.metro_sig]);
        Metronome_Start();
    }

    /* 只有正停在节拍器页时才需要刷新那行状态 */
    if (s_ui.page == UI_PAGE_METRONOME) {
        draw_metronome_state();
    }
}

ui_page_id_t ui_page_current(void)
{
    return s_ui.page;
}

const ui_page_state_t *ui_page_get_state(void)
{
    return &s_ui;
}
