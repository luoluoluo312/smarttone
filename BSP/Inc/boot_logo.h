#ifndef BOOT_LOGO_H
#define BOOT_LOGO_H

#include <stdbool.h>
#include <stdint.h>

/* 128x160 竖屏，RGB565 调色板索引图，显示时长 5 s。 */
#define BOOT_LOGO_WIDTH   128U
#define BOOT_LOGO_HEIGHT  160U
#define BOOT_LOGO_MS      5000U

void boot_logo_start(void);
bool boot_logo_active(void);
/* 返回 true 表示 5 s 计时结束，主循环应初始化并进入主菜单。 */
bool boot_logo_tick(uint32_t now_ms);

/* 只把整屏图片重画一遍，不启动也不影响 5 s 计时。
 * 欢迎页（UI_PAGE_HOME）每次回到该页时用它重建背景。 */
void boot_logo_draw(void);

/* 供透明文字使用：查询图片在 (x,y) 处的颜色。
 * 签名与 tft_bg_lookup_t 一致，直接传给 tft_draw_text_*_overlay。 */
uint16_t boot_logo_pixel_at(void *ctx, int16_t x, int16_t y);

#endif
