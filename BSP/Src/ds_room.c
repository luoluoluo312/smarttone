#include "ds_room.h"

#include "bsp_tft.h"
#include "record.h"

#include <string.h>

typedef struct {
    const uint16_t *rows;
    const uint8_t *runs;
    uint32_t flash_offset;
} ds_sprite_t;

typedef struct {
    const ds_sprite_t *art;
    uint8_t center, lift, width;
    bool mirror;
} ds_step_t;

#include "ds_room_art.inc"
#include "ds_lion_art.inc"

#define WALL    0xF71EU
#define INK     0x2110U
#define NAVY    0x294FU
#define BLUE    0x5379U
#define PINK    0xF5D9U
#define GOLD    0xE638U
#define FLOOR   0xD637U
#define IVORY   0xFFDFU

#define FLOOR_Y 108
#define FOOT_Y  144
#define DANCE_FRAME_MS 92U
#define LION_FRAME_MS 80U
#define ACT_Y   94
#define ACT_END 134
#define KEY_Y   110

static const char *const s_labels[] = {"Do", "Re", "Mi", "Fa", "So", "La", "Si"};
static const uint16_t s_key_colors[] = {
    0xFCB7U, 0xFDD3U, 0xF696U, 0x9678U, 0x65FFU, 0xA4FFU, 0xE53EU
};

static uint32_t s_enter, s_last_note, s_interval;
static uint8_t s_streak, s_mask, s_note, s_body, s_face;
static uint8_t s_draw_mask, s_draw_note, s_draw_hand, s_hand;
static uint8_t s_body_row, s_action_row, s_initial_row;
static bool s_dancing, s_asset_ready, s_asset_error;
static uint16_t s_check_offset, s_dance_first_byte;
static uint32_t s_check_hash;
static uint8_t s_dance_io[232];
static uint32_t s_dance_frame_at;
static uint8_t s_dance_step, s_dance_row;
static int16_t s_paint_y, s_paint_x;

static bool s_lion, s_switch_dance, s_full_dance;
static struct {
    uint16_t rows[LION_HEIGHT + 1U];
    uint8_t damage[DANCE_END - DANCE_Y];
} s_lion_meta;
_Static_assert(sizeof(s_lion_meta) == 2U * (LION_HEIGHT + 1U) + DANCE_END - DANCE_Y,
               "Lion metadata layout must match the asset");
static ds_sprite_t s_lion_sprite = {s_lion_meta.rows, NULL, 0U};

static void star(int16_t x, int16_t y, uint16_t color)
{
    tft_draw_hline(x - 2, y, 5, color);
    tft_draw_vline(x, y - 2, 5, color);
}

static void paint_background(void)
{
    tft_fill_rect(s_paint_x, s_paint_y, 128 - s_paint_x, 2, WALL);
    tft_fill_rect(0, 18, 128, 4, 0xE61CU);

    /* Tall window, lace curtains and a small shelf on the other wall. */
    tft_fill_round_rect(6, 28, 47, 65, 0xB535U);
    tft_fill_round_rect(8, 28, 43, 61, IVORY);
    tft_fill_round_rect(11, 31, 37, 55, 0xB73EU);
    tft_fill_circle(38, 41, 6, 0xFFB8U);
    tft_fill_round_rect(13, 63, 31, 7, 0xDF9FU);
    tft_fill_round_rect(27, 58, 18, 7, 0xDF9FU);
    tft_fill_rect(28, 31, 2, 55, IVORY);
    tft_fill_rect(11, 55, 37, 2, IVORY);
    tft_fill_round_rect(6, 27, 9, 48, 0xEBB7U);
    tft_fill_round_rect(45, 27, 8, 48, 0xEBB7U);
    tft_draw_vline(12, 30, 38, 0xF5BBU);
    tft_draw_vline(47, 30, 38, 0xF5BBU);
    tft_fill_rect(6, 59, 9, 2, GOLD);
    tft_fill_rect(45, 59, 8, 2, GOLD);
    tft_fill_round_rect(4, 87, 51, 5, IVORY);
    tft_draw_hline(7, 91, 45, 0xCAEBU);

    tft_fill_round_rect(82, 35, 30, 25, 0xA42FU);
    tft_fill_round_rect(84, 37, 26, 21, IVORY);
    tft_fill_round_rect(86, 39, 22, 17, 0xA6FCU);
    tft_fill_round_rect(89, 46, 13, 7, BLUE);
    tft_fill_circle(103, 45, 2, BLUE);
    tft_fill_circle(105, 44, 1, BLUE);
    tft_draw_hline(91, 51, 7, IVORY);
    tft_fill_rect(91, 47, 1, 1, INK);
    star(88, 42, IVORY);
    tft_draw_vline(17, 80, 7, 0xAD0CU);
    tft_draw_hline(13, 86, 9, 0xAD0CU);
    tft_fill_round_rect(10, 75, 15, 6, 0xFFD9U);
    tft_draw_hline(11, 81, 13, 0xE5B3U);
    tft_fill_round_rect(92, 86, 30, 4, 0xB534U);
    tft_fill_rect(96, 74, 4, 12, 0x9375U);
    tft_fill_rect(100, 76, 4, 10, 0xCC39U);
    tft_fill_rect(104, 73, 3, 13, 0x6D57U);
    tft_fill_circle(115, 76, 4, 0x75F1U);
    tft_fill_circle(119, 73, 3, 0x6D70U);
    tft_fill_round_rect(113, 78, 8, 8, 0xE5D5U);
    star(69, 30, GOLD);
    star(117, 29, 0xDCB7U);

    tft_fill_rect(0, 102, 128, 6, 0xBF1CU);
    tft_draw_hline(0, 102, 128, IVORY);
    tft_draw_hline(0, 107, 128, 0x9CD6U);
    tft_fill_rect(0, FLOOR_Y, 128, FOOT_Y - FLOOR_Y, FLOOR);
    tft_draw_hline(0, 118, 128, 0xC574U);
    tft_draw_hline(0, 133, 128, 0xC574U);
    tft_fill_round_rect(12, 128, 104, 14, 0xB4F8U);
    tft_fill_round_rect(15, 129, 98, 10, 0xDD5CU);
    tft_draw_hline(24, 138, 80, 0xF6DDU);
}

/* Each row lists opaque runs as [start, count, palette indices...]. */
static void paint_sprite(int16_t x, int16_t y, uint8_t width, uint8_t height,
                         const ds_sprite_t *sprite, bool mirror, uint8_t draw_width)
{
    const uint16_t *palette = sprite == &s_lion_sprite ? s_lion_palette : s_ds_palette;
    uint16_t colors[86];
    for (int16_t row = s_paint_y; row < s_paint_y + 2; ++row) {
        int16_t ty = row - y;
        if (ty < 0 || ty >= height) { continue; }
        uint16_t origin = sprite->runs == NULL ? s_dance_first_byte : 0U;
        const uint8_t *data = sprite->runs == NULL ? s_dance_io : sprite->runs;
        const uint8_t *run = data + (sprite->rows[ty] - origin);
        const uint8_t *end = data + (sprite->rows[ty + 1] - origin);
        while (end - run >= 2) {
            uint8_t start = *run++;
            uint8_t count = *run++;
            if (count == 0U || start + count > width || count > end - run) { break; }
            uint8_t left = mirror ? width - start - count : start;
            if (draw_width == width) {
                for (uint8_t i = 0U; i < count; ++i) {
                    colors[i] = palette[run[mirror ? count - i - 1U : i]];
                }
                tft_draw_rgb565_image(x + left, row, count, 1, colors);
            } else {
                /* Exact inverse of floor(dst * width / draw_width). */
                uint8_t from = (uint8_t)((left * draw_width + width - 1U) / width);
                uint8_t to = (uint8_t)(((left + count) * draw_width + width - 1U) / width);
                for (uint8_t dst = from; dst < to; ++dst) {
                    uint8_t i = (uint8_t)(dst * width / draw_width - left);
                    colors[dst - from] = palette[run[mirror ? count - i - 1U : i]];
                }
                if (to > from) { tft_draw_rgb565_image(x + from, row, to - from, 1, colors); }
            }
            run += count;
        }
    }
}

static void paint_piano(void)
{
    tft_fill_round_rect(13, 124, 6, 16, 0x0841U);
    tft_fill_round_rect(106, 124, 6, 16, 0x0841U);
    tft_draw_vline(14, 132, 5, 0x52AAU);
    tft_draw_vline(107, 132, 5, 0x52AAU);
    tft_fill_round_rect(8, 103, 112, 31, 0x0841U);
    tft_fill_round_rect(12, 101, 104, 12, 0x2105U);
    tft_draw_hline(16, 102, 94, 0x6B6EU);
    tft_draw_hline(16, 106, 95, 0x39E8U);
    tft_draw_hline(11, 111, 106, 0xCE13U);
    tft_draw_hline(12, 132, 104, 0x39E8U);
    for (uint8_t i = 0U; i < 7U; ++i) {
        int16_t x = 12 + 15 * i;
        bool down = (s_draw_mask & (1U << i)) != 0U;
        uint16_t cap = down ? s_key_colors[i] : IVORY;
        tft_fill_rect(x, 112, 13, 18, down ? NAVY : 0xBDF7U);
        tft_fill_rect(x, down ? 113 : 112, 13, 16, cap);
        tft_draw_hline(x + 1, down ? 113 : 112, 11, 0xFFFFU);
        tft_draw_text_ascii_5x7(x + 1, 120, s_labels[i], 1, INK, cap);
    }
    /* Five raised accidentals, visual only. The seven front keys map to Do–Si. */
    static const uint8_t accidentals[] = {0U, 1U, 3U, 4U, 5U};
    for (uint8_t i = 0U; i < sizeof(accidentals); ++i) {
        int16_t x = 23 + 15 * accidentals[i];
        tft_fill_rect(x, 111, 6, 8, 0x0841U);
        tft_fill_rect(x + 1, 111, 4, 6, 0x2947U);
        tft_draw_hline(x + 1, 111, 4, 0x73AFU);
    }

    if (s_draw_hand != 0U) {
        int16_t from = s_draw_note < 3U ? 52 : 73;
        int16_t to = 18 + 15 * s_draw_note;
        int16_t tip = s_draw_hand == 1U ? 111 : 105;
        tft_draw_line(from, 99, to, tip - 1, INK);
        tft_draw_line(from, 100, to, tip, NAVY);
        tft_draw_line(from, 101, to, tip + 1, NAVY);
        tft_fill_circle(to, tip, 3, INK);
        tft_fill_circle(to, tip, 2, 0xFEDBU);
        star(to, 96, 0xE514U);
    }
}

static void paint_dance(void)
{
    if (s_lion) {
        tft_fill_rect(47, 133, 35, 4, 0xB499U);
        paint_sprite(LION_X, LION_Y, LION_WIDTH, LION_HEIGHT, &s_lion_sprite, false, LION_WIDTH);
        return;
    }
    const ds_step_t *step = &s_steps[s_dance_step];
    int16_t x = step->center - step->width / 2;
    tft_fill_rect(step->center - 17, 133, 35, 4, 0xB499U);
    paint_sprite(x, 39 - step->lift, 76, 96, step->art, step->mirror, step->width);
}

static void paint_room(void)
{
    if (s_paint_y < 18) {
        tft_fill_rect(0, s_paint_y, 128, 2, INK);
        tft_draw_text_ascii_5x7(29, 5, "DS", 1, GOLD, INK);
        tft_draw_text_font(&font12, 45, 2, "音乐小屋", IVORY, INK);
        tft_draw_hline(0, 17, 128, GOLD);
        return;
    }
    if (s_paint_y >= FOOT_Y) {
        tft_fill_rect(0, s_paint_y, 128, 2, INK);
        tft_draw_hline(0, FOOT_Y, 128, GOLD);
        if (s_asset_error) {
            tft_draw_text_ascii_5x7(4, 149, "DS ERR", 1, GOLD, INK);
        } else {
            tft_draw_text_ascii_5x7(4, 149, "FUNC", 1, GOLD, INK);
            tft_draw_text_font(&font12, 29, 146, s_dancing ? "弹琴" : "跳舞", IVORY, INK);
        }
        tft_draw_text_font(&font12, 62, 146, "长按返回", IVORY, INK);
        return;
    }
    paint_background();
    if (s_dancing) {
        paint_dance();
    } else {
        int16_t x = 21 + (s_body == 2U ? 1 : 0);
        int16_t y = 18 + (s_body == 1U ? 1 : 0);
        paint_sprite(x, y, 86, 109, &s_sprite, false, 86);
        if (s_face != 0U) {
            paint_sprite(x + 25, y + 40, 35, 16, s_face == 2U ? &s_smile : &s_blink, false, 35);
        }
        paint_piano();
    }
}

static bool prefetch_dance(int16_t y)
{
    const ds_sprite_t *pose = s_lion ? &s_lion_sprite : s_steps[s_dance_step].art;
    uint32_t base = s_lion ? RECORD_LION_ASSET_BASE : RECORD_DS_ASSET_BASE;
    int16_t top = y - (s_lion ? (int16_t)LION_Y : 39 - s_steps[s_dance_step].lift);
    int16_t end = top + 2;
    if (end <= 0 || top >= 96) { return true; }
    if (top < 0) { top = 0; }
    if (end > 96) { end = 96; }
    s_dance_first_byte = pose->rows[top];
    uint16_t size = pose->rows[end] - s_dance_first_byte;
    return size == 0U || (size <= sizeof(s_dance_io) &&
           record_asset_read(base + pose->flash_offset + s_dance_first_byte,
                             s_dance_io, size));
}

static void paint_strip(int16_t x, int16_t y, int16_t w)
{
    if (s_dancing && !prefetch_dance(y)) {
        s_dancing = false;
        s_asset_ready = false;
        s_asset_error = true;
        s_initial_row = 18U;
        return;
    }
    s_paint_x = x;
    s_paint_y = y;
    tft_redraw_region(x, y, w, 2, paint_room);
}

static void reset_view(uint32_t now)
{
    s_enter = s_last_note = now;
    s_interval = 340U;
    s_mask = s_note = s_streak = s_hand = s_body = s_face = 0U;
    s_draw_mask = s_draw_note = s_draw_hand = 0U;
    s_body_row = ACT_Y;
    s_action_row = ACT_END;
    s_initial_row = 0U;
    s_dancing = false;
    s_dance_frame_at = now;
    s_dance_step = 0U;
    s_dance_row = DANCE_END;
    s_lion = s_switch_dance = s_full_dance = false;
}

void ds_room_enter(uint32_t now)
{
    reset_view(now);
    s_asset_ready = false;
    s_asset_error = false;
    s_check_offset = 0U;
}

static void switch_mode(uint32_t now)
{
    bool dancing = !s_dancing;
    bool header_done = s_initial_row >= 18U;
    reset_view(now);
    s_dancing = dancing;
    if (header_done) { s_initial_row = 18U; }
}

void ds_room_toggle_dance(uint32_t now)
{
    if (s_asset_ready) {
        switch_mode(now);
    } else if (s_check_offset == 0U) {
        s_check_offset = sizeof(s_ds_dance_header);
        s_check_hash = 2166136261UL;
        s_asset_error = false;
        if (!record_asset_read(RECORD_DS_ASSET_BASE, s_dance_io, s_check_offset) ||
            memcmp(s_dance_io, s_ds_dance_header, s_check_offset) != 0) {
            s_check_offset = 0U;
            s_asset_error = true;
            if (s_initial_row >= ST7735_HEIGHT) { s_initial_row = FOOT_Y; }
        }
    }
}

static void check_dance_asset(uint32_t now)
{
    uint16_t len = DS_DANCE_ASSET_BYTES - s_check_offset;
    if (len > sizeof(s_dance_io)) { len = sizeof(s_dance_io); }
    if (!record_asset_read(RECORD_DS_ASSET_BASE + s_check_offset, s_dance_io, len)) {
        s_check_offset = 0U;
        s_asset_error = true;
        if (s_initial_row >= ST7735_HEIGHT) { s_initial_row = FOOT_Y; }
        return;
    }
    for (uint16_t i = 0U; i < len; ++i) {
        s_check_hash = (s_check_hash ^ s_dance_io[i]) * 16777619UL;
    }
    s_check_offset += len;
    if (s_check_offset == DS_DANCE_ASSET_BYTES) {
        s_check_offset = 0U;
        s_asset_ready = (s_check_hash == DS_DANCE_FNV);
        if (s_asset_ready) {
            switch_mode(now);
        } else {
            s_asset_error = true;
            if (s_initial_row >= ST7735_HEIGHT) { s_initial_row = FOOT_Y; }
        }
    }
}

void ds_room_next_dance(void)
{
    if (s_dancing) { s_switch_dance = !s_switch_dance; }
}

static bool load_lion_frame(uint8_t frame)
{
    uint32_t start = s_lion_offsets[frame];
    uint32_t end = s_lion_offsets[frame + 1U];
    uint32_t data = start + sizeof(s_lion_meta);
    if (!record_asset_read(RECORD_LION_ASSET_BASE + start, (uint8_t *)&s_lion_meta,
                           sizeof(s_lion_meta)) ||
        s_lion_meta.rows[0] != 0U || s_lion_meta.rows[LION_HEIGHT] != end - data) {
        return false;
    }
    for (uint8_t y = 0U; y < LION_HEIGHT; ++y) {
        if (s_lion_meta.rows[y] > s_lion_meta.rows[y + 1U]) { return false; }
    }
    for (uint8_t i = 0U; i < sizeof(s_lion_meta.damage); i += 2U) {
        uint8_t left = s_lion_meta.damage[i], width = s_lion_meta.damage[i + 1U];
        if (width != 0U && (left < LION_X || left + width > LION_X + LION_WIDTH)) {
            return false;
        }
    }
    s_lion_sprite.flash_offset = data;
    return true;
}

static bool start_lion(void)
{
    uint8_t hdr[sizeof(s_lion_header)];
    return record_asset_read(RECORD_LION_ASSET_BASE, hdr, sizeof(hdr)) &&
           memcmp(hdr, s_lion_header, sizeof(hdr)) == 0 && load_lion_frame(0U);
}

static void dance_tick(uint32_t now)
{
    if (s_dance_row >= DANCE_END) {
        if (s_switch_dance) {
            s_switch_dance = false;
            if (!s_lion && !start_lion()) { return; }
            s_lion = !s_lion;
            s_dance_step = 0U;
            s_full_dance = true;
        } else {
            uint32_t frame_ms = s_lion ? LION_FRAME_MS : DANCE_FRAME_MS;
            if (now - s_dance_frame_at < frame_ms) { return; }
            uint8_t count = s_lion ? LION_FRAMES : sizeof(s_steps) / sizeof(s_steps[0]);
            s_dance_step = (uint8_t)((s_dance_step + 1U) % count);
            s_full_dance = false;
            if (s_lion && !load_lion_frame(s_dance_step)) {
                s_lion = false;
                s_dance_step = 0U;
                s_full_dance = true;
            }
        }
        s_dance_frame_at = now;
        s_dance_row = DANCE_Y;
    }

    /* Finish each transition so the next frame's delta applies cleanly. */
    const uint8_t *damage = s_lion ? s_lion_meta.damage :
                            &s_dance_damage[s_dance_step * (DANCE_END - DANCE_Y)];
    while (s_dance_row < DANCE_END) {
        uint8_t y = s_dance_row;
        uint8_t at = y - DANCE_Y;
        s_dance_row += 2U;
        if (s_full_dance) {
            paint_strip(16, y, 96);
            return;
        }
        if (damage[at + 1U] != 0U) {
            paint_strip(damage[at], y, damage[at + 1U]);
            return;
        }
    }
}

#ifdef RECORD_HOST_TEST
int ds_room_test_completed_step(void)
{
    if (!s_dancing || s_initial_row < ST7735_HEIGHT || s_dance_row < DANCE_END) { return -1; }
    return s_dance_step;
}

void ds_room_test_redraw_dance(void)
{
    for (uint8_t row = DANCE_Y; row < DANCE_END; row += 2U) {
        paint_strip(0, row, ST7735_WIDTH);
    }
}
#endif

void ds_room_tick(uint32_t now, uint8_t pressed)
{
    if (s_check_offset != 0U) {
        check_dance_asset(now);
        return;
    }
    if (s_dancing) {
        if (s_initial_row < ST7735_HEIGHT) {
            paint_strip(0, s_initial_row, ST7735_WIDTH);
            if (s_dancing) {
                s_initial_row += 2U;
                if (s_initial_row >= ST7735_HEIGHT) { s_dance_frame_at = now; }
            }
        } else {
            dance_tick(now);
        }
        return;
    }
    uint8_t added = pressed & (uint8_t)~s_mask;
    s_mask = pressed;
    if (added != 0U) {
        uint32_t gap = now - s_last_note;
        if (s_streak == 0U || gap > 620U) {
            s_streak = 1U;
        } else {
            if (s_streak < 8U) { ++s_streak; }
            s_interval = gap < 140U ? 140U : gap;
        }
        s_last_note = now;
        for (uint8_t i = 0; i < 7U; ++i) {
            if ((added & (1U << i)) != 0U) { s_note = i; }
        }
    }

    if (s_initial_row < ST7735_HEIGHT) {
        paint_strip(0, s_initial_row, ST7735_WIDTH);
        s_initial_row += 2U;
        return;
    }

    uint32_t age = now - s_last_note;
    bool playing = s_streak != 0U && age < 550U;
    s_hand = playing && age < 155U ? (age < 85U ? 1U : 2U) : 0U;
    if (s_action_row == ACT_END &&
        (s_draw_mask != pressed || s_draw_hand != s_hand ||
         (s_hand != 0U && s_draw_note != s_note))) {
        s_draw_mask = pressed;
        s_draw_note = s_note;
        s_draw_hand = s_hand;
        s_action_row = ACT_Y;
    }
    if (s_action_row < ACT_END) {
        /* Update the key caps before the reaching hand, within the same sweep. */
        int16_t y = s_action_row + KEY_Y - ACT_Y;
        if (y >= ACT_END) { y -= ACT_END - ACT_Y; }
        paint_strip(8, y, 112);
        s_action_row += 2U;
        return;
    }

    if (s_body_row >= ACT_Y) {
        uint32_t since_enter = now - s_enter;
        uint8_t body = playing ? (uint8_t)(age < s_interval / 2U ? 2U : 0U)
                               : (uint8_t)((since_enter / 770U) & 1U);
        uint8_t face = playing && s_streak >= 6U ? 2U :
                       (!playing && since_enter % 3900U >= 3660U ? 1U : 0U);
        if (body == s_body && face == s_face) { return; }
        s_body = body;
        s_face = face;
        s_body_row = 18U;
        /* Include the lower sprite when its position has changed. */
        s_action_row = ACT_Y;
    }
    paint_strip(19, s_body_row, 90);
    s_body_row += 2U;
}
