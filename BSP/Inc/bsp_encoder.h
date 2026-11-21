#ifndef BSP_ENCODER_H
#define BSP_ENCODER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_ENCODER_EVENT_NONE = 0,
    BSP_ENCODER_EVENT_CW,     /* 顺时针旋转一格 */
    BSP_ENCODER_EVENT_CCW,    /* 逆时针旋转一格 */
    BSP_ENCODER_EVENT_SHORT,  /* 短按 */
    BSP_ENCODER_EVENT_LONG    /* 长按超过 1 秒 */
} bsp_encoder_event_t;

void bsp_encoder_init(void);
void bsp_encoder_poll(uint32_t now_ms);
bool bsp_encoder_take_event(bsp_encoder_event_t *event);

#endif /* BSP_ENCODER_H */
