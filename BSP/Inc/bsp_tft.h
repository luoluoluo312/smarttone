#ifndef BSP_TFT_H
#define BSP_TFT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>
#include "font.h"

/* Panel geometry.  The common 1.8" modules are 128x160 ST7735S panels. */
#define ST7735_WIDTH   128U
#define ST7735_HEIGHT  160U

/*
 * Some 1.8" modules have a 1-2 pixel display-RAM offset.  The default 0/0 works
 * for most green-PCB modules.  If the image is shifted or the last row/column
 * is missing, try 1/1 or 1/2 here.
 */
#ifndef ST7735_X_OFFSET
#define ST7735_X_OFFSET 0U
#endif
#ifndef ST7735_Y_OFFSET
#define ST7735_Y_OFFSET 0U
#endif

/*
 * MADCTL: MX | MY | RGB.  If red and blue are swapped on your panel, change
 * this to 0xC0 (BGR).  If the image is upside down, try 0x00/0xC8 variants.
 */
#ifndef ST7735_MADCTL_VALUE
#define ST7735_MADCTL_VALUE 0xC8U
#endif

#define ST7735_COLOR_BLACK   0x0000U
#define ST7735_COLOR_WHITE   0xFFFFU
#define ST7735_COLOR_RED     0xF800U
#define ST7735_COLOR_GREEN   0x07E0U
#define ST7735_COLOR_BLUE    0x001FU
#define ST7735_COLOR_CYAN    0x07FFU
#define ST7735_COLOR_MAGENTA 0xF81FU
#define ST7735_COLOR_YELLOW  0xFFE0U

/* SmartTone neon theme (RGB565). */
#define TFT_COLOR_BG          0x0862U
#define TFT_COLOR_PANEL       0x08C5U
#define TFT_COLOR_CYAN        0x07FFU
#define TFT_COLOR_CYAN_DARK   0x04D5U
#define TFT_COLOR_WHITE       0xFFFFU
#define TFT_COLOR_TEXT        0xE71CU
#define TFT_COLOR_GRAY        0x8410U
#define TFT_COLOR_DIM         0x2945U
#define TFT_COLOR_ORANGE      0xFB08U
#define TFT_COLOR_ORANGE_FILL 0x6182U
#define TFT_COLOR_YELLOW      0xFFE0U

typedef enum {
    TFT_ICON_PLAY = 0,
    TFT_ICON_PERFORM,
    TFT_ICON_OCTAVE,
    TFT_ICON_TRANSPOSE,
    TFT_ICON_METRONOME,
    TFT_ICON_LIBRARY,
    TFT_ICON_SETTINGS,
    TFT_ICON_RECORD,
    TFT_ICON_PAUSE,
    TFT_ICON_VOLUME,
    TFT_ICON_BRIGHTNESS,
    TFT_ICON_TONE,
    TFT_ICON_NOTE,
    TFT_ICON_GAMEPAD
} tft_icon_t;

bool tft_init(SPI_HandleTypeDef *hspi);
void tft_set_y_offset(int16_t dy);
/* Flip only the panel R/B order relative to the board default. */
void tft_set_color_order_swapped(bool swapped);

/* Compose complete final pixels in a 4-row/1KB strip, then send once.
 * paint must only draw; this synchronous helper is main-loop-only and non-reentrant. */
void tft_redraw_region(int16_t x, int16_t y, int16_t w, int16_t h,
                       void (*paint)(void));

void tft_fill_screen(uint16_t color);
void tft_draw_rgb565_image(int16_t x, int16_t y, int16_t w, int16_t h,
                           const uint16_t *pixels);
void tft_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void tft_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void tft_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);
void tft_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color);
void tft_draw_triangle_right(int16_t x, int16_t y, int16_t h, uint16_t color);

/*
 * 5x7 ASCII renderer and the 12/16/24 pixel font renderer used by the UI.
 * All renderers fill their text rectangle with bg.
 */
void tft_draw_text_ascii_5x7(int16_t x, int16_t y, const char *text,
                                uint8_t scale, uint16_t fg, uint16_t bg);

void tft_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void tft_draw_circle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
void tft_fill_circle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
void tft_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void tft_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void tft_fill_gradient_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                           uint16_t top, uint16_t bottom, bool vertical);
void tft_fill_gradient_round_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                                 uint16_t top, uint16_t bottom);
void tft_draw_card(int16_t x, int16_t y, int16_t w, int16_t h,
                   uint16_t top, uint16_t bottom,
                   uint16_t border, uint16_t shadow, uint16_t highlight);
void tft_fill_rect_blend(int16_t x, int16_t y, int16_t w, int16_t h,
                         uint16_t fg, uint16_t bg, uint8_t alpha);
void tft_draw_icon(int16_t x, int16_t y, tft_icon_t icon,
                   uint16_t color, uint8_t size);
void tft_draw_text_font(const font_t *font, int16_t x, int16_t y,
                        const char *utf8, uint16_t fg, uint16_t bg);
uint16_t tft_font_text_width(const font_t *font, const char *utf8);
/* ------------------------------------------------------------ 背光调光
 *
 * TFT BL connects to PB12; SysTick produces 200 Hz software PWM.
 * 若背光电流超过 IO 能力（8 mA），中间需要一级 NPN 做开关，不要直驱。
 * level: 1~5，对应占空比 20%~100%。
 */
void tft_backlight_init(void);
void tft_set_brightness(uint8_t level);
void tft_backlight_tick(void);   /* SysTick calls once per millisecond. */

/* ------------------------------------------------------- 背景透明的文字
 *
 * 这两组函数不填背景色，而是逐像素向 bgsrc 询问"这一点的底色是多少"，
 * 再把字形按它自带的 alpha 混合上去。
 *
 * alpha 控制整串文字的浓度（255 = 完全不透明）。给 0~254 时字形与底色
 * 做混合，用来画柔和的投影，而不是盖一整块死黑的字。
 *
 * 用途：把文字直接压在整屏图片（启动图 / 欢迎页）上，不出现任何色块。
 * 代价：每个像素都要回调一次，比对纯色背景的版本慢，只适合短字符串。
 */
typedef uint16_t (*tft_bg_lookup_t)(void *ctx, int16_t x, int16_t y);

void tft_draw_text_ascii_5x7_overlay(int16_t x, int16_t y, const char *text,
                                     uint8_t scale, uint16_t fg, uint8_t alpha,
                                     tft_bg_lookup_t bgsrc, void *ctx);

void tft_draw_text_font_overlay(const font_t *font, int16_t x, int16_t y,
                                const char *utf8, uint16_t fg, uint8_t alpha,
                                tft_bg_lookup_t bgsrc, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* BSP_TFT_H */
