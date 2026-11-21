#include "bsp_func.h"

#include "main.h"

#define FUNC_STABLE_SAMPLES 5U
#define FUNC_LONG_MS        600U

static uint32_t s_last_ms;
static uint8_t  s_candidate;
static uint8_t  s_stable;
static uint8_t  s_same_count;

static uint32_t s_press_ms;     /* 本次按下稳定的时刻 */
static bool     s_holding;      /* 当前处于"按住且尚未产生长按"状态 */
static uint8_t  s_pending;      /* 待取事件：1=短按 2=长按 */

void bsp_func_init(void)
{
    s_candidate = (HAL_GPIO_ReadPin(FUNC_GPIO_Port, FUNC_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
    s_stable = s_candidate;
    s_same_count = FUNC_STABLE_SAMPLES;
    s_last_ms = HAL_GetTick();
    s_press_ms = 0UL;
    s_holding = false;
    s_pending = 0U;
}

void bsp_func_poll(uint32_t now_ms)
{
    if ((uint32_t)(now_ms - s_last_ms) < 1U) {
        return;
    }
    s_last_ms = now_ms;

    uint8_t raw = (HAL_GPIO_ReadPin(FUNC_GPIO_Port, FUNC_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
    if (raw != s_candidate) {
        s_candidate = raw;
        s_same_count = 0U;
        return;
    }
    if (s_same_count < FUNC_STABLE_SAMPLES) {
        ++s_same_count;
    }

    if (s_same_count >= FUNC_STABLE_SAMPLES && s_stable != s_candidate) {
        s_stable = s_candidate;
        if (s_stable != 0U) {
            /* 按下：先不产生事件，等松开或超时才决定是短按还是长按 */
            s_press_ms = now_ms;
            s_holding  = true;
        } else {
            /* 松开：没触发过长按才算短按 */
            if (s_holding) {
                s_pending = 1U;
            }
            s_holding = false;
        }
    }

    /* 按住超过阈值立刻产生长按，并抑制随后的短按 */
    if (s_holding && ((uint32_t)(now_ms - s_press_ms) >= FUNC_LONG_MS)) {
        s_holding = false;
        s_pending = 2U;
    }
}

bsp_func_event_t bsp_func_take_event(void)
{
    uint8_t pending = s_pending;

    s_pending = 0U;
    if (pending == 1U) {
        return BSP_FUNC_EVENT_SHORT;
    }
    if (pending == 2U) {
        return BSP_FUNC_EVENT_LONG;
    }
    return BSP_FUNC_EVENT_NONE;
}
