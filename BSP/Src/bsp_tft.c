#include "bsp_tft.h"

#include "main.h"
#include "ui_font_ascii.h"
#include "audio_pwm.h"
#include "sample.h"


#include <stddef.h>

/* Pin macros come from main.h, which is also where CubeMX places the labels. */
#define TFT_CS_LOW()  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET)
#define TFT_CS_HIGH() HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET)
#define TFT_DC_LOW()  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET)
#define TFT_DC_HIGH() HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET)
#define TFT_RES_LOW() HAL_GPIO_WritePin(TFT_RES_GPIO_Port, TFT_RES_Pin, GPIO_PIN_RESET)
#define TFT_RES_HIGH() HAL_GPIO_WritePin(TFT_RES_GPIO_Port, TFT_RES_Pin, GPIO_PIN_SET)

#define ST7735_SPI_TIMEOUT 1000U

#define ST7735_CMD_SWRESET 0x01U
#define ST7735_CMD_SLPOUT  0x11U
#define ST7735_CMD_NORON   0x13U
#define ST7735_CMD_INVOFF  0x20U
#define ST7735_CMD_DISPON  0x29U
#define ST7735_CMD_CASET   0x2AU
#define ST7735_CMD_RASET   0x2BU
#define ST7735_CMD_RAMWR   0x2CU
#define ST7735_CMD_MADCTL  0x36U
#define ST7735_CMD_COLMOD  0x3AU

static SPI_HandleTypeDef *s_spi;
static uint8_t s_line[ST7735_WIDTH * 2U];
static int16_t s_y_offset = 0;

#define TFT_COMPOSE_ROWS 4
static uint8_t s_compose_pixels[ST7735_WIDTH * TFT_COMPOSE_ROWS * 2U];
static struct {
    bool active;
    int16_t x, y, w, h;
    int16_t window_x, window_w, cursor_x, cursor_y;
} s_compose;


static bool spi_send(const uint8_t *data, uint16_t len)
{
    if (s_spi == NULL) {
        return false;
    }
    if (len == 0U) {
        return true;
    }
    if (s_compose.active) {
        for (uint16_t i = 0U; i + 1U < len; i += 2U) {
            int16_t x = s_compose.cursor_x, y = s_compose.cursor_y;
            if (x >= s_compose.x && x < s_compose.x + s_compose.w &&
                y >= s_compose.y && y < s_compose.y + s_compose.h) {
                uint16_t at = (uint16_t)(((y - s_compose.y) * s_compose.w + x - s_compose.x) * 2);
                s_compose_pixels[at] = data[i];
                s_compose_pixels[at + 1U] = data[i + 1U];
            }
            if (++s_compose.cursor_x == s_compose.window_x + s_compose.window_w) {
                s_compose.cursor_x = s_compose.window_x;
                ++s_compose.cursor_y;
            }
        }
        return true;
    }
    /* SPI1 is shared with the sample flash. Service between TFT byte batches;
     * ST7735 retains its RAM-write cursor while CS is high. Never read flash in IRQ. */
    if (AudioPwm_PcmActive()) {
        TFT_CS_HIGH();
        Sample_Service();
        TFT_CS_LOW();
    }
    return HAL_SPI_Transmit(s_spi, (uint8_t *)data, len, ST7735_SPI_TIMEOUT) == HAL_OK;
}

static bool write_command(uint8_t command)
{
    TFT_CS_LOW();
    TFT_DC_LOW();
    bool ok = spi_send(&command, 1U);
    TFT_CS_HIGH();
    return ok;
}

static bool write_command_data(uint8_t command, const uint8_t *data, uint16_t len)
{
    TFT_CS_LOW();
    TFT_DC_LOW();
    bool ok = spi_send(&command, 1U);
    if (ok && len != 0U) {
        TFT_DC_HIGH();
        ok = spi_send(data, len);
    }
    TFT_CS_HIGH();
    return ok;
}

void tft_set_color_order_swapped(bool swapped)
{
    const uint8_t madctl = (uint8_t)(ST7735_MADCTL_VALUE ^ (swapped ? 0x08U : 0U));
    (void)write_command_data(ST7735_CMD_MADCTL, &madctl, 1U);
}

void tft_set_y_offset(int16_t dy)
{
    s_y_offset = dy;
}

static bool set_window(int16_t x, int16_t y, int16_t w, int16_t h)
{
    y = (int16_t)(y + s_y_offset);
    if (s_compose.active) {
        if (x + w <= s_compose.x || x >= s_compose.x + s_compose.w ||
            y + h <= s_compose.y || y >= s_compose.y + s_compose.h) {
            return false;
        }
        s_compose.window_x = s_compose.cursor_x = x;
        s_compose.cursor_y = y;
        s_compose.window_w = w;
        return true;
    }
    uint16_t x0 = (uint16_t)(x + (int16_t)ST7735_X_OFFSET);
    uint16_t y0 = (uint16_t)(y + (int16_t)ST7735_Y_OFFSET);
    uint16_t x1 = (uint16_t)(x0 + (uint16_t)w - 1U);
    uint16_t y1 = (uint16_t)(y0 + (uint16_t)h - 1U);

    uint8_t caset[4] = {
        (uint8_t)(x0 >> 8), (uint8_t)x0,
        (uint8_t)(x1 >> 8), (uint8_t)x1
    };
    uint8_t raset[4] = {
        (uint8_t)(y0 >> 8), (uint8_t)y0,
        (uint8_t)(y1 >> 8), (uint8_t)y1
    };

    if (!write_command_data(ST7735_CMD_CASET, caset, sizeof(caset))) {
        return false;
    }
    if (!write_command_data(ST7735_CMD_RASET, raset, sizeof(raset))) {
        return false;
    }
    return write_command(ST7735_CMD_RAMWR);
}

void tft_redraw_region(int16_t x, int16_t y, int16_t w, int16_t h,
                       void (*paint)(void))
{
    if (paint == NULL || s_compose.active || w <= 0 || h <= 0 ||
        x < 0 || y < 0 || x + w > (int16_t)ST7735_WIDTH ||
        y + h > (int16_t)ST7735_HEIGHT) {
        return;
    }
    int16_t saved_offset = s_y_offset;
    s_y_offset = 0;
    for (int16_t row = y; row < y + h; row += TFT_COMPOSE_ROWS) {
        int16_t rows = (int16_t)(y + h - row);
        if (rows > TFT_COMPOSE_ROWS) { rows = TFT_COMPOSE_ROWS; }
        s_compose.x = x;
        s_compose.y = row;
        s_compose.w = w;
        s_compose.h = rows;
        s_compose.active = true;
        paint();
        s_compose.active = false;
        if (!set_window(x, row, w, rows)) { break; }
        TFT_CS_LOW();
        TFT_DC_HIGH();
        bool ok = spi_send(s_compose_pixels, (uint16_t)(w * rows * 2));
        TFT_CS_HIGH();
        if (!ok) { break; }
    }
    s_y_offset = saved_offset;
}

static void panel_reset(void)
{
    TFT_RES_HIGH();
    HAL_Delay(5);
    TFT_RES_LOW();
    HAL_Delay(50);
    TFT_RES_HIGH();
    HAL_Delay(150);
}

bool tft_init(SPI_HandleTypeDef *hspi)
{
    static const uint8_t frmctr1[] = {0x01, 0x2C, 0x2D};
    static const uint8_t frmctr2[] = {0x01, 0x2C, 0x2D};
    static const uint8_t frmctr3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    static const uint8_t invctr[]  = {0x07};
    static const uint8_t pwctr1[]  = {0xA2, 0x02, 0x84};
    static const uint8_t pwctr2[]  = {0xC5};
    static const uint8_t pwctr3[]  = {0x0A, 0x00};
    static const uint8_t pwctr4[]  = {0x8A, 0x2A};
    static const uint8_t pwctr5[]  = {0x8A, 0xEE};
    static const uint8_t vmctr1[]  = {0x0E};
    static const uint8_t madctl[]  = {ST7735_MADCTL_VALUE};
    static const uint8_t colmod[]  = {0x05}; /* 16 bits/pixel */
    static const uint8_t caset[]   = {0x00, 0x00, 0x00, 0x7F};
    static const uint8_t raset[]   = {0x00, 0x00, 0x00, 0x9F};
    static const uint8_t gmctrp1[] = {
        0x0F, 0x1A, 0x0F, 0x18, 0x2F, 0x28, 0x20, 0x22,
        0x1F, 0x1B, 0x23, 0x37, 0x00, 0x07, 0x02, 0x10
    };
    static const uint8_t gmctrn1[] = {
        0x0F, 0x1B, 0x0F, 0x17, 0x33, 0x2C, 0x29, 0x2E,
        0x30, 0x30, 0x39, 0x3F, 0x00, 0x07, 0x03, 0x10
    };

    if (hspi == NULL) {
        return false;
    }
    s_spi = hspi;

    TFT_CS_HIGH();
    TFT_DC_LOW();
    panel_reset();

    if (!write_command(ST7735_CMD_SWRESET)) {
        return false;
    }
    HAL_Delay(150);

    if (!write_command(ST7735_CMD_SLPOUT)) {
        return false;
    }
    HAL_Delay(120);

    if (!write_command_data(0xB1, frmctr1, sizeof(frmctr1))) return false;
    if (!write_command_data(0xB2, frmctr2, sizeof(frmctr2))) return false;
    if (!write_command_data(0xB3, frmctr3, sizeof(frmctr3))) return false;
    if (!write_command_data(0xB4, invctr, sizeof(invctr)))   return false;
    if (!write_command_data(0xC0, pwctr1, sizeof(pwctr1)))   return false;
    if (!write_command_data(0xC1, pwctr2, sizeof(pwctr2)))   return false;
    if (!write_command_data(0xC2, pwctr3, sizeof(pwctr3)))   return false;
    if (!write_command_data(0xC3, pwctr4, sizeof(pwctr4)))   return false;
    if (!write_command_data(0xC4, pwctr5, sizeof(pwctr5)))   return false;
    if (!write_command_data(0xC5, vmctr1, sizeof(vmctr1)))   return false;

    if (!write_command(ST7735_CMD_INVOFF)) return false;
    if (!write_command_data(ST7735_CMD_MADCTL, madctl, sizeof(madctl))) return false;
    if (!write_command_data(ST7735_CMD_COLMOD, colmod, sizeof(colmod))) return false;

    if (!write_command_data(ST7735_CMD_CASET, caset, sizeof(caset))) return false;
    if (!write_command_data(ST7735_CMD_RASET, raset, sizeof(raset))) return false;
    if (!write_command_data(0xE0, gmctrp1, sizeof(gmctrp1))) return false;
    if (!write_command_data(0xE1, gmctrn1, sizeof(gmctrn1))) return false;

    if (!write_command(ST7735_CMD_NORON)) return false;
    HAL_Delay(10);
    if (!write_command(ST7735_CMD_DISPON)) return false;
    HAL_Delay(100);

    /* 冷启动时前面几条命令可能因面板电源未稳而丢失（现象是满屏花），
     * 这里把最关键的 COLMOD/MADCTL 再发一遍兜底。 */
    if (!write_command_data(ST7735_CMD_MADCTL, madctl, sizeof(madctl))) return false;
    if (!write_command_data(ST7735_CMD_COLMOD, colmod, sizeof(colmod))) return false;
    if (!write_command(ST7735_CMD_DISPON)) return false;
    HAL_Delay(20);

    return true;
}

void tft_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) {
        w = (int16_t)(w + x);
        x = 0;
    }
    if (y < 0) {
        h = (int16_t)(h + y);
        y = 0;
    }
    if (x >= (int16_t)ST7735_WIDTH || y >= (int16_t)ST7735_HEIGHT) {
        return;
    }
    if (x + w > (int16_t)ST7735_WIDTH) {
        w = (int16_t)((int16_t)ST7735_WIDTH - x);
    }
    if (y + h > (int16_t)ST7735_HEIGHT) {
        h = (int16_t)((int16_t)ST7735_HEIGHT - y);
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    if (s_compose.active) {
        int16_t top = (int16_t)(s_compose.y - s_y_offset);
        int16_t right = (int16_t)(s_compose.x + s_compose.w);
        int16_t bottom = (int16_t)(top + s_compose.h);
        if (x < s_compose.x) { w -= s_compose.x - x; x = s_compose.x; }
        if (y < top) { h -= top - y; y = top; }
        if (x + w > right) { w = (int16_t)(right - x); }
        if (y + h > bottom) { h = (int16_t)(bottom - y); }
        if (w <= 0 || h <= 0) { return; }
    }
    if (!set_window(x, y, w, h)) {
        return;
    }

    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)color;
    for (uint16_t i = 0; i < ST7735_WIDTH; ++i) {
        s_line[i * 2U] = hi;
        s_line[i * 2U + 1U] = lo;
    }

    TFT_CS_LOW();
    TFT_DC_HIGH();
    uint32_t remaining = (uint32_t)w * (uint32_t)h;
    while (remaining != 0U) {
        uint16_t pixels = (remaining > ST7735_WIDTH) ? ST7735_WIDTH : (uint16_t)remaining;
        if (!spi_send(s_line, (uint16_t)(pixels * 2U))) {
            break;
        }
        remaining -= pixels;
    }
    TFT_CS_HIGH();
}

void tft_fill_screen(uint16_t color)
{
    tft_fill_rect(0, 0, (int16_t)ST7735_WIDTH, (int16_t)ST7735_HEIGHT, color);
}

/*
 * 启动图专用的 RGB565 图像块写入。图像数据为行优先；调用方可用一行
 * 256 B 的临时缓冲逐行送入，避免为整幅图占用 SRAM。
 */
void tft_draw_rgb565_image(int16_t x, int16_t y, int16_t w, int16_t h,
                           const uint16_t *pixels)
{
    if (pixels == NULL || w <= 0 || h <= 0 ||
        x < 0 || y < 0 ||
        (x + w) > (int16_t)ST7735_WIDTH ||
        (y + h) > (int16_t)ST7735_HEIGHT) {
        return;
    }
    if (!set_window(x, y, w, h)) {
        return;
    }

    TFT_CS_LOW();
    TFT_DC_HIGH();
    uint32_t total = (uint32_t)w * (uint32_t)h;
    uint32_t index = 0U;
    while (total != 0U) {
        uint16_t chunk = (total > ST7735_WIDTH) ? (uint16_t)ST7735_WIDTH : (uint16_t)total;
        for (uint16_t i = 0; i < chunk; ++i) {
            uint16_t color = pixels[index++];
            s_line[i * 2U] = (uint8_t)(color >> 8);
            s_line[i * 2U + 1U] = (uint8_t)color;
        }
        if (!spi_send(s_line, (uint16_t)(chunk * 2U))) {
            break;
        }
        total -= chunk;
    }
    TFT_CS_HIGH();
}

void tft_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (w <= 2 || h <= 2) {
        tft_fill_rect(x, y, w, h, color);
        return;
    }
    tft_fill_rect(x, y, w, 1, color);
    tft_fill_rect(x, (int16_t)(y + h - 1), w, 1, color);
    tft_fill_rect(x, y, 1, h, color);
    tft_fill_rect((int16_t)(x + w - 1), y, 1, h, color);
}

static uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t alpha)
{
    uint32_t a = alpha;
    uint32_t inv = 255U - a;
    uint32_t fr = (fg >> 11) & 0x1FU;
    uint32_t fg6 = (fg >> 5) & 0x3FU;
    uint32_t fb = fg & 0x1FU;
    uint32_t br = (bg >> 11) & 0x1FU;
    uint32_t bg6 = (bg >> 5) & 0x3FU;
    uint32_t bb = bg & 0x1FU;

    uint32_t r = (fr * a + br * inv + 127U) / 255U;
    uint32_t g = (fg6 * a + bg6 * inv + 127U) / 255U;
    uint32_t b = (fb * a + bb * inv + 127U) / 255U;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint32_t utf8_next(const char **cursor)
{
    const uint8_t *s = (const uint8_t *)*cursor;
    uint32_t cp;
    uint8_t n;

    if (s[0] == 0U) {
        return 0U;
    }
    if (s[0] < 0x80U) {
        *cursor += 1;
        return s[0];
    }
    if ((s[0] & 0xE0U) == 0xC0U && s[1] != 0U) {
        cp = ((uint32_t)(s[0] & 0x1FU) << 6) | (uint32_t)(s[1] & 0x3FU);
        n = 2U;
    } else if ((s[0] & 0xF0U) == 0xE0U && s[1] != 0U && s[2] != 0U) {
        cp = ((uint32_t)(s[0] & 0x0FU) << 12) |
             ((uint32_t)(s[1] & 0x3FU) << 6) |
             (uint32_t)(s[2] & 0x3FU);
        n = 3U;
    } else {
        *cursor += 1;
        return 0xFFFDU;
    }
    *cursor += n;
    return cp;
}

static const ui_ascii_glyph_t *find_ascii_glyph(char code)
{
    for (uint16_t i = 0; i < ui_ascii_glyph_count; ++i) {
        if (ui_ascii_glyphs[i].code == code) {
            return &ui_ascii_glyphs[i];
        }
    }
    return NULL;
}

void tft_draw_text_ascii_5x7(int16_t x, int16_t y, const char *text,
                                uint8_t scale, uint16_t fg, uint16_t bg)
{
    if (text == NULL || scale == 0U) {
        return;
    }

    uint16_t len = 0;
    while (text[len] != '\0' && len < 32U) {
        ++len;
    }
    if (len == 0U) {
        return;
    }

    int32_t cell_w = (int32_t)6 * (int32_t)scale;
    int32_t text_w = (int32_t)len * cell_w - (int32_t)scale;
    int32_t text_h = (int32_t)UI_ASCII_H * (int32_t)scale;
    int32_t x0 = x;
    int32_t y0 = y;
    int32_t x1 = x0 + text_w;
    int32_t y1 = y0 + text_h;
    if (x1 <= 0 || y1 <= 0 || x0 >= (int32_t)ST7735_WIDTH ||
        y0 >= (int32_t)ST7735_HEIGHT) {
        return;
    }

    int32_t cx0 = (x0 < 0) ? 0 : x0;
    int32_t cy0 = (y0 < 0) ? 0 : y0;
    int32_t cx1 = (x1 > (int32_t)ST7735_WIDTH) ? (int32_t)ST7735_WIDTH : x1;
    int32_t cy1 = (y1 > (int32_t)ST7735_HEIGHT) ? (int32_t)ST7735_HEIGHT : y1;
    int32_t draw_w = cx1 - cx0;
    int32_t draw_h = cy1 - cy0;
    if (draw_w <= 0 || draw_h <= 0) {
        return;
    }
    if (!set_window((int16_t)cx0, (int16_t)cy0, (int16_t)draw_w, (int16_t)draw_h)) {
        return;
    }

    TFT_CS_LOW();
    TFT_DC_HIGH();
    for (int32_t gy = cy0; gy < cy1; ++gy) {
        int32_t row = gy - y0;
        uint16_t out = 0;
        for (int32_t gx = cx0; gx < cx1; ++gx, ++out) {
            int32_t local = gx - x0;
            int32_t idx = local / cell_w;
            int32_t in_cell = local - idx * cell_w;
            uint16_t color = bg;

            if (idx >= 0 && idx < (int32_t)len && in_cell >= 0 &&
                in_cell < (int32_t)5 * (int32_t)scale) {
                const ui_ascii_glyph_t *glyph = find_ascii_glyph(text[idx]);
                if (glyph != NULL) {
                    int32_t gx_col = in_cell / (int32_t)scale;
                    int32_t gy_row = row / (int32_t)scale;
                    if (gy_row >= 0 && gy_row < (int32_t)UI_ASCII_H) {
                        uint8_t mask = (uint8_t)(1U << (4 - (uint8_t)gx_col));
                        if ((glyph->rows[gy_row] & mask) != 0U) {
                            color = fg;
                        }
                    }
                }
            }

            s_line[out * 2U] = (uint8_t)(color >> 8);
            s_line[out * 2U + 1U] = (uint8_t)color;
        }
        if (!spi_send(s_line, (uint16_t)(draw_w * 2))) {
            break;
        }
    }
    TFT_CS_HIGH();
}
void tft_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    tft_fill_rect(x, y, w, 1, color);
}

void tft_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    tft_fill_rect(x, y, 1, h, color);
}

void tft_draw_triangle_right(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    if (h <= 0) {
        return;
    }
    int16_t half = (int16_t)(h / 2);
    for (int16_t i = 0; i <= half; ++i) {
        int16_t len = (int16_t)(h - 2 * i);
        if (len <= 0) {
            break;
        }
        tft_fill_rect((int16_t)(x + i), (int16_t)(y + i), 1, len, color);
    }
}

void tft_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    int16_t dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (int16_t)(dx - dy);

    for (;;) {
        tft_fill_rect(x0, y0, 1, 1, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int16_t e2 = (int16_t)(2 * err);
        if (e2 > -dy) {
            err = (int16_t)(err - dy);
            x0 = (int16_t)(x0 + sx);
        }
        if (e2 < dx) {
            err = (int16_t)(err + dx);
            y0 = (int16_t)(y0 + sy);
        }
    }
}

void tft_draw_circle(int16_t cx, int16_t cy, int16_t r, uint16_t color)
{
    int16_t x = 0;
    int16_t y = r;
    int16_t d = (int16_t)(3 - 2 * r);
    while (x <= y) {
        tft_fill_rect((int16_t)(cx + x), (int16_t)(cy + y), 1, 1, color);
        tft_fill_rect((int16_t)(cx - x), (int16_t)(cy + y), 1, 1, color);
        tft_fill_rect((int16_t)(cx + x), (int16_t)(cy - y), 1, 1, color);
        tft_fill_rect((int16_t)(cx - x), (int16_t)(cy - y), 1, 1, color);
        tft_fill_rect((int16_t)(cx + y), (int16_t)(cy + x), 1, 1, color);
        tft_fill_rect((int16_t)(cx - y), (int16_t)(cy + x), 1, 1, color);
        tft_fill_rect((int16_t)(cx + y), (int16_t)(cy - x), 1, 1, color);
        tft_fill_rect((int16_t)(cx - y), (int16_t)(cy - x), 1, 1, color);
        if (d < 0) {
            d = (int16_t)(d + 4 * x + 6);
        } else {
            d = (int16_t)(d + 4 * (x - y) + 10);
            --y;
        }
        ++x;
    }
}

void tft_fill_circle(int16_t cx, int16_t cy, int16_t r, uint16_t color)
{
    for (int16_t y = -r; y <= r; ++y) {
        int16_t x = 0;
        while ((int32_t)(x + 1) * (x + 1) + (int32_t)y * y <= (int32_t)r * r) {
            ++x;
        }
        tft_fill_rect((int16_t)(cx - x), (int16_t)(cy + y),
                      (int16_t)(2 * x + 1), 1, color);
    }
}

static uint16_t lerp565(uint16_t a, uint16_t b, uint16_t t, uint16_t max)
{
    if (max == 0U) return a;
    uint32_t ar = (a >> 11) & 0x1FU, ag = (a >> 5) & 0x3FU, ab = a & 0x1FU;
    uint32_t br = (b >> 11) & 0x1FU, bg = (b >> 5) & 0x3FU, bb = b & 0x1FU;
    uint32_t r = (ar * (max - t) + br * t) / max;
    uint32_t g = (ag * (max - t) + bg * t) / max;
    uint32_t bl = (ab * (max - t) + bb * t) / max;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

static int16_t round_inset(int16_t row, int16_t h)
{
    if (h < 6) return 0;
    if (row == 0) return 3;
    if (row == 1 || row == 2) return 1;
    if (row == h - 1) return 3;
    if (row == h - 2 || row == h - 3) return 1;
    return 0;
}
void tft_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int16_t row = 0; row < h; ++row) {
        int16_t inset = round_inset(row, h);
        if (inset * 2 >= w) {
            continue;
        }
        tft_fill_rect((int16_t)(x + inset), (int16_t)(y + row),
                      (int16_t)(w - 2 * inset), 1, color);
    }
}

void tft_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int16_t row = 0; row < h; ++row) {
        int16_t inset = round_inset(row, h);
        tft_fill_rect((int16_t)(x + inset), (int16_t)(y + row), 1, 1, color);
        tft_fill_rect((int16_t)(x + w - 1 - inset), (int16_t)(y + row), 1, 1, color);
    }
    tft_draw_hline((int16_t)(x + 3), y, (int16_t)(w - 6), color);
    tft_draw_hline((int16_t)(x + 3), (int16_t)(y + h - 1), (int16_t)(w - 6), color);
}

void tft_fill_gradient_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                           uint16_t top, uint16_t bottom, bool vertical)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) { w = (int16_t)(w + x); x = 0; }
    if (y < 0) { h = (int16_t)(h + y); y = 0; }
    if (x >= (int16_t)ST7735_WIDTH || y >= (int16_t)ST7735_HEIGHT) {
        return;
    }
    if (x + w > (int16_t)ST7735_WIDTH) w = (int16_t)((int16_t)ST7735_WIDTH - x);
    if (y + h > (int16_t)ST7735_HEIGHT) h = (int16_t)((int16_t)ST7735_HEIGHT - y);
    if (w <= 0 || h <= 0 || !set_window(x, y, w, h)) {
        return;
    }

    TFT_CS_LOW();
    TFT_DC_HIGH();
    for (int16_t row = 0; row < h; ++row) {
        if (vertical) {
            uint16_t color = lerp565(top, bottom, (uint16_t)row, (uint16_t)(h - 1));
            for (int16_t col = 0; col < w; ++col) {
                s_line[col * 2] = (uint8_t)(color >> 8);
                s_line[col * 2 + 1] = (uint8_t)color;
            }
        } else if (row == 0) {
            for (int16_t col = 0; col < w; ++col) {
                uint16_t color = lerp565(top, bottom, (uint16_t)col, (uint16_t)(w - 1));
                s_line[col * 2] = (uint8_t)(color >> 8);
                s_line[col * 2 + 1] = (uint8_t)color;
            }
        }
        if (!spi_send(s_line, (uint16_t)(w * 2))) {
            break;
        }
    }
    TFT_CS_HIGH();
}

void tft_fill_gradient_round_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                                 uint16_t top, uint16_t bottom)
{
    for (int16_t row = 0; row < h; ++row) {
        int16_t inset = round_inset(row, h);
        if (inset * 2 >= w) {
            continue;
        }
        uint16_t color = lerp565(top, bottom, (uint16_t)row, (uint16_t)(h - 1));
        tft_fill_rect((int16_t)(x + inset), (int16_t)(y + row),
                      (int16_t)(w - 2 * inset), 1, color);
    }
}

void tft_draw_card(int16_t x, int16_t y, int16_t w, int16_t h,
                   uint16_t top, uint16_t bottom,
                   uint16_t border, uint16_t shadow, uint16_t highlight)
{
    tft_fill_round_rect((int16_t)(x + 1), (int16_t)(y + 1), w, h, shadow);
    tft_fill_gradient_round_rect(x, y, w, h, top, bottom);
    tft_draw_round_rect(x, y, w, h, border);
    tft_draw_hline((int16_t)(x + 4), (int16_t)(y + 1), (int16_t)(w - 8), highlight);
}

void tft_fill_rect_blend(int16_t x, int16_t y, int16_t w, int16_t h,
                         uint16_t fg, uint16_t bg, uint8_t alpha)
{
    tft_fill_rect(x, y, w, h, blend565(fg, bg, alpha));
}

static void icon_note(int16_t x, int16_t y, uint16_t c)
{
    tft_draw_circle((int16_t)(x + 5), (int16_t)(y + 11), 3, c);
    tft_draw_line((int16_t)(x + 8), (int16_t)(y + 11), (int16_t)(x + 8), (int16_t)(y + 3), c);
    tft_draw_line((int16_t)(x + 8), (int16_t)(y + 3), (int16_t)(x + 13), (int16_t)(y + 5), c);
}

void tft_draw_icon(int16_t x, int16_t y, tft_icon_t icon, uint16_t color, uint8_t size)
{
    (void)size;
    switch (icon) {
        case TFT_ICON_PLAY:
        case TFT_ICON_PERFORM:
            if (icon == TFT_ICON_PERFORM) {
                icon_note(x, y, color);
            } else {
                tft_draw_triangle_right((int16_t)(x + 4), (int16_t)(y + 3), 10, color);
            }
            break;
        case TFT_ICON_OCTAVE:
            tft_draw_line((int16_t)(x + 4), (int16_t)(y + 11), (int16_t)(x + 8), (int16_t)(y + 7), color);
            tft_draw_line((int16_t)(x + 8), (int16_t)(y + 7), (int16_t)(x + 12), (int16_t)(y + 11), color);
            tft_draw_line((int16_t)(x + 4), (int16_t)(y + 6), (int16_t)(x + 8), (int16_t)(y + 2), color);
            tft_draw_line((int16_t)(x + 8), (int16_t)(y + 2), (int16_t)(x + 12), (int16_t)(y + 6), color);
            break;
        case TFT_ICON_TRANSPOSE:
            tft_draw_line((int16_t)(x + 5), (int16_t)(y + 12), (int16_t)(x + 5), (int16_t)(y + 4), color);
            tft_draw_line((int16_t)(x + 2), (int16_t)(y + 7), (int16_t)(x + 5), (int16_t)(y + 4), color);
            tft_draw_line((int16_t)(x + 8), (int16_t)(y + 4), (int16_t)(x + 11), (int16_t)(y + 4), color);
            tft_draw_line((int16_t)(x + 11), (int16_t)(y + 4), (int16_t)(x + 11), (int16_t)(y + 12), color);
            tft_draw_line((int16_t)(x + 8), (int16_t)(y + 9), (int16_t)(x + 11), (int16_t)(y + 12), color);
            break;
        case TFT_ICON_METRONOME:
            tft_draw_line((int16_t)(x + 5), (int16_t)(y + 3), (int16_t)(x + 3), (int16_t)(y + 13), color);
            tft_draw_line((int16_t)(x + 11), (int16_t)(y + 3), (int16_t)(x + 13), (int16_t)(y + 13), color);
            tft_draw_line((int16_t)(x + 5), (int16_t)(y + 3), (int16_t)(x + 11), (int16_t)(y + 3), color);
            tft_draw_line((int16_t)(x + 3), (int16_t)(y + 13), (int16_t)(x + 13), (int16_t)(y + 13), color);
            tft_draw_line((int16_t)(x + 8), (int16_t)(y + 13), (int16_t)(x + 12), (int16_t)(y + 5), color);
            break;
        case TFT_ICON_LIBRARY:
            tft_draw_line((int16_t)(x + 2), (int16_t)(y + 4), (int16_t)(x + 9), (int16_t)(y + 4), color);
            tft_draw_line((int16_t)(x + 2), (int16_t)(y + 8), (int16_t)(x + 9), (int16_t)(y + 8), color);
            tft_draw_line((int16_t)(x + 2), (int16_t)(y + 12), (int16_t)(x + 7), (int16_t)(y + 12), color);
            icon_note((int16_t)(x + 7), (int16_t)(y + 1), color);
            break;
        case TFT_ICON_SETTINGS:
            tft_draw_circle((int16_t)(x + 8), (int16_t)(y + 8), 4, color);
            tft_draw_line((int16_t)(x + 8), (int16_t)(y + 1), (int16_t)(x + 8), (int16_t)(y + 4), color);
            tft_draw_line((int16_t)(x + 8), (int16_t)(y + 12), (int16_t)(x + 8), (int16_t)(y + 15), color);
            tft_draw_line((int16_t)(x + 1), (int16_t)(y + 8), (int16_t)(x + 4), (int16_t)(y + 8), color);
            tft_draw_line((int16_t)(x + 12), (int16_t)(y + 8), (int16_t)(x + 15), (int16_t)(y + 8), color);
            break;
        case TFT_ICON_RECORD:
            tft_fill_circle((int16_t)(x + 8), (int16_t)(y + 8), 4, color);
            break;
        case TFT_ICON_GAMEPAD:
            /* 16x16 手柄：圆角机身 + 左侧十字 + 右侧两个按键 */
            tft_draw_round_rect((int16_t)(x + 1), (int16_t)(y + 5), 14, 9, color);
            tft_draw_hline((int16_t)(x + 4), (int16_t)(y + 9), 3, color);
            tft_draw_vline((int16_t)(x + 5), (int16_t)(y + 8), 3, color);
            tft_fill_circle((int16_t)(x + 11), (int16_t)(y + 8), 1, color);
            tft_fill_circle((int16_t)(x + 13), (int16_t)(y + 10), 1, color);
            break;
        case TFT_ICON_PAUSE:
            tft_fill_rect((int16_t)(x + 4), (int16_t)(y + 3), 3, 10, color);
            tft_fill_rect((int16_t)(x + 10), (int16_t)(y + 3), 3, 10, color);
            break;
        case TFT_ICON_VOLUME:
            tft_fill_rect((int16_t)(x + 3), (int16_t)(y + 6), 3, 5, color);
            tft_draw_line((int16_t)(x + 6), (int16_t)(y + 6), (int16_t)(x + 10), (int16_t)(y + 3), color);
            tft_draw_line((int16_t)(x + 6), (int16_t)(y + 10), (int16_t)(x + 10), (int16_t)(y + 13), color);
            tft_draw_circle((int16_t)(x + 9), (int16_t)(y + 8), 4, color);
            break;
        case TFT_ICON_BRIGHTNESS:
            tft_draw_circle((int16_t)(x + 8), (int16_t)(y + 8), 3, color);
            tft_draw_line((int16_t)(x + 8), (int16_t)(y + 1), (int16_t)(x + 8), (int16_t)(y + 3), color);
            tft_draw_line((int16_t)(x + 8), (int16_t)(y + 13), (int16_t)(x + 8), (int16_t)(y + 15), color);
            tft_draw_line((int16_t)(x + 1), (int16_t)(y + 8), (int16_t)(x + 3), (int16_t)(y + 8), color);
            tft_draw_line((int16_t)(x + 13), (int16_t)(y + 8), (int16_t)(x + 15), (int16_t)(y + 8), color);
            break;
        case TFT_ICON_TONE:
            tft_fill_rect((int16_t)(x + 3), (int16_t)(y + 7), 2, 6, color);
            tft_fill_rect((int16_t)(x + 7), (int16_t)(y + 3), 2, 10, color);
            tft_fill_rect((int16_t)(x + 11), (int16_t)(y + 6), 2, 4, color);
            break;
        case TFT_ICON_NOTE:
        default:
            icon_note(x, y, color);
            break;
    }
}

static const font_glyph_t *font_glyph(const font_t *font, uint32_t codepoint)
{
    for (uint16_t i = 0; i < font->count; ++i) {
        if (font->glyphs[i].codepoint == codepoint) {
            return &font->glyphs[i];
        }
    }
    return NULL;
}

uint16_t tft_font_text_width(const font_t *font, const char *utf8)
{
    if (font == NULL || utf8 == NULL) {
        return 0U;
    }
    const char *p = utf8;
    uint16_t width = 0;
    while (*p != '\0') {
        const font_glyph_t *glyph = font_glyph(font, utf8_next(&p));
        if (glyph != NULL) {
            width = (uint16_t)(width + font->width + font->gap);
        }
    }
    if (width >= font->gap) {
        width = (uint16_t)(width - font->gap);
    }
    return width;
}

void tft_draw_text_font(const font_t *font, int16_t x, int16_t y,
                        const char *utf8, uint16_t fg, uint16_t bg)
{
    if (font == NULL || utf8 == NULL) {
        return;
    }
    const font_glyph_t *glyphs[16];
    uint16_t count = 0;
    const char *p = utf8;
    while (*p != '\0' && count < 16U) {
        const font_glyph_t *glyph = font_glyph(font, utf8_next(&p));
        if (glyph != NULL) {
            glyphs[count++] = glyph;
        }
    }
    if (count == 0U) {
        return;
    }

    uint16_t text_w = (uint16_t)(count * (font->width + font->gap) - font->gap);
    int32_t x0 = x, y0 = y;
    int32_t x1 = x0 + text_w;
    int32_t y1 = y0 + font->height;
    if (x1 <= 0 || y1 <= 0 || x0 >= (int32_t)ST7735_WIDTH || y0 >= (int32_t)ST7735_HEIGHT) {
        return;
    }
    int32_t cx0 = (x0 < 0) ? 0 : x0;
    int32_t cy0 = (y0 < 0) ? 0 : y0;
    int32_t cx1 = (x1 > (int32_t)ST7735_WIDTH) ? (int32_t)ST7735_WIDTH : x1;
    int32_t cy1 = (y1 > (int32_t)ST7735_HEIGHT) ? (int32_t)ST7735_HEIGHT : y1;
    if (!set_window((int16_t)cx0, (int16_t)cy0,
                    (int16_t)(cx1 - cx0), (int16_t)(cy1 - cy0))) {
        return;
    }

    uint16_t lut[16];
    for (uint8_t a = 0; a < 16U; ++a) {
        lut[a] = blend565(fg, bg, (uint8_t)(a * 17U));
    }

    TFT_CS_LOW();
    TFT_DC_HIGH();
    for (int32_t gy = cy0; gy < cy1; ++gy) {
        int32_t row = gy - y0;
        uint16_t out = 0;
        for (int32_t gx = cx0; gx < cx1; ++gx, ++out) {
            int32_t cell = (gx - x0) / (font->width + font->gap);
            int32_t col = (gx - x0) - cell * (font->width + font->gap);
            uint16_t color = bg;
            if (cell >= 0 && cell < (int32_t)count &&
                col >= 0 && col < (int32_t)font->width) {
                const font_glyph_t *glyph = glyphs[cell];
                uint32_t bit = (uint32_t)row * font->width + (uint32_t)col;
                if (font->bpp == 4U) {
                    uint8_t packed = glyph->data[bit >> 1];
                    uint8_t alpha = (bit & 1U) ? (packed & 0x0FU) : (uint8_t)(packed >> 4);
                    color = lut[alpha];
                } else {
                    uint8_t packed = glyph->data[bit >> 3];
                    if ((packed & (uint8_t)(0x80U >> (bit & 7U))) != 0U) {
                        color = fg;
                    }
                }
            }
            s_line[out * 2] = (uint8_t)(color >> 8);
            s_line[out * 2 + 1] = (uint8_t)color;
        }
        if (!spi_send(s_line, (uint16_t)((cx1 - cx0) * 2))) {
            break;
        }
    }
    TFT_CS_HIGH();
}
/* ==================================================================== 透明叠加
 *
 * 下面两个函数与 tft_draw_text_* 的区别只有一个：底色不再是一个固定值，
 * 而是每像素向 bgsrc 回调询问。这样文字可以直接压在整屏图片上，
 * 不产生任何矩形色块。
 */
void tft_draw_text_ascii_5x7_overlay(int16_t x, int16_t y, const char *text,
                                     uint8_t scale, uint16_t fg, uint8_t alpha,
                                     tft_bg_lookup_t bgsrc, void *ctx)
{
    if (text == NULL || scale == 0U || bgsrc == NULL) {
        return;
    }

    uint16_t len = 0;
    while (text[len] != '\0' && len < 32U) {
        ++len;
    }
    if (len == 0U) {
        return;
    }

    int32_t cell_w = (int32_t)6 * (int32_t)scale;
    int32_t text_w = (int32_t)len * cell_w - (int32_t)scale;
    int32_t text_h = (int32_t)UI_ASCII_H * (int32_t)scale;
    int32_t x0 = x, y0 = y;
    int32_t cx0 = (x0 < 0) ? 0 : x0;
    int32_t cy0 = (y0 < 0) ? 0 : y0;
    int32_t cx1 = (x0 + text_w > (int32_t)ST7735_WIDTH) ? (int32_t)ST7735_WIDTH : (x0 + text_w);
    int32_t cy1 = (y0 + text_h > (int32_t)ST7735_HEIGHT) ? (int32_t)ST7735_HEIGHT : (y0 + text_h);
    if (cx1 <= cx0 || cy1 <= cy0) {
        return;
    }
    if (!set_window((int16_t)cx0, (int16_t)cy0,
                    (int16_t)(cx1 - cx0), (int16_t)(cy1 - cy0))) {
        return;
    }

    TFT_CS_LOW();
    TFT_DC_HIGH();
    for (int32_t gy = cy0; gy < cy1; ++gy) {
        int32_t row = gy - y0;
        uint16_t out = 0;
        for (int32_t gx = cx0; gx < cx1; ++gx, ++out) {
            int32_t local = gx - x0;
            int32_t idx = local / cell_w;
            int32_t in_cell = local - idx * cell_w;
            uint16_t bgpix = bgsrc(ctx, (int16_t)gx, (int16_t)gy);
            uint16_t color = bgpix;

            if (idx >= 0 && idx < (int32_t)len && in_cell >= 0 &&
                in_cell < (int32_t)5 * (int32_t)scale) {
                const ui_ascii_glyph_t *glyph = find_ascii_glyph(text[idx]);
                if (glyph != NULL) {
                    int32_t gcol = in_cell / (int32_t)scale;
                    int32_t grow = row / (int32_t)scale;
                    if (grow >= 0 && grow < (int32_t)UI_ASCII_H) {
                        uint8_t mask = (uint8_t)(1U << (4 - (uint8_t)gcol));
                        if ((glyph->rows[grow] & mask) != 0U) {
                            color = (alpha == 255U) ? fg : blend565(fg, bgpix, alpha);
                        }
                    }
                }
            }
            s_line[out * 2] = (uint8_t)(color >> 8);
            s_line[out * 2 + 1] = (uint8_t)color;
        }
        if (!spi_send(s_line, (uint16_t)((cx1 - cx0) * 2))) {
            break;
        }
    }
    TFT_CS_HIGH();
}

void tft_draw_text_font_overlay(const font_t *font, int16_t x, int16_t y,
                                const char *utf8, uint16_t fg, uint8_t alpha,
                                tft_bg_lookup_t bgsrc, void *ctx)
{
    if (font == NULL || utf8 == NULL || bgsrc == NULL) {
        return;
    }
    const font_glyph_t *glyphs[16];
    uint16_t count = 0;
    const char *p = utf8;
    while (*p != '\0' && count < 16U) {
        const font_glyph_t *glyph = font_glyph(font, utf8_next(&p));
        if (glyph != NULL) {
            glyphs[count++] = glyph;
        }
    }
    if (count == 0U) {
        return;
    }

    uint16_t text_w = (uint16_t)(count * (font->width + font->gap) - font->gap);
    int32_t x0 = x, y0 = y;
    int32_t cx0 = (x0 < 0) ? 0 : x0;
    int32_t cy0 = (y0 < 0) ? 0 : y0;
    int32_t cx1 = (x0 + (int32_t)text_w > (int32_t)ST7735_WIDTH) ? (int32_t)ST7735_WIDTH : (x0 + (int32_t)text_w);
    int32_t cy1 = (y0 + (int32_t)font->height > (int32_t)ST7735_HEIGHT) ? (int32_t)ST7735_HEIGHT : (y0 + (int32_t)font->height);
    if (cx1 <= cx0 || cy1 <= cy0) {
        return;
    }
    if (!set_window((int16_t)cx0, (int16_t)cy0,
                    (int16_t)(cx1 - cx0), (int16_t)(cy1 - cy0))) {
        return;
    }

    TFT_CS_LOW();
    TFT_DC_HIGH();
    for (int32_t gy = cy0; gy < cy1; ++gy) {
        int32_t row = gy - y0;
        uint16_t out = 0;
        for (int32_t gx = cx0; gx < cx1; ++gx, ++out) {
            int32_t cell = (gx - x0) / (font->width + font->gap);
            int32_t col = (gx - x0) - cell * (font->width + font->gap);
            uint16_t bgpix = bgsrc(ctx, (int16_t)gx, (int16_t)gy);
            uint16_t color = bgpix;

            if (cell >= 0 && cell < (int32_t)count &&
                col >= 0 && col < (int32_t)font->width) {
                const font_glyph_t *glyph = glyphs[cell];
                uint32_t bit = (uint32_t)row * font->width + (uint32_t)col;
                if (font->bpp == 4U) {
                    uint8_t packed = glyph->data[bit >> 1];
                    uint8_t glyph_alpha = (bit & 1U) ? (packed & 0x0FU) : (uint8_t)(packed >> 4);
                    if (glyph_alpha != 0U) {
                        uint16_t mixed = (uint16_t)(((uint32_t)glyph_alpha * 17U * alpha) / 255U);
                        if (mixed != 0U) {
                            color = blend565(fg, bgpix, (uint8_t)mixed);
                        }
                    }
                } else {
                    uint8_t packed = glyph->data[bit >> 3];
                    if ((packed & (uint8_t)(0x80U >> (bit & 7U))) != 0U) {
                        color = fg;
                    }
                }
            }
            s_line[out * 2] = (uint8_t)(color >> 8);
            s_line[out * 2 + 1] = (uint8_t)color;
        }
        if (!spi_send(s_line, (uint16_t)((cx1 - cx0) * 2))) {
            break;
        }
    }
    TFT_CS_HIGH();
}


/* ==================================================================== 背光调光
 *
 * 引脚：PB12（新版 PCB 把 TFT 的 BL 接在这里）。
 * 注意：PB12 在 F103 上没有定时器输出（复用功能是 TIM1_BKIN 输入），
 *       所以只能用软件 PWM。200 Hz / 5 档，背光是惯性负载，看不出闪烁。
 *
 * ⚠ PB12 原来被 main.h 定义成 LED_DO（Do 的指示灯）。指示灯已经砍掉，
 *   如果 bsp_notes.c 还在驱动 LED_DO，就会出现"按 Do 键屏幕才亮"的现象。
 */
static volatile uint8_t s_bl_level = 3U;
static volatile bool s_bl_ready;

void tft_backlight_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 先写高再配成输出，避免上电瞬间背光闪一下 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);

    gpio.Pin   = GPIO_PIN_12;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
    s_bl_level = 3U;
    s_bl_ready = true;
}

void tft_set_brightness(uint8_t level)
{
    if (level < 1U) { level = 1U; }
    if (level > 5U) { level = 5U; }
    s_bl_level = level;
}

/* SysTick calls this every 1 ms, including while SPI transfers block main. */
void tft_backlight_tick(void)
{
    if (!s_bl_ready) { return; }
    if (s_bl_level >= 5U) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
        return;
    }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12,
                      ((HAL_GetTick() % 5U) < s_bl_level) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
