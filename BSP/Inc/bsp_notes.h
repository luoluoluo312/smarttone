#ifndef BSP_NOTES_H
#define BSP_NOTES_H

#include <stdbool.h>
#include <stdint.h>

/* 按键事件回调：在按键扫描确认按下/松开时调用，时间戳就是扫描时刻。
 * 游戏用它做判定；普通演奏不注册时没有任何额外开销。 */
typedef void (*bsp_notes_event_cb_t)(uint8_t note, bool pressed, uint32_t now_ms);

void bsp_notes_init(void);
void bsp_notes_poll(uint32_t now_ms);
/* bit0~bit6 对应 DO~SI，仅声部分配成功的按键置位。 */
uint8_t bsp_notes_active_mask(void);
/* 消抖后的物理按键位图，包含未获得声部的按键。 */
uint8_t bsp_notes_pressed_mask(void);

/* 注册/清除按键事件回调（传 NULL 清除）。 */
void bsp_notes_set_event_cb(bsp_notes_event_cb_t cb);

#endif
