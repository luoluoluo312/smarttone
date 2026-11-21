#include "bsp_encoder.h"

#include "main.h"

#define ENC_POLL_MS          1U
#define ENC_STABLE_SAMPLES   2U
#define ENC_BUTTON_SAMPLES   5U
#define ENC_LONG_PRESS_MS    1000U
#define ENC_EVENT_QUEUE_LEN  8U

/*
 * 正交解码表。索引为 (上一状态 << 2) | 当前状态，A 相为 bit1，B 相为 bit0。
 * 每变化一次累计 ±1，累计到 ±4 视为转过一个 EC11 机械定位点。
 */
static const int8_t s_quad_table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

static bsp_encoder_event_t s_queue[ENC_EVENT_QUEUE_LEN];
static uint8_t s_queue_head;
static uint8_t s_queue_tail;
static uint8_t s_queue_count;

static uint32_t s_last_poll_ms;
static uint8_t s_ab_raw;
static uint8_t s_ab_stable;
static uint8_t s_ab_same_count;
static int8_t s_quad_accum;

static uint8_t s_button_raw;
static uint8_t s_button_stable;
static uint8_t s_button_same_count;
static bool s_button_pressed;
static bool s_button_long_fired;
static uint32_t s_button_press_ms;

static uint8_t read_ab(void)
{
    uint8_t a = (HAL_GPIO_ReadPin(ENC_A_GPIO_Port, ENC_A_Pin) == GPIO_PIN_SET) ? 1U : 0U;
    uint8_t b = (HAL_GPIO_ReadPin(ENC_B_GPIO_Port, ENC_B_Pin) == GPIO_PIN_SET) ? 1U : 0U;
    return (uint8_t)((a << 1) | b);
}

static uint8_t read_button_raw(void)
{
    /* EC11 公共端接地，按下为低电平。 */
    return (HAL_GPIO_ReadPin(ENC_SW_GPIO_Port, ENC_SW_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

static void push_event(bsp_encoder_event_t event)
{
    if (s_queue_count >= ENC_EVENT_QUEUE_LEN) {
        return;
    }
    s_queue[s_queue_tail] = event;
    s_queue_tail = (uint8_t)((s_queue_tail + 1U) % ENC_EVENT_QUEUE_LEN);
    ++s_queue_count;
}

void bsp_encoder_init(void)
{
    s_queue_head = 0U;
    s_queue_tail = 0U;
    s_queue_count = 0U;
    s_quad_accum = 0;

    s_ab_raw = read_ab();
    s_ab_stable = s_ab_raw;
    s_ab_same_count = ENC_STABLE_SAMPLES;
    s_last_poll_ms = HAL_GetTick();

    s_button_raw = read_button_raw();
    s_button_stable = s_button_raw;
    s_button_same_count = ENC_BUTTON_SAMPLES;
    s_button_pressed = (s_button_stable != 0U);
    s_button_long_fired = false;
    s_button_press_ms = s_last_poll_ms;
}

void bsp_encoder_poll(uint32_t now_ms)
{
    if ((uint32_t)(now_ms - s_last_poll_ms) < ENC_POLL_MS) {
        return;
    }
    s_last_poll_ms = now_ms;

    /* ---- 旋转编码器：先做 2 次采样消抖，再做正交状态累计 ---- */
    uint8_t ab = read_ab();
    if (ab == s_ab_raw) {
        if (s_ab_same_count < ENC_STABLE_SAMPLES) {
            ++s_ab_same_count;
        }
    } else {
        s_ab_raw = ab;
        s_ab_same_count = 0U;
    }

    if (s_ab_same_count >= ENC_STABLE_SAMPLES && ab != s_ab_stable) {
        uint8_t prev = s_ab_stable;
        s_ab_stable = ab;
        int8_t delta = s_quad_table[(uint8_t)((prev << 2) | ab)];
        s_quad_accum = (int8_t)(s_quad_accum + delta);

        if (s_quad_accum >= 4) {
            push_event(BSP_ENCODER_EVENT_CW);
            s_quad_accum = 0;
        } else if (s_quad_accum <= -4) {
            push_event(BSP_ENCODER_EVENT_CCW);
            s_quad_accum = 0;
        }
    }

    /* ---- 按键：5 ms 消抖，按下 1 s 后产生一次长按事件 ---- */
    uint8_t button = read_button_raw();
    if (button == s_button_raw) {
        if (s_button_same_count < ENC_BUTTON_SAMPLES) {
            ++s_button_same_count;
        }
    } else {
        s_button_raw = button;
        s_button_same_count = 0U;
    }

    if (s_button_same_count >= ENC_BUTTON_SAMPLES && button != s_button_stable) {
        s_button_stable = button;
        if (button != 0U) {
            s_button_pressed = true;
            s_button_long_fired = false;
            s_button_press_ms = now_ms;
        } else {
            s_button_pressed = false;
            if (!s_button_long_fired) {
                push_event(BSP_ENCODER_EVENT_SHORT);
            }
            s_button_long_fired = false;
        }
    }

    if (s_button_pressed && !s_button_long_fired &&
        (uint32_t)(now_ms - s_button_press_ms) >= ENC_LONG_PRESS_MS) {
        s_button_long_fired = true;
        push_event(BSP_ENCODER_EVENT_LONG);
    }
}

bool bsp_encoder_take_event(bsp_encoder_event_t *event)
{
    if (event == NULL || s_queue_count == 0U) {
        return false;
    }
    *event = s_queue[s_queue_head];
    s_queue_head = (uint8_t)((s_queue_head + 1U) % ENC_EVENT_QUEUE_LEN);
    --s_queue_count;
    return true;
}
