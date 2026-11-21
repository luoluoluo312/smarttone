#include "bsp_notes.h"

#include "audio.h"
#include "main.h"
#include "record.h"

#define NOTE_STABLE_SAMPLES 5U

/* ------------------------------------------------------------ 颤音参数
 *
 * 按住琴键不放时，让频率在一个小范围内快速上下波动，听感上"有情绪"。
 *   VIB_HZ       波动频率（每秒多少次）
 *   VIB_DEPTH_PM 波动深度，千分比；管风琴 25 = ±2.5%，吉他 7 = ±0.7%
 *   把 VIB_DEPTH_PM 改成 0 即可关闭颤音。
 *
 * 实现要点：基准频率在按下那一刻记下来，之后每毫秒按三角波算一个新的
 * 瞬时频率去 Retune。因为基准是固定的，反复改频不会产生漂移。
 * 录音写进去的是**基准频率**，所以回放不会带颤音。
 */
#define VIB_HZ              6U
#define VIB_DEPTH_ORGAN_PM  25U
#define VIB_DEPTH_GUITAR_PM 7U
#define VIB_DIV             3U      /* 每 3 ms 更新一次，听感无差别，CPU 降到 1/3 */
#define VIB_STEP            ((uint16_t)(65536UL * VIB_HZ / (1000UL / VIB_DIV)))

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_TypeDef *led_port;
    uint16_t led_pin;
    uint8_t note;
    uint8_t candidate;
    uint8_t stable;
    uint8_t same_count;
    int8_t voice;
    uint16_t base_freq;    /* 按下那一刻的频率，颤音以它为基准 */
    uint16_t vib_phase;    /* 颤音相位累加器 */
} note_key_t;

static note_key_t s_keys[NOTE_COUNT] = {
    {NOTE_DO_GPIO_Port,  NOTE_DO_Pin,  LED_DO_GPIO_Port,  LED_DO_Pin,  0U, 0U, 0U, 0U, -1, 0U, 0U},
    {NOTE_RE_GPIO_Port,  NOTE_RE_Pin,  LED_RE_GPIO_Port,  LED_RE_Pin,  1U, 0U, 0U, 0U, -1, 0U, 0U},
    {NOTE_MI_GPIO_Port,  NOTE_MI_Pin,  LED_MI_GPIO_Port,  LED_MI_Pin,  2U, 0U, 0U, 0U, -1, 0U, 0U},
    {NOTE_FA_GPIO_Port,  NOTE_FA_Pin,  LED_FA_GPIO_Port,  LED_FA_Pin,  3U, 0U, 0U, 0U, -1, 0U, 0U},
    {NOTE_SOL_GPIO_Port, NOTE_SOL_Pin, LED_SOL_GPIO_Port, LED_SOL_Pin, 4U, 0U, 0U, 0U, -1, 0U, 0U},
    {NOTE_LA_GPIO_Port,  NOTE_LA_Pin,  LED_LA_GPIO_Port,  LED_LA_Pin,  5U, 0U, 0U, 0U, -1, 0U, 0U},
    {NOTE_SI_GPIO_Port,  NOTE_SI_Pin,  LED_SI_GPIO_Port,  LED_SI_Pin,  6U, 0U, 0U, 0U, -1, 0U, 0U}
};

static uint32_t s_last_ms;
static uint8_t s_active_mask;
static uint8_t s_vib_div;
static bsp_notes_event_cb_t s_event_cb;

static uint8_t read_key(const note_key_t *key)
{
    return (HAL_GPIO_ReadPin(key->port, key->pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

/* 三角波颤音：每毫秒调用一次，算完直接 Retune。
 * 用三角波而不是正弦表，省 Flash 也省算力，听感差别很小。 */
static void key_vibrato(note_key_t *key)
{
    int32_t tri;
    int32_t span;
    int32_t off;
    int32_t f;
    uint16_t depth;
    TimbreId timbre;

    if ((key->voice < 0) || (key->base_freq == 0U)) {
        return;
    }
    /* 管风琴用明显颤音；失真吉他加一点很浅的揉弦。钢琴不加。 */
    timbre = Audio_GetTimbre();
    if (timbre == TIMBRE_ORGAN) {
        depth = VIB_DEPTH_ORGAN_PM;
    } else if (timbre == TIMBRE_GUITAR) {
        depth = VIB_DEPTH_GUITAR_PM;
    } else {
        return;
    }

    key->vib_phase = (uint16_t)(key->vib_phase + VIB_STEP);

    /* 相位 0..65535 映射成三角形 0..32767..0 */
    tri = (key->vib_phase < 32768U) ? (int32_t)key->vib_phase
                                    : (int32_t)(65536UL - key->vib_phase);

    span = ((int32_t)key->base_freq * (int32_t)depth) / 1000;
    off  = ((tri - 16384) * span) / 16384;
    f    = (int32_t)key->base_freq + off;

    if (f < 100) {
        f = 100;
    }
    if (f > 4000) {
        f = 4000;
    }
    (void)Audio_Retune(key->voice, (uint16_t)f);
}

void bsp_notes_init(void)
{
    for (uint8_t i = 0; i < NOTE_COUNT; ++i) {
        s_keys[i].candidate = read_key(&s_keys[i]);
        s_keys[i].stable = s_keys[i].candidate;
        s_keys[i].same_count = NOTE_STABLE_SAMPLES;
        s_keys[i].voice = -1;
        s_keys[i].base_freq = 0U;
        s_keys[i].vib_phase = 0U;
    }
    s_last_ms = HAL_GetTick();
    s_active_mask = 0U;
}

uint8_t bsp_notes_active_mask(void)
{
    return s_active_mask;
}

uint8_t bsp_notes_pressed_mask(void)
{
    uint8_t mask = 0U;
    for (uint8_t i = 0U; i < NOTE_COUNT; ++i) {
        if (s_keys[i].stable != 0U) { mask |= (uint8_t)(1U << s_keys[i].note); }
    }
    return mask;
}

void bsp_notes_set_event_cb(bsp_notes_event_cb_t cb)
{
    s_event_cb = cb;
}

void bsp_notes_poll(uint32_t now_ms)
{
    bool do_vib;

    if ((uint32_t)(now_ms - s_last_ms) < 1U) {
        return;
    }
    s_last_ms = now_ms;

    /* 颤音不必每毫秒都算，分频降低主循环负担 */
    s_vib_div = (uint8_t)(s_vib_div + 1U);
    do_vib = (s_vib_div >= VIB_DIV);
    if (do_vib) { s_vib_div = 0U; }

    for (uint8_t i = 0; i < NOTE_COUNT; ++i) {
        note_key_t *key = &s_keys[i];
        uint8_t raw;

        /* 颤音在按住期间持续更新，所以放在状态机之前，不能被 continue 跳过。
         * 更新频率由 VIB_DIV 分频，减轻主循环负担。 */
        if (do_vib) {
            key_vibrato(key);
        }

        raw = read_key(key);

        if (raw != key->candidate) {
            key->candidate = raw;
            key->same_count = 0U;
            continue;
        }
        if (key->same_count < NOTE_STABLE_SAMPLES) {
            ++key->same_count;
        }
        if (key->same_count < NOTE_STABLE_SAMPLES || key->stable == key->candidate) {
            continue;
        }
        key->stable = key->candidate;

        if (key->stable != 0U) {
            key->voice = Audio_NoteOn(key->note);
            if (key->voice >= 0) {
                s_active_mask |= (uint8_t)(1U << key->note);
                /* 颤音的基准频率就是按下那一刻的频率 */
                key->base_freq = Audio_GetVoiceFreq(key->voice);
                key->vib_phase = 0U;

                /* 只有声部分配成功才生成事件；频率/音区全部取音频模块。 */
                record_note_press(key->note, key->voice, Audio_GetOctave(),
                                  Audio_GetVoiceFreq(key->voice), now_ms);
            } else {
                s_active_mask &= (uint8_t)~(1U << key->note);
            }
            /* 发声是否成功都要通知游戏判定：错键/声部满也要记录按键 */
            if (s_event_cb != NULL) {
                s_event_cb(key->note, true, now_ms);
            }
        } else {
            if (key->voice >= 0) {
                record_note_release(key->note, now_ms);
                Audio_NoteOff(key->voice);
                s_active_mask &= (uint8_t)~(1U << key->note);
                key->voice = -1;
                key->base_freq = 0U;
            }
            if (s_event_cb != NULL) {
                s_event_cb(key->note, false, now_ms);
            }
        }
    }
}
