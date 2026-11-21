#ifndef BSP_FUNC_H
#define BSP_FUNC_H

#include <stdbool.h>
#include <stdint.h>

void bsp_func_init(void);
void bsp_func_poll(uint32_t now_ms);

/* 功能键事件。短按在"松开"时产生；长按（≥600 ms）在按住期间就产生，
 * 并且产生长按之后不会再补发短按。 */
typedef enum {
    BSP_FUNC_EVENT_NONE = 0,
    BSP_FUNC_EVENT_SHORT,
    BSP_FUNC_EVENT_LONG
} bsp_func_event_t;

bsp_func_event_t bsp_func_take_event(void);

#endif
