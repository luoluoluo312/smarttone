/*
 * Host-side test for the SmartTone UI and EC11 decoder.
 *
 * A tiny ST7735/SPI model reconstructs the 128x160 framebuffer.  The test
 * exercises the encoder state machine, page transitions, bounds and partial
 * refreshes without hardware.
 */
#include "main.h"
#include "bsp_encoder.h"
#include "bsp_tft.h"
#include "ui_page.h"
#include "record.h"
#include "audio.h"
#include "bsp_notes.h"
#include "ds_room.h"
#include "boot_logo.h"
#include "bsp_audio_tim.h"
#include "audio_pwm.h"
#include "audio_config.h"
#include "rhythm_game.h"
#include "melody_maker.h"
#include "rhythm_chart.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void bsp_notes_init(void) {}

GPIO_TypeDef mock_gpio_b = {0};
GPIO_TypeDef mock_gpio_a = {0};
TIM_TypeDef mock_tim2, mock_tim3, mock_tim4;
void Error_Handler(void) { assert(!"BSP initialization failed"); }


static uint16_t framebuffer[ST7735_HEIGHT][ST7735_WIDTH];

static int cs_level = 1;
static int dc_level = 0;
static uint8_t current_command;
static uint8_t panel_madctl;
static uint8_t params[8];
static int param_count;
static int xs, xe, ys, ye, cx, cy;
static int pixel_hi = -1;
static uint32_t spi_bytes, spi_calls, pixel_writes;
static uint32_t row_writes[ST7735_HEIGHT];
static uint8_t pixel_hits[ST7735_HEIGHT][ST7735_WIDTH];

static int mock_a = 1;
static int mock_b = 1;
static int mock_sw = 1;
static uint32_t mock_tick = 0;
static uint32_t sample_spi_hz; /* Nonzero: consume samples during modeled SPI writes. */
static uint8_t mock_note_mask;

/* 主机测试不编译实物按键扫描，只注入有效声部位图。 */
uint8_t bsp_notes_active_mask(void)
{
    return mock_note_mask;
}

/* bsp_notes.c 不在主机测试的编译列表里，这里提供同签名的空实现。 */
uint8_t bsp_notes_pressed_mask(void)
{
    return mock_note_mask;
}

void bsp_notes_set_event_cb(bsp_notes_event_cb_t cb)
{
    (void)cb;
}

static void process_command(uint8_t command)
{
    current_command = command;
    param_count = 0;
    pixel_hi = -1;
    if (command == 0x2C) {
        cx = xs;
        cy = ys;
    }
}

static void process_data(uint8_t data)
{
    if (current_command == 0x36U) {
        panel_madctl = data;
        return;
    }
    if (current_command == 0x2A || current_command == 0x2B) {
        if (param_count < 4) {
            params[param_count++] = data;
        }
        if (param_count == 4) {
            int start = (params[0] << 8) | params[1];
            int end = (params[2] << 8) | params[3];
            if (current_command == 0x2A) {
                xs = start;
                xe = end;
            } else {
                ys = start;
                ye = end;
            }
        }
        return;
    }

    if (current_command == 0x2C) {
        if (pixel_hi < 0) {
            pixel_hi = data;
            return;
        }
        uint16_t color = (uint16_t)((pixel_hi << 8) | data);
        pixel_hi = -1;
        if (cx >= 0 && cx < (int)ST7735_WIDTH && cy >= 0 && cy < (int)ST7735_HEIGHT) {
            framebuffer[cy][cx] = color;
            ++pixel_writes;
            ++row_writes[cy];
            ++pixel_hits[cy][cx];
        }
        ++cx;
        if (cx > xe) {
            cx = xs;
            ++cy;
            if (cy > ye) {
                cy = ys;
            }
        }
    }
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    (void)port;
    if (pin == TFT_CS_Pin) {
        cs_level = (state == GPIO_PIN_SET);
    } else if (pin == TFT_DC_Pin) {
        dc_level = (state == GPIO_PIN_SET);
    }
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    (void)port;
    if (pin == ENC_A_Pin) {
        return mock_a ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    if (pin == ENC_B_Pin) {
        return mock_b ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    if (pin == ENC_SW_Pin) {
        return mock_sw ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    return GPIO_PIN_SET;
}

uint32_t HAL_GetTick(void)
{
    return mock_tick;
}

void HAL_Delay(uint32_t ms)
{
    mock_tick += ms;
}

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *data,
                                   uint16_t size, uint32_t timeout)
{
    (void)hspi;
    (void)timeout;
    if (cs_level != 0) {
        return HAL_ERROR;
    }
    spi_bytes += size;
    ++spi_calls;
    if (sample_spi_hz != 0U) {
        uint32_t interrupts = ((uint32_t)size * 8U * AUDIO_SAMPLE_HZ + sample_spi_hz - 1U) / sample_spi_hz;
        for (uint32_t i = 0U; i < interrupts; ++i) { AudioPwm_SampleIRQ(); }
    }
    for (uint16_t i = 0; i < size; ++i) {
        if (dc_level) {
            process_data(data[i]);
        } else {
            process_command(data[i]);
        }
    }
    return HAL_OK;
}

static void capture_framebuffer(const char *path)
{
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    fprintf(f, "P6\n%u %u\n255\n", ST7735_WIDTH, ST7735_HEIGHT);
    for (uint16_t y = 0; y < ST7735_HEIGHT; ++y) {
        for (uint16_t x = 0; x < ST7735_WIDTH; ++x) {
            uint16_t c = framebuffer[y][x];
            uint8_t rgb[3] = {
                (uint8_t)((((c >> 11) & 0x1F) * 255 + 15) / 31),
                (uint8_t)((((c >> 5) & 0x3F) * 255 + 31) / 63),
                (uint8_t)(((c & 0x1F) * 255 + 15) / 31)
            };
            fwrite(rgb, 1, sizeof(rgb), f);
        }
    }
    fclose(f);
}

static void capture_ppm(const char *path)
{
    /* 先让页面滑入/数值过渡收敛，预览才是最终静止画面。 */
    mock_tick += 200U;
    ui_page_tick(mock_tick);
    capture_framebuffer(path);
}

/* 采样 128 次，返回单声部偏离中点的最大幅度（PWM 计数）。 */
static int measure_peak_deviation(void)
{
    int peak = 0;
    /* 先让输出门控完成淡入/淡出，再测稳态振幅。 */
    for (unsigned i = 0U; i < 128U; ++i) {
        AudioPwm_SampleIRQ();
    }
    for (unsigned i = 0U; i < 128U; ++i) {
        AudioPwm_SampleIRQ();
        int dev = (TIM2->CCR1 == 0U)
                      ? 0
                      : ((int)TIM2->CCR1 - (int)AUDIO_PWM_MIDPOINT);
        if (dev < 0) {
            dev = -dev;
        }
        if (dev > peak) {
            peak = dev;
        }
    }
    return peak;
}

static void settle_pwm_idle(void)
{
    for (unsigned i = 0U; i < 256U; ++i) {
        AudioPwm_SampleIRQ();
    }
}

static void drain_encoder_events(void)
{
    bsp_encoder_event_t event;
    while (bsp_encoder_take_event(&event)) {
    }
}

static void encoder_poll_ms(uint32_t count)
{
    while (count-- != 0U) {
        ++mock_tick;
        bsp_encoder_poll(mock_tick);
    }
}

static void encoder_set_ab(uint8_t ab)
{
    mock_a = (ab >> 1) & 1U;
    mock_b = ab & 1U;
    encoder_poll_ms(3U);
}

static void encoder_send_cw(void)
{
    static const uint8_t seq[] = {3, 1, 0, 2, 3};
    for (uint8_t i = 0; i < sizeof(seq); ++i) {
        encoder_set_ab(seq[i]);
    }
}

static void encoder_send_ccw(void)
{
    static const uint8_t seq[] = {3, 2, 0, 1, 3};
    for (uint8_t i = 0; i < sizeof(seq); ++i) {
        encoder_set_ab(seq[i]);
    }
}

static void test_audio_contract(void)
{
    assert(sizeof(NoteEvent) == 12U);
    Audio_Init();
    VoiceId a = Audio_NoteOn(NOTE_DO);
    VoiceId b = Audio_NoteOn(NOTE_RE);
    VoiceId c = Audio_NoteOn(NOTE_MI);
    assert(a >= 0);
    assert(b >= 0);
    assert(c < 0);                 /* V2：声部上限 2 */
    assert(Audio_GetVoiceFreq(a) > 0U);
    Audio_NoteOff(a);
    Audio_NoteOff(b);
    Audio_SetOctave(OCT_HIGH);
    VoiceId la = Audio_NoteOn(NOTE_LA);
    assert(la >= 0);
    assert(Audio_GetVoiceFreq(la) == 880U);
    Audio_AllOff();
}
static void test_boot_logo(void)
{
    mock_tick = 0U;
    boot_logo_start();
    assert(boot_logo_active());
    capture_ppm("build/boot.ppm");
    assert(!boot_logo_tick(4999U));
    assert(boot_logo_tick(5000U));
    assert(!boot_logo_active());
}

static void test_encoder(void)
{
    bsp_encoder_event_t event;

    mock_a = 1;
    mock_b = 1;
    mock_sw = 1;
    bsp_encoder_init();
    drain_encoder_events();

    encoder_send_cw();
    assert(bsp_encoder_take_event(&event));
    assert(event == BSP_ENCODER_EVENT_CW);

    encoder_send_ccw();
    assert(bsp_encoder_take_event(&event));
    assert(event == BSP_ENCODER_EVENT_CCW);

    mock_sw = 0;
    encoder_poll_ms(6U);
    mock_sw = 1;
    encoder_poll_ms(6U);
    assert(bsp_encoder_take_event(&event));
    assert(event == BSP_ENCODER_EVENT_SHORT);

    mock_sw = 0;
    encoder_poll_ms(1010U);
    assert(bsp_encoder_take_event(&event));
    assert(event == BSP_ENCODER_EVENT_LONG);
    mock_sw = 1;
    encoder_poll_ms(6U);
    assert(!bsp_encoder_take_event(&event));

    printf("encoder: CW/CCW/SHORT/LONG ok\n");
}

static void nav_main_to(uint8_t target)
{
    while (ui_page_get_state()->main_sel != target) {
        ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    }
}

/* Both recording paths must keep running while FUNC/EC11 adjust live parameters. */
static void check_record_live_params(void)
{
    uint8_t mode = ui_page_get_state()->record_mode;
    uint8_t source = ui_page_get_state()->record_sel;
    uint8_t focus = ui_page_get_state()->param_sel;
    uint16_t count = record_event_count();
    uint32_t elapsed = record_elapsed_ms(mock_tick);
    bool overdub = record_overdub_active();
    for (unsigned i = 0U; i < 3U; ++i) {
        ui_page_handle_func();
        uint8_t selected = (uint8_t)((focus + i + 1U) % 3U);
        assert(ui_page_get_state()->param_sel == selected);
        assert(framebuffer[119][5U + selected * 41U] == 0xFFE0U);
        int before = selected == 0U ? (int)Audio_GetOctave() :
                     selected == 1U ? (int)Audio_GetVolume() : Audio_GetTranspose();
        int expected = selected == 0U ? (before + 1) % 3 :
                       selected == 1U ? (before + 1) % 5 : (before == 2 ? -2 : before + 1);
        ui_page_handle_event(BSP_ENCODER_EVENT_CW);
        int after = selected == 0U ? (int)Audio_GetOctave() :
                    selected == 1U ? (int)Audio_GetVolume() : Audio_GetTranspose();
        assert(after == expected);
        ui_page_handle_event(BSP_ENCODER_EVENT_CCW);
        after = selected == 0U ? (int)Audio_GetOctave() :
                selected == 1U ? (int)Audio_GetVolume() : Audio_GetTranspose();
        assert(after == before);
        assert(record_is_recording());
        assert(record_overdub_active() == overdub);
        assert(ui_page_get_state()->record_mode == mode);
        assert(ui_page_get_state()->record_sel == source);
        assert(record_event_count() == count);
        assert(record_elapsed_ms(mock_tick) == elapsed);
    }
}

static void test_ui_pages(void)
{
    const ui_page_state_t *s;

    mock_a = 1;
    mock_b = 1;
    mock_sw = 1;
    Audio_Init();
    record_init(NULL);
    ui_page_init();
    s = ui_page_get_state();
    assert(s->page == UI_PAGE_HOME);
    capture_ppm("build/ui_home.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    Song_Stop();
    Audio_AllOff();
    assert(s->page == UI_PAGE_MAIN);
    assert(s->main_sel == 0U);
    capture_ppm("build/ui_main.ppm");

    /* 主菜单选中项切换：整行重画，不能出现经过其他行的黑线 */
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    assert(ui_page_get_state()->main_sel == 1U);
    capture_ppm("build/ui_main_sel.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_CCW);
    assert(ui_page_get_state()->main_sel == 0U);

    /* 演奏 */
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_PERFORM);
    capture_ppm("build/ui_perform.ppm");

    /* 演奏页高亮：注入 DO+SO 有效声部，只应点亮这两个音符格。 */
    mock_tick += 100U;
    ui_page_tick(mock_tick);
    mock_tick += 100U;
    ui_page_tick(mock_tick);
    mock_note_mask = (uint8_t)((1U << 0) | (1U << 4));
    mock_tick += 1U;
    ui_page_tick(mock_tick);
    assert(framebuffer[81][3] == 0xFFE0U);   /* DO 格子黄色 */
    assert(framebuffer[81][75] == 0xFFE0U);  /* SO 格子黄色 */
    capture_ppm("build/ui_perform_highlight.ppm");

    mock_note_mask = 0U;
    mock_tick += 1U;
    ui_page_tick(mock_tick);
    assert(framebuffer[81][3] != 0xFFE0U);
    assert(framebuffer[81][75] != 0xFFE0U);

    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    assert(ui_page_current() == UI_PAGE_MAIN);

    /* 音区设置：默认 2，旋转到 3，越界保护 */
    nav_main_to(1U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_OCTAVE);
    assert(Audio_GetOctave() == OCT_MID);
    capture_ppm("build/ui_octave.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    assert(Audio_GetOctave() == OCT_HIGH);
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    assert(Audio_GetOctave() == OCT_HIGH);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);

    /* 移调设置：-2..+2 边界（V2） */
    nav_main_to(2U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_TRANSPOSE);
    assert(Audio_GetTranspose() == 0);
    capture_ppm("build/ui_transpose.ppm");
    for (int i = 0; i < 20; ++i) {
        ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    }
    assert(Audio_GetTranspose() == 2);
    for (int i = 0; i < 30; ++i) {
        ui_page_handle_event(BSP_ENCODER_EVENT_CCW);
    }
    assert(Audio_GetTranspose() == -2);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);

    /* 节拍器：0..240 边界 */
    nav_main_to(3U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_METRONOME);
    assert(ui_page_get_state()->bpm == 120U);
    capture_ppm("build/ui_metronome.ppm");
    for (int i = 0; i < 300; ++i) {
        ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    }
    assert(ui_page_get_state()->bpm == 240U);
    for (int i = 0; i < 300; ++i) {
        ui_page_handle_event(BSP_ENCODER_EVENT_CCW);
    }
    assert(ui_page_get_state()->bpm == 40U);

    /* FUNC 只切 BPM / 拍号；音量模块已从节拍器页移除。 */
    ui_page_handle_func();
    assert(ui_page_get_state()->metro_focus == 1U);
    assert(ui_page_get_state()->bpm == 40U);
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    assert(Metronome_GetBeatsPerBar() == 6U);     /* 4/4 → 6/8 */
    ui_page_handle_event(BSP_ENCODER_EVENT_CCW);
    assert(Metronome_GetBeatsPerBar() == 4U);     /* 6/8 → 4/4 */
    capture_ppm("build/ui_metronome_sig.ppm");

    ui_page_handle_func();
    assert(ui_page_get_state()->metro_focus == 0U);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);

    /* 曲库与回放 */
    nav_main_to(5U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_LIBRARY);
    assert(ui_page_get_state()->library_sel == 0U);
    capture_ppm("build/ui_library.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    assert(ui_page_get_state()->library_sel == 1U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_PLAYBACK);
    assert(ui_page_get_state()->playing);
    assert(ui_page_get_state()->playback_track == 1U);
    capture_ppm("build/ui_playback.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(!ui_page_get_state()->playing);
    capture_ppm("build/ui_paused.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    assert(ui_page_current() == UI_PAGE_LIBRARY);
    /* The finite C4 song must show completion and restart on a short press. */
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_get_state()->playback_track == 2U);
    for (unsigned i = 0; i < 6000U && Song_GetState() == SONG_PLAYING; ++i) {
        mock_tick += 10U;
        Song_Tick();
        ui_page_tick(mock_tick);
    }
    assert(Song_GetState() == SONG_IDLE);
    assert(!ui_page_get_state()->playing);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(Song_GetState() == SONG_PLAYING);
    assert(ui_page_get_state()->playing);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);

    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    assert(ui_page_current() == UI_PAGE_MAIN);

    /* 录音：启动、采集两个音符事件、停止保存并自动进入曲库 */
    nav_main_to(4U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_RECORD);
    capture_ppm("build/ui_record.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(record_is_recording());
    check_record_live_params();
    int8_t v0 = Audio_NoteOn(0U);
    assert(v0 >= 0);
    record_note_press(0U, v0, Audio_GetOctave(), Audio_GetVoiceFreq(v0), mock_tick);
    mock_tick += 10U;
    record_note_release(0U, mock_tick);
    Audio_NoteOff(v0);
    int8_t v1 = Audio_NoteOn(2U);
    assert(v1 >= 0);
    record_note_press(2U, v1, Audio_GetOctave(), Audio_GetVoiceFreq(v1), mock_tick);
    mock_tick += 10U;
    record_note_release(2U, mock_tick);
    Audio_NoteOff(v1);
    ui_page_tick(mock_tick);
    assert(record_event_count() == 2U);
    mock_tick += 1200U;
    ui_page_tick(mock_tick);
    assert(record_elapsed_ms(mock_tick) >= 1200U);
    mock_note_mask = (uint8_t)((1U << 0) | (1U << 4));
    mock_tick += 1U;
    ui_page_tick(mock_tick);
    assert(framebuffer[97][3] == 0xFFE0U);   /* 录音页 DO 高亮 */
    assert(framebuffer[97][75] == 0xFFE0U);  /* 录音页 SO 高亮 */
    capture_ppm("build/ui_recording.ppm");
    mock_note_mask = 0U;
    mock_tick += 1U;
    ui_page_tick(mock_tick);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(!record_is_recording());
    assert(ui_page_current() == UI_PAGE_LIBRARY);
    assert(record_track_count() == 1U);
    assert(ui_page_get_state()->library_sel == Song_GetBuiltinCount());
    capture_ppm("build/ui_library_rec.ppm");
    assert(record_init(NULL));
    assert(record_track_count() == 1U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_PLAYBACK);
    assert(!ui_page_get_state()->playing); /* Recording waits for explicit play. */
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_get_state()->playing);
    mock_tick += 200U;
    ui_page_tick(mock_tick);
    mock_tick += 1U;
    ui_page_tick(mock_tick);
    assert(!ui_page_get_state()->playing);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    assert(ui_page_current() == UI_PAGE_LIBRARY);
    /* FUNC 删除当前录音 */
    ui_page_handle_func();
    assert(record_track_count() == 0U);
    assert(ui_page_get_state()->library_sel < Song_GetBuiltinCount());
    capture_ppm("build/ui_library_deleted.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    assert(ui_page_current() == UI_PAGE_MAIN);

    /* 系统设置：选择项切换、循环递增、长按保存 */
    nav_main_to(8U);
    capture_ppm("build/ui_main_scrolled.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_SETTINGS);
    assert(ui_page_get_state()->settings_sel == 0U);
    capture_ppm("build/ui_settings.ppm");
    uint8_t brightness = ui_page_get_state()->brightness;
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    assert(ui_page_get_state()->brightness == (uint8_t)((brightness % 5U) + 1U));

    ui_page_handle_func();
    assert(ui_page_get_state()->settings_sel == 1U);
    uint8_t volume = Audio_GetVolume();
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    assert(Audio_GetVolume() == (uint8_t)((volume + 1U) % VOL_COUNT));

    /* 音色 5 档：选中音色行并放入第 5 档，整页重绘检查滑块不越界。 */
    ui_page_handle_func();
    assert(ui_page_get_state()->settings_sel == 2U);
    Audio_SetTimbre(TIMBRE_MUSICBOX);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);   /* 回到首行并整页重绘 */
    assert(ui_page_get_state()->settings_sel == 0U);
    assert(Audio_GetTimbre() == TIMBRE_MUSICBOX);
    capture_ppm("build/ui_settings_tone5.ppm");

    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    assert(ui_page_current() == UI_PAGE_SETTINGS);
    capture_ppm("build/ui_popup.ppm");
    mock_tick += 500U;
    ui_page_tick(mock_tick);
    assert(ui_page_current() == UI_PAGE_MAIN);
    assert(ui_page_get_state()->saved_tone == 5U);

    /* 返回演奏页：OCT/VOL/KEY 必须从音频模块重新读取，而不是沿用旧显示。 */
    nav_main_to(0U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_PERFORM);
    mock_tick += 100U;
    ui_page_tick(mock_tick);
    mock_tick += 100U;
    ui_page_tick(mock_tick);
    capture_ppm("build/ui_perform_synced.ppm");

    printf("ui: pages, transitions and bounds ok\n");
}

static void test_overdub_flow(void)
{
    Audio_Init();
    mock_tick = 0U;
    assert(record_init(NULL));
    ui_page_init();
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);   /* HOME → MAIN */
    nav_main_to(4U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_RECORD);

    /* 先录一条两音符的基础录音。 */
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(record_is_recording());
    VoiceId v0 = Audio_NoteOn(0U);
    assert(v0 >= 0);
    record_note_press(0U, v0, Audio_GetOctave(), Audio_GetVoiceFreq(v0), mock_tick);
    mock_tick += 10U;
    record_note_release(0U, mock_tick);
    Audio_NoteOff(v0);
    VoiceId v1 = Audio_NoteOn(2U);
    assert(v1 >= 0);
    record_note_press(2U, v1, Audio_GetOctave(), Audio_GetVoiceFreq(v1), mock_tick);
    mock_tick += 10U;
    record_note_release(2U, mock_tick);
    Audio_NoteOff(v1);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_LIBRARY);
    assert(record_track_count() == 1U);

    /* 回录音页，FUNC 切到叠录，选择 REC_01 并录入一个新音符。 */
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    assert(ui_page_current() == UI_PAGE_MAIN);
    nav_main_to(4U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_RECORD);
    assert(ui_page_get_state()->record_mode == 0U);
    ui_page_handle_func();
    assert(ui_page_get_state()->record_mode == 1U);
    assert(ui_page_get_state()->record_sel == 0U);
    capture_ppm("build/ui_record_overdub.ppm");

    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(record_overdub_active());
    assert(record_is_recording());
    check_record_live_params();
    mock_tick += 50U;
    ui_page_tick(mock_tick);   /* 推进原曲回放 */

    VoiceId ov = Audio_NoteOn(1U);
    assert(ov >= 0);
    record_note_press(1U, ov, Audio_GetOctave(), Audio_GetVoiceFreq(ov), mock_tick);
    mock_tick += 20U;
    record_note_release(1U, mock_tick);
    Audio_NoteOff(ov);
    ui_page_tick(mock_tick);

    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(!record_is_recording());
    assert(ui_page_current() == UI_PAGE_LIBRARY);
    assert(record_track_count() >= 1U);

    record_info_t info;
    assert(record_get_info(0U, &info));
    assert(info.event_count >= 3U);   /* 原 2 个 + 叠录 1 个 */
    capture_ppm("build/ui_library_overdub.ppm");
    puts("overdub: mode switch, select, original playback, merge and library sync ok");
}

static void test_runtime_regressions(void)
{
    Audio_Init();
    mock_tick = 0U;
    assert(TIM2->PSC == 0U && TIM2->ARR == 255U);
    assert(TIM4->ARR == 2249U);
    assert(TIM3->polarity == TIM_OCPOLARITY_LOW);
    AudioPwm_SampleIRQ();
    assert(TIM2->CCR1 == 0U);
    assert(AudioPwm_SetTone(0U, 1000U, 500U));
    for (unsigned i = 0U; i < 256U; ++i) {
        AudioPwm_SampleIRQ();
    }
    unsigned edges = 0U;
    uint32_t previous = TIM2->CCR1;
    for (unsigned i = 0U; i < AUDIO_SAMPLE_HZ; ++i) {
        AudioPwm_SampleIRQ();
        edges += TIM2->CCR1 != previous;
        previous = TIM2->CCR1;
    }
    assert(edges >= 1999U && edges <= 2001U); /* 1000 Hz, two edges per cycle. */
    assert(AudioPwm_SetTone(1U, 1500U, 500U));
    unsigned transitions = 0U;
    uint32_t last = TIM2->CCR1;
    for (unsigned i = 0U; i < AUDIO_SAMPLE_HZ; ++i) {
        AudioPwm_SampleIRQ();
        uint32_t value = TIM2->CCR1;
        assert(value <= AUDIO_PWM_PERIOD - 1U);
        transitions += (value != last);
        last = value;
    }
    assert(transitions > 2000U);
    AudioPwm_Silence(0U);
    AudioPwm_Silence(1U);
    for (unsigned i = 0U; i < 128U; ++i) {
        AudioPwm_SampleIRQ();
    }
    assert(TIM2->CCR1 == 0U);
    Metronome_Start();
    Metronome_Tick();
    assert(TIM3->CCR4 > 0U);
    mock_tick = 31U;
    AudioMetroBeep_Tick(mock_tick); /* Main-loop redraw cannot lengthen the click. */
    assert(TIM3->CCR4 == 0U);
    Metronome_Stop();

    /* 节拍器音量 3 档：PWM 占空比逐级增大；越界值必须被拒绝。 */
    AudioMetroBeep_SetVolume(1U);
    AudioMetroBeep_Play(1000U, 30U);
    uint32_t cc1 = TIM3->CCR4;
    AudioMetroBeep_Stop();
    AudioMetroBeep_SetVolume(2U);
    AudioMetroBeep_Play(1000U, 30U);
    uint32_t cc2 = TIM3->CCR4;
    AudioMetroBeep_Stop();
    AudioMetroBeep_SetVolume(3U);
    AudioMetroBeep_Play(1000U, 30U);
    uint32_t cc3 = TIM3->CCR4;
    AudioMetroBeep_Stop();
    assert(cc1 == cc2 && cc2 == cc3); /* 蜂鸣器固定 50% 占空比 */
    assert(Metronome_SetVolume(0U) == false);
    assert(Metronome_SetVolume(1U));
    assert(Metronome_GetVolume() == 1U);
    assert(Metronome_SetVolume(3U));
    assert(Metronome_GetVolume() == 3U);
    puts("speaker: two-voice mix, midpoint, headroom, PB1 timeout and 3-step volume ok");

    /* 全局 5 档音量：档位越高振幅越大；固定增益的提示音不受音量影响。 */
    Audio_AllOff();
    settle_pwm_idle();
    Audio_SetVolume(VOL_5);
    assert(measure_peak_deviation() == 0);
    VoiceId sfx = AUDIO_VOICE_INVALID;
    assert(Audio_NoteOnPitchGain(1000U, AUDIO_SFX_PERMILLE,
                                 AUDIO_PRIO_MELODY, &sfx) == AUDIO_OK);
    { int p = measure_peak_deviation(); assert(p >= 60 && p <= 80); }
    Audio_AllOff();

    VoiceId vol_voice = AUDIO_VOICE_INVALID;
    Audio_SetVolume(VOL_1);
    assert(Audio_NoteOnPitch(1000U, &vol_voice) == AUDIO_OK);
    int vol1_peak = measure_peak_deviation();
    Audio_SetVolume(VOL_3);
    int vol3_peak = measure_peak_deviation();
    Audio_SetVolume(VOL_5);
    int vol5_peak = measure_peak_deviation();
    assert(vol1_peak >= 26 && vol1_peak <= 34);
    assert(vol3_peak >= 55 && vol3_peak <= 65);
    assert(vol5_peak >= 108 && vol5_peak <= 120);
    assert(vol1_peak < vol3_peak && vol3_peak < vol5_peak);
    Audio_SetVolume((VolumeId)VOL_COUNT);        /* 越界值必须被忽略 */
    assert(Audio_GetVolume() == VOL_5);
    Audio_AllOff();
    puts("volume: five levels, live update and fixed-gain sfx ok");

    /* 三个音色：包络形状与波表都要能出声、能收尾。 */
    Audio_SetVolume(VOL_5);

    Audio_SetTimbre(TIMBRE_ORGAN);
    mock_tick = 0U;
    VoiceId tv = AUDIO_VOICE_INVALID;
    assert(Audio_NoteOnPitch(1000U, &tv) == AUDIO_OK);
    { int p = measure_peak_deviation(); assert(p >= 105 && p <= 125); }        /* 窄脉冲满幅 */
    Audio_AllOff();
    settle_pwm_idle();

    Audio_SetTimbre(TIMBRE_PIANO);
    mock_tick = 0U;
    assert(Audio_NoteOnPitch(1000U, &tv) == AUDIO_OK);
    assert(measure_peak_deviation() == 0);        /* 起音从 0 开始 */
    mock_tick = 10U;
    Audio_Envelope_Tick();
    int piano_early = measure_peak_deviation();
    assert(piano_early >= 105 && piano_early <= 125);                     /* 起音完成后满幅 */
    mock_tick = 1000U;
    Audio_Envelope_Tick();
    int piano_late = measure_peak_deviation();
    assert(piano_late >= 1 && piano_late < piano_early);
    Audio_AllOff();
    settle_pwm_idle();

    Audio_SetTimbre(TIMBRE_GUITAR);
    mock_tick = 0U;
    assert(Audio_NoteOnPitch(1000U, &tv) == AUDIO_OK);
    assert(measure_peak_deviation() == 0);        /* 起音从 0 开始 */
    mock_tick = 10U;
    Audio_Envelope_Tick();
    int guitar_early = measure_peak_deviation();
    assert(guitar_early >= 105 && guitar_early <= 125);                    /* 拨弦瞬间满幅 */
    mock_tick = 1200U;
    Audio_Envelope_Tick();
    int guitar_sustain = measure_peak_deviation();
    assert(guitar_sustain >= 1 && guitar_sustain < guitar_early);
    mock_tick = 2600U;
    Audio_Envelope_Tick();
    assert(measure_peak_deviation() >= 1);        /* 30% 持续，不归零 */
    Audio_AllOff();

    Audio_SetTimbre(TIMBRE_ORGAN);
    Audio_SetTimbre((TimbreId)TIMBRE_COUNT);      /* 越界值必须被忽略 */
    assert(Audio_GetTimbre() == TIMBRE_ORGAN);
    puts("timbre: organ pulse, piano decay and guitar sustain ok");

    /* A long held note survives pause/resume; keyboard survives playback stop. */
    assert(record_init(NULL));
    assert(record_start(100U));
    record_note_press(0U, 0, OCT_MID, 262U, 100U);
    record_note_release(0U, 1100U);
    assert(record_stop_and_save(1100U) == RECORD_RESULT_OK);
    assert(record_playback_start(0U, 2000U));
    record_playback_tick(2000U);
    assert(Audio_GetActiveCount() == 1U);
    VoiceId key = Audio_NoteOn(NOTE_LA);
    assert(key >= 0);
    record_playback_set_paused(true, 2100U);
    assert(Audio_IsVoiceActive(key));
    assert(Audio_GetActiveCount() == 1U);
    record_playback_set_paused(false, 4000U);
    assert(Audio_GetActiveCount() == 2U);
    record_playback_stop();
    assert(Audio_IsVoiceActive(key));
    Audio_NoteOff(key);
    assert(record_playback_start(0U, 5000U));
    record_playback_tick(6100U); /* Entire note expired during a blocked main loop. */
    assert(Audio_GetActiveCount() == 0U);
    assert(!record_playback_active());
    assert(record_delete(0U));
    assert(record_start(7000U));
    assert(record_stop_and_save(7000U) == RECORD_RESULT_EMPTY);
    assert(record_track_count() == 0U);

    /* Tick wrap and a 600-event recording exercise the full buffer and slot. */
    assert(record_start(0xFFFFFFF0U));
    record_note_press(0U, 0, OCT_MID, 262U, 0xFFFFFFF8U);
    record_note_release(0U, 8U);
    assert(record_stop_and_save(9U) == RECORD_RESULT_OK);
    record_info_t info;
    assert(record_get_info(0U, &info) && info.duration_ms == 24U);
    assert(record_delete(0U));
    assert(record_start(0U));
    for (unsigned i = 0U; i < RECORD_MAX_EVENTS; ++i) {
        record_note_press(0U, 0, OCT_MID, 262U, i * 10U);
        record_note_release(0U, i * 10U + 5U);
    }
    assert(record_is_full());
    assert(record_stop_and_save(6000U) == RECORD_RESULT_OK);
    assert(record_init(NULL));
    assert(record_get_info(0U, &info) && info.event_count == 600U);
    assert(record_playback_start(0U, 0U));
    for (unsigned i = 0U; i < 6000U; ++i) { record_playback_tick(i); }
    assert(!record_playback_active());
    assert(Audio_GetActiveCount() == 0U);
    assert(record_delete(0U));
    uint8_t settings[2] = {0xA6U, 0x05U}, loaded[2] = {0};
    assert(record_setting_save(settings, 2U));
    assert(record_setting_load(loaded, 2U));
    assert(memcmp(settings, loaded, 2U) == 0);
    settings[1] = 0U;
    assert(record_setting_save(settings, 2U));
    puts("recording: owned voices, expired notes, tick wrap, empty save, 600 events and settings ok");

    static const SongNote notes[] = {{NOTE_DO, OCT_MID, 100U, 0U}};
    static const SongDef song = {"Test", notes, 1U, false, 0U, NULL, 0U, NULL};
    mock_tick = 0U;
    Song_SetSpeed(2U);
    assert(Song_Play(&song));
    assert(Song_GetTotalMs() == 65U);
    mock_tick = 25U;
    Song_Tick();
    assert(Song_GetElapsedMs() == 25U);
    assert(Song_GetProgress() >= 38U && Song_GetProgress() <= 39U);
    Song_Pause();
    mock_tick = 1000U;
    Song_Resume();
    Song_Tick();
    assert(Song_GetElapsedMs() == 25U);
    Song_Stop();

    /* 开机琶音用固定第 3 档：即使全局音量拉到 5，也仍然是 5 个计数。 */
    Audio_SetVolume(VOL_5);
    Audio_AllOff();
    assert(Song_PlayGain(Song_GetBootChime(), AUDIO_SFX_PERMILLE));
    { int p = measure_peak_deviation(); assert(p >= 60 && p <= 80); }
    Song_Stop();
    Audio_AllOff();
    puts("song: speed progress and resume clock ok");
}

static void test_rhythm_chart_data(void)
{
    uint16_t i;
    uint16_t j;

    assert(g_rhythm_chart.count == 30U);
    assert(g_rhythm_chart.duration_ms == 30000U);
    assert(g_rhythm_chart.notes[0].time_ms == 1500U);
    assert(g_rhythm_chart.notes[0].track == 2U);       /* Mi */
    assert(g_rhythm_chart.notes[29].time_ms == 26500U);
    assert(g_rhythm_chart.notes[29].dur_ms == 1500U);
    assert(((uint32_t)g_rhythm_chart.notes[29].time_ms +
            g_rhythm_chart.notes[29].dur_ms) <= g_rhythm_chart.duration_ms);

    for (i = 0U; i < g_rhythm_chart.count; ++i) {
        const rhythm_chart_note_t *n = &g_rhythm_chart.notes[i];
        assert(n->track <= 4U);       /* 本编配只用 Do..Sol，保留 7 轨 UI */
        assert(n->dur_ms > 0U);
        if (i > 0U) {
            assert(n->time_ms > g_rhythm_chart.notes[i - 1U].time_ms);
        }
        for (j = 0U; j < i; ++j) {
            if (g_rhythm_chart.notes[j].track == n->track) {
                /* 同轨最近两音必须大于 200 ms，避免一次按键同时够到两个目标 */
                assert((uint16_t)(n->time_ms - g_rhythm_chart.notes[j].time_ms) > 200U);
            }
        }
    }
    puts("chart: Ode to Joy 30 notes, 30 s, monotonic, tracks/gaps ok");
}

static void test_rhythm_difficulty(void)
{
    rhythm_stats_t st;
    uint32_t t0;

    Audio_Init();
    record_init(NULL);
    ui_page_init();
    mock_tick = 700000U;

    rhythm_game_enter();
    rhythm_game_handle_event(BSP_ENCODER_EVENT_CW);   /* LOW -> MED */
    rhythm_game_handle_event(BSP_ENCODER_EVENT_CW);   /* MED -> HIGH */
    rhythm_game_get_stats(&st);
    assert(st.difficulty == 2U);
    assert(st.speed_pct == 150U);
    capture_framebuffer("build/rhythm_home_high.ppm");

    mock_tick += 10U;
    t0 = mock_tick;
    rhythm_game_handle_event(BSP_ENCODER_EVENT_SHORT);
    mock_tick = t0 + 2800U;
    rhythm_game_tick(mock_tick);
    mock_tick += 1000U;                               /* 1 s real = 1.5 s chart */
    rhythm_game_tick(mock_tick);
    rhythm_game_get_stats(&st);
    assert(st.state == RHYTHM_STATE_PLAY);
    assert(st.chart_ms >= 1499U && st.chart_ms <= 1501U);

    /* 高难度下，第一个音在真实 1.0 s 处、曲谱 1500 ms 处，仍可 PERFECT */
    rhythm_game_on_key(g_rhythm_chart.notes[0].track, true, mock_tick);
    rhythm_game_get_stats(&st);
    assert(st.perfect == 1U);
    assert(st.points == 100U);

    rhythm_game_leave();
    assert(!rhythm_game_active());
    puts("rhythm: LOW/MED/HIGH speed scaling ok");
}

static void test_rhythm_game(void)
{
    rhythm_stats_t st;
    uint32_t t0;
    uint32_t run_start;
    uint16_t n = g_rhythm_chart.count;
    uint32_t chart_end;
    uint32_t saved_points;
    uint16_t miss_before;

    Audio_Init();
    record_init(NULL);
    ui_page_init();
    mock_tick = 500000U;

    /* 首页 -> 主菜单 -> 音乐游戏 */
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    Song_Stop();
    Audio_AllOff();
    nav_main_to(6U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_RHYTHM);
    assert(rhythm_game_active());
    assert(Audio_GetTimbre() == TIMBRE_PIANO);
    assert(Audio_GetOctave() == OCT_MID);
    assert(Audio_GetTranspose() == 0);

    /* 难度选择走真实的 ui_page -> game 事件链：首页旋转切 LOW/MED/HIGH */
    rhythm_game_get_stats(&st);
    assert(st.difficulty == 0U);
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    rhythm_game_get_stats(&st);
    assert(st.difficulty == 1U);
    ui_page_handle_event(BSP_ENCODER_EVENT_CCW);
    rhythm_game_get_stats(&st);
    assert(st.difficulty == 0U);

    capture_ppm("build/rhythm_home.ppm");

    /* 倒计时 -> PLAY */
    mock_tick += 10U;
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    t0 = mock_tick;
    capture_ppm("build/rhythm_countdown.ppm");
    mock_tick = t0 + 2800U;
    ui_page_tick(mock_tick);
    run_start = mock_tick;
    rhythm_game_get_stats(&st);
    assert(st.state == RHYTHM_STATE_PLAY);
    capture_ppm("build/rhythm_play.ppm");

    /* 判定窗口的“上边界”必须包含：+60 PERFECT / +120 GREAT / +200 GOOD */
    rhythm_game_on_key(g_rhythm_chart.notes[0].track, true,
                       run_start + g_rhythm_chart.notes[0].time_ms + 60U);
    rhythm_game_on_key(g_rhythm_chart.notes[1].track, true,
                       run_start + g_rhythm_chart.notes[1].time_ms + 120U);
    rhythm_game_on_key(g_rhythm_chart.notes[2].track, true,
                       run_start + g_rhythm_chart.notes[2].time_ms + 200U);
    rhythm_game_get_stats(&st);
    assert(st.perfect == 1U && st.great == 1U && st.good == 1U);
    assert(st.points == 210U);
    assert(st.combo == 3U && st.max_combo == 3U);
    capture_ppm("build/rhythm_judge.ppm");

    /* 错键：该轨此刻没有目标音符，仍算一次错误输入 */
    rhythm_game_on_key((uint8_t)((g_rhythm_chart.notes[3].track + 3U) % 7U), true,
                       run_start + g_rhythm_chart.notes[3].time_ms);
    rhythm_game_get_stats(&st);
    assert(st.extra == 1U);
    assert(st.combo == 0U);

    /* 同一个音符重复按：不再加分，只多一次错误输入 */
    saved_points = st.points;
    rhythm_game_on_key(g_rhythm_chart.notes[0].track, true,
                       run_start + g_rhythm_chart.notes[0].time_ms + 40U);
    rhythm_game_get_stats(&st);
    assert(st.points == saved_points);
    assert(st.extra == 2U);

    /* 漏键：越过 note[3] 的 +200 ms 窗口 */
    mock_tick = run_start + g_rhythm_chart.notes[3].time_ms + 300U;
    ui_page_tick(mock_tick);
    rhythm_game_get_stats(&st);
    assert(st.miss >= 1U);
    assert(st.combo == 0U);

    /* 暂停：曲谱时间冻结，不继续判 MISS */
    miss_before = st.miss;
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    capture_ppm("build/rhythm_pause.ppm");
    mock_tick += 5000U;
    ui_page_tick(mock_tick);
    rhythm_game_get_stats(&st);
    assert(st.state == RHYTHM_STATE_PAUSE);
    assert(st.miss == miss_before);
    assert(st.chart_ms <= (mock_tick - run_start) + 1U);

    /* CONTINUE -> 倒计时 -> 恢复，曲谱时间不跳 */
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    mock_tick += 2800U;
    ui_page_tick(mock_tick);
    rhythm_game_get_stats(&st);
    assert(st.state == RHYTHM_STATE_PLAY);
    assert(st.chart_ms <= 6100U);   /* 暂停在 ~6.1 s 以内，恢复后不能跳到 11 s */

    /* 长按 -> 暂停菜单，选 EXIT -> 回主菜单并恢复音频状态 */
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    ui_page_tick(mock_tick + 1U);
    assert(!rhythm_game_active());
    assert(ui_page_current() == UI_PAGE_MAIN);
    assert(ui_page_get_state()->main_sel == 6U);
    assert(Audio_GetTimbre() == TIMBRE_ORGAN);
    assert(Audio_GetOctave() == OCT_MID);
    assert(Audio_GetTranspose() == 0);

    /* 完美全连：一次按完所有 30 个音符，最终 100000 / S */
    mock_tick = 600000U;
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);  /* enter rhythm */
    assert(ui_page_current() == UI_PAGE_RHYTHM);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);  /* start */
    t0 = mock_tick;
    mock_tick = t0 + 2800U;
    ui_page_tick(mock_tick);
    run_start = mock_tick;
    for (uint16_t i = 0U; i < n; ++i) {
        rhythm_game_on_key(g_rhythm_chart.notes[i].track, true,
                           run_start + g_rhythm_chart.notes[i].time_ms);
    }
    rhythm_game_get_stats(&st);
    assert(st.perfect == n);
    assert(st.max_combo == n);
    assert(st.points == (uint32_t)n * 100U);

    chart_end = run_start + g_rhythm_chart.duration_ms + 10U;
    mock_tick = chart_end;
    ui_page_tick(mock_tick);
    rhythm_game_get_stats(&st);
    assert(st.state == RHYTHM_STATE_RESULT);
    assert(st.miss == 0U && st.extra == 0U);
    assert(st.final_score == 100000U);
    assert(st.grade == RHYTHM_GRADE_S);
    capture_ppm("build/rhythm_result.ppm");

    /* 结算 BACK -> 主菜单 */
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    ui_page_tick(mock_tick + 1U);
    assert(ui_page_current() == UI_PAGE_MAIN);
    assert(!rhythm_game_active());

    puts("rhythm: judgement windows, combo, miss, pause and perfect run ok");
}

/* AC power checks catch quiet waveforms that a peak-only test misses. */
static double measure_ac_power(void)
{
    for (unsigned i = 0U; i < 256U; ++i) { AudioPwm_SampleIRQ(); }
    double sum = 0.0, sum2 = 0.0;
    for (unsigned i = 0U; i < 32000U; ++i) {
        AudioPwm_SampleIRQ();
        double value = TIM2->CCR1;
        assert(value < AUDIO_PWM_PERIOD);
        sum += value;
        sum2 += value * value;
    }
    double mean = sum / 32000.0;
    return sum2 / 32000.0 - mean * mean;
}

static void test_audio_loudness(void)
{
    Audio_Init();
    Audio_SetTimbre(TIMBRE_ORGAN);
    Audio_SetVolume(VOL_1);
    VoiceId id = AUDIO_VOICE_INVALID;
    assert(Audio_NoteOnPitch(1000U, &id) == AUDIO_OK);
    double previous = 0.0;
    for (unsigned v = 0U; v < VOL_COUNT; ++v) {
        Audio_SetVolume((VolumeId)v);
        double power = measure_ac_power();
        if (v != 0U) {
            assert(power > previous * 1.7 && power < previous * 2.3);
        }
        previous = power;
    }
    assert(previous > 60.0 * 60.0);
    Audio_AllOff();

    const uint8_t waves[] = {AUDIO_WAVE_ORGAN, AUDIO_WAVE_BELL, AUDIO_WAVE_PIANO};
    for (unsigned i = 0U; i < sizeof(waves) / sizeof(waves[0]); ++i) {
        Audio_Init();
        AudioPwm_SetWave(0U, waves[i]);
        assert(AudioPwm_SetTone(0U, 1000U, 500U));
        assert(measure_ac_power() > 60.0 * 60.0);
        Audio_AllOff();
        settle_pwm_idle();
        assert(TIM2->CCR1 == 0U);
    }
    puts("loudness: roughly 3dB volume steps, waveform AC power and headroom ok");
}

static uint32_t melody_hash(void)
{
    melody_info_t info;
    melody_maker_get_info(&info);
    const uint8_t *raw = (const uint8_t *)melody_maker_events();
    uint32_t hash = 2166136261U;
    for (unsigned i = 0U; i < info.count * sizeof(melody_event_t); ++i) {
        hash = (hash ^ raw[i]) * 16777619U;
    }
    return hash;
}

static void test_melody_generation(void)
{
    unsigned coverage[TIMBRE_COUNT][3] = {{0}};
    unsigned rests[3] = {0}, slots[3] = {0}, short_slots[3] = {0};
    const uint8_t bpm_low[3] = {65,110,80}, bpm_high[3] = {85,140,105};
    uint32_t min_ms = UINT32_MAX, max_ms = 0U, previous_hash = 0U;
    unsigned different = 0U;
    for (unsigned timbre = 0U; timbre < TIMBRE_COUNT; ++timbre) {
        for (uint32_t seed = 0U; seed < 500U; ++seed) {
            assert(melody_maker_generate((TimbreId)timbre, seed));
            melody_info_t info;
            melody_maker_get_info(&info);
            assert(info.style < 3U && info.timbre == timbre);
            assert(info.bpm >= bpm_low[info.style] && info.bpm <= bpm_high[info.style]);
            assert(info.total_ms >= 15000U && info.total_ms <= 20000U);
            assert(info.total_ms == (uint32_t)info.bars * 240000U / info.bpm);
            assert(info.count > 0U && info.count <= MELODY_MAX_EVENTS);
            assert(info.bars >= 5U && info.bars <= 10U);
            ++coverage[timbre][info.style];
            if (info.total_ms < min_ms) { min_ms = info.total_ms; }
            if (info.total_ms > max_ms) { max_ms = info.total_ms; }
            const melody_event_t *e = melody_maker_events();
            assert(e[0].start_ms == 0U && e[0].note == NOTE_DO);
            assert(e[info.count - 1U].note == NOTE_DO);
            assert(e[info.count - 1U].start_ms + e[info.count - 1U].duration_ms == info.total_ms);
            unsigned bar_units = 0U;
            for (unsigned i = 0U; i < info.count; ++i) {
                assert(e[i].duration_ms > 0U);
                unsigned unit = 0U;
                while (unit <= info.bars * 8U && unit * 30000U / info.bpm != e[i].start_ms) { ++unit; }
                assert(unit < info.bars * 8U);
                uint32_t next = (i + 1U < info.count) ? e[i + 1U].start_ms : info.total_ms;
                assert(next > e[i].start_ms && e[i].start_ms + e[i].duration_ms <= next);
                unsigned end_unit = unit + 1U;
                while (end_unit <= info.bars * 8U && end_unit * 30000U / info.bpm != next) { ++end_unit; }
                unsigned length = end_unit - unit;
                assert(length == 1U || length == 2U || length == 4U);
                bar_units += length;
                assert(bar_units <= 8U);
                if (bar_units == 8U) { bar_units = 0U; }
                ++slots[info.style];
                short_slots[info.style] += length == 1U;
                rests[info.style] += e[i].note == MELODY_REST;
                assert(e[i].octave == (timbre == TIMBRE_BASS ? OCT_LOW :
                                      (timbre == TIMBRE_MUSICBOX ? OCT_HIGH : OCT_MID)));
                if (e[i].note != MELODY_REST) {
                    assert(e[i].note < NOTE_COUNT);
                    uint16_t hz = Audio_GetFreqOfNote(e[i].note, (OctaveId)e[i].octave, 0);
                    assert(hz >= AUDIO_FREQ_MIN_HZ && hz <= AUDIO_FREQ_MAX_HZ);
                }
            }
            assert(bar_units == 0U);
            uint32_t hash = melody_hash();
            different += hash != previous_hash;
            previous_hash = hash;
            assert(melody_maker_generate((TimbreId)timbre, seed));
            assert(melody_hash() == hash);
        }
    }
    for (unsigned t = 0U; t < TIMBRE_COUNT; ++t) {
        for (unsigned style = 0U; style < 3U; ++style) { assert(coverage[t][style] > 0U); }
    }
    assert(different > 2400U);
    assert(short_slots[MELODY_HAPPY] * slots[MELODY_CALM] > short_slots[MELODY_CALM] * slots[MELODY_HAPPY]);
    assert(rests[MELODY_DREAM] * slots[MELODY_CALM] > rests[MELODY_CALM] * slots[MELODY_DREAM]);
    assert(!melody_maker_generate((TimbreId)TIMBRE_COUNT, 1U));
    printf("melody generation: 2500 seeds/timbres, all 15 combinations, whole bars, %lu..%lums ok\n",
           (unsigned long)min_ms, (unsigned long)max_ms);
}

static void melody_advance(uint32_t now)
{
    mock_tick = now;
    Audio_Envelope_Tick();
    assert(!melody_maker_tick(now));
    assert(Audio_GetActiveCount() <= 1U);
}

static void test_melody_playback(void)
{
    Audio_Init();
    mock_tick = 100U;
    Audio_SetTimbre(TIMBRE_GUITAR);
    Audio_SetOctave(OCT_HIGH);
    Audio_SetTranspose(-2);
    Audio_SetVolume(VOL_4);
    Metronome_SetBpm(123U);
    Metronome_SetBeatsPerBar(3U);
    Metronome_Start();
    Metronome_Tick();
    melody_maker_enter();
    assert(melody_maker_active() && !Metronome_IsRunning());
    assert(TIM3->CCR4 == 0U);
    for (unsigned t = 0U; t < TIMBRE_COUNT; ++t) {
        assert(melody_maker_generate((TimbreId)t, t + 1U));
        assert(melody_maker_replay(1000U));
        uint32_t hash = melody_hash();
        melody_info_t info;
        melody_maker_get_info(&info);
        assert(Audio_GetTimbre() == (TimbreId)t && Audio_GetTranspose() == 0);
        for (uint32_t ms = 1000U; ms <= 1000U + info.total_ms + 10U; ms += 5U) {
            melody_advance(ms);
            melody_info_t current;
            melody_maker_get_info(&current);
            if (current.current_note != MELODY_REST) { assert(Audio_GetActiveCount() == 1U); }
            assert(TIM3->CCR4 == 0U);
        }
        melody_maker_get_info(&info);
        assert(info.state == MELODY_FINISHED && info.elapsed_ms == info.total_ms);
        assert(Audio_GetActiveCount() == 0U && info.current_note == MELODY_REST);
        assert(melody_hash() == hash);
        assert(melody_maker_replay(0xFFFFFFF0U));
        melody_advance(0xFFFFFFF0U);
        melody_advance(20U); /* Millisecond clock wraps while playing. */
        melody_maker_get_info(&info);
        assert(info.elapsed_ms == 36U);
        melody_advance((uint32_t)(0xFFFFFFF0U + info.total_ms + 1U));
        assert(Audio_GetActiveCount() == 0U && melody_hash() == hash);
    }
    assert(melody_maker_generate(TIMBRE_ACCORDION, 998U));
    assert(melody_maker_replay(0U));
    melody_advance(0U);
    const melody_event_t *events = melody_maker_events();
    melody_info_t info;
    melody_maker_get_info(&info);
    bool tested_rest = false;
    for (unsigned i = 0U; i < info.count; ++i) {
        if (events[i].note == MELODY_REST) {
            melody_advance(events[i].start_ms + 1U);
            assert(Audio_GetActiveCount() == 0U);
            tested_rest = true;
            break;
        }
    }
    assert(tested_rest);
    /* The final tonic gets its complete two-beat slot, then releases once. */
    uint32_t final_start = events[info.count - 1U].start_ms;
    assert(events[info.count - 1U].note == NOTE_DO);
    assert(melody_maker_replay(0U));
    melody_advance(final_start);
    melody_maker_get_info(&info);
    assert(info.current_note == NOTE_DO && Audio_GetActiveCount() == 1U);
    melody_advance(info.total_ms - 1U);
    assert(Audio_GetActiveCount() == 1U);
    melody_advance(info.total_ms);
    melody_maker_get_info(&info);
    assert(info.state == MELODY_FINISHED && Audio_GetActiveCount() == 0U);
    melody_maker_leave();
    assert(!melody_maker_active());
    assert(Audio_GetActiveCount() == 0U);
    assert(Audio_GetTimbre() == TIMBRE_GUITAR && Audio_GetOctave() == OCT_HIGH);
    assert(Audio_GetTranspose() == -2 && Audio_GetVolume() == VOL_4);
    assert(Metronome_IsRunning() && Metronome_GetBpm() == 123U && Metronome_GetBeatsPerBar() == 3U);
    Metronome_Stop();
    puts("melody playback: five timbres, finish/replay, wrap, skipped events and state restoration ok");
}

static void test_melody_ui(void)
{
    Audio_Init();
    record_init(NULL);
    ui_page_init();
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    nav_main_to(7U);
    capture_framebuffer("build/melody_menu.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_MELODY && melody_maker_active());
    capture_framebuffer("build/melody_select.ppm");
    melody_info_t info;
    for (unsigned i = 0U; i < 5U; ++i) {
        ui_page_handle_event(BSP_ENCODER_EVENT_CW);
        melody_maker_get_info(&info);
        assert(info.timbre == (TIMBRE_ORGAN + i + 1U) % TIMBRE_COUNT);
    }
    melody_maker_get_info(&info);
    assert(info.timbre == TIMBRE_ORGAN);
    ui_page_handle_event(BSP_ENCODER_EVENT_CCW);
    melody_maker_get_info(&info);
    assert(info.timbre == TIMBRE_GUITAR);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    ui_page_tick(mock_tick);
    uint32_t hash = melody_hash();
    melody_maker_get_info(&info);
    assert(info.state == MELODY_PLAYING);
    capture_framebuffer("build/melody_play.ppm");
    ui_page_handle_func_long();
    ui_page_handle_func();
    assert(!Metronome_IsRunning());
    mock_tick += 800U;
    ui_page_tick(mock_tick);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT); /* REPLAY */
    assert(melody_hash() == hash);
    ui_page_handle_event(BSP_ENCODER_EVENT_CW); /* NEW SONG */
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(melody_hash() != hash);
    melody_maker_get_info(&info);
    assert(info.timbre == TIMBRE_GUITAR && info.state == MELODY_PLAYING);
    mock_tick += info.total_ms + 5U;
    ui_page_tick(mock_tick);
    melody_maker_get_info(&info);
    assert(info.state == MELODY_FINISHED);
    capture_framebuffer("build/melody_finished.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_CW); /* BACK */
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    melody_maker_get_info(&info);
    assert(info.state == MELODY_SELECT && Audio_GetActiveCount() == 0U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG); /* Playing -> selector. */
    melody_maker_get_info(&info);
    assert(info.state == MELODY_SELECT);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG); /* Selector -> main. */
    ui_page_tick(mock_tick);
    assert(ui_page_current() == UI_PAGE_MAIN && !melody_maker_active());
    assert(ui_page_get_state()->main_sel == 7U);
    assert(Audio_GetActiveCount() == 0U);
    nav_main_to(8U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_SETTINGS);
    puts("melody UI: selector, replay/new/back, FINISHED, FUNC isolation and ninth menu entry ok");
}

static void test_qinghuaci(void)
{
    Audio_Init();
    assert(Song_GetBuiltinCount() == 7U);
    assert(strcmp(Song_GetBuiltin(0U)->name, "Twinkle Star") == 0);
    assert(strcmp(Song_GetBuiltin(1U)->name, "Two Tigers") == 0);
    assert(strcmp(Song_GetBuiltin(2U)->name, "C4 Timer") == 0);
    const SongDef *def = Song_GetBuiltin(3U);
    assert(def && strcmp(def->name, "QingHuaCi") == 0);
    assert(def->count == 231U && !def->loop && def->timbre == TIMBRE_PIANO + 1U);
    SongDef melody_only = *def;
    melody_only.accompaniment = NULL;
    melody_only.accompaniment_count = 0U;
    uint32_t total = 0U;
    unsigned high = 0U, rests = 0U;
    for (unsigned i = 0U; i < def->count; ++i) {
        const SongNote *n = &def->notes[i];
        assert(n->dur_ms > 0U);
        total += n->dur_ms;
        if (n->note == SONG_NOTE_REST) { ++rests; assert(n->freq_hz == 0U); }
        else {
            assert(n->freq_hz >= 392U && n->freq_hz <= 1760U);
            high += n->freq_hz >= 1047U;
            total += SONG_ARTICULATION_GAP_MS;
        }
    }
    assert(total == 122250U && high > 20U && rests > 20U);
    assert(def->notes[2].freq_hz == 1568U); /* Intro G6: do not fold into G5/G4. */
    assert(def->notes[3].freq_hz == 1760U);
    assert(def->notes[def->count - 1U].freq_hz == 523U);
    for (uint8_t speed = 0U; speed < 3U; ++speed) {
        Audio_SetTimbre(TIMBRE_GUITAR);
        Audio_SetTranspose(2);
        Audio_SetOctave(OCT_LOW);
        Song_SetSpeed(speed);
        mock_tick = 0xFFFFF000U;
        assert(Song_Play(&melody_only));
        assert(Audio_GetTimbre() == TIMBRE_PIANO);
        uint32_t expected_total = 0U;
        for (unsigned i = 0U; i < def->count; ++i) {
            const SongNote *n = &def->notes[i];
            assert(Audio_GetActiveCount() == (n->note == SONG_NOTE_REST ? 0U : 1U));
            if (n->note != SONG_NOTE_REST) {
                bool found = false;
                for (unsigned id = 0U; id < 128U; ++id) {
                    if (Audio_IsVoiceActive((VoiceId)id)) {
                        assert(Audio_GetVoiceFreq((VoiceId)id) == n->freq_hz);
                        found = true;
                    }
                }
                assert(found);
            }
            uint32_t length = speed == 0U ? n->dur_ms * 2U :
                              (speed == 2U ? n->dur_ms / 2U : n->dur_ms);
            expected_total += length;
            mock_tick += length;
            Song_Tick();
            if (n->note != SONG_NOTE_REST) {
                assert(Audio_GetActiveCount() == 0U);
                uint32_t gap = speed == 0U ? 60U : (speed == 2U ? 15U : 30U);
                expected_total += gap;
                mock_tick += gap;
                Song_Tick();
            }
        }
        assert(Song_GetTotalMs() == expected_total);
        assert(Song_GetState() == SONG_IDLE && Audio_GetActiveCount() == 0U);
        assert(Audio_GetTimbre() == TIMBRE_GUITAR);
        assert(Audio_GetTranspose() == 2 && Audio_GetOctave() == OCT_LOW);
    }
    Song_SetSpeed(1U);
    assert(Song_Play(&melody_only));
    Song_Pause();
    assert(Song_GetState() == SONG_PAUSED && Audio_GetActiveCount() == 0U);
    assert(Audio_GetTimbre() == TIMBRE_PIANO);
    Song_Resume();
    assert(Song_GetState() == SONG_PLAYING);
    Song_Stop();
    assert(Audio_GetTimbre() == TIMBRE_GUITAR);
    assert(Song_Play(&melody_only));
    assert(Song_Play(Song_GetBuiltin(0U))); /* Replacing a preset song restores the user timbre. */
    assert(Audio_GetTimbre() == TIMBRE_GUITAR);
    Song_Stop();
    record_init(NULL);
    ui_page_init();
    Audio_SetTimbre(TIMBRE_GUITAR);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    nav_main_to(5U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    for (unsigned i = 0U; i < 3U; ++i) { ui_page_handle_event(BSP_ENCODER_EVENT_CW); }
    assert(ui_page_get_state()->library_sel == 3U);
    capture_framebuffer("build/qinghuaci_library.ppm");
    mock_tick += 200U;
    ui_page_tick(mock_tick); /* Let menu feedback end before checking song-owned sound. */
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_PLAYBACK && Song_GetCurrent() == def);
    assert(Audio_GetTimbre() == TIMBRE_PIANO);
    capture_framebuffer("build/qinghuaci_playback.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(Song_GetState() == SONG_PAUSED);
    ui_page_handle_event(BSP_ENCODER_EVENT_CW); /* Changing speed while paused must not play. */
    assert(Song_GetState() == SONG_PAUSED);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    assert(ui_page_current() == UI_PAGE_LIBRARY && Song_GetState() == SONG_IDLE);
    assert(Audio_GetTimbre() == TIMBRE_GUITAR); /* Menu feedback may still own a separate voice. */
    ui_page_handle_func(); /* Existing per-song deletion must handle the new bit. */
    unsigned remaining = Song_GetBuiltinCount() - 1U + record_track_count();
    unsigned selected = remaining > 3U ? 3U : remaining - 1U;
    assert(ui_page_get_state()->library_sel == selected);
    for (unsigned i = selected; i < remaining; ++i) {
        ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    }
    assert(ui_page_get_state()->library_sel == 0U);
    uint8_t visibility[2] = {0xA6U, 0U};
    assert(record_setting_save(visibility, sizeof(visibility))); /* Restore mock storage for the next case. */
    puts("qinghuaci: 40 bars + 3-beat tail/122.25s, score octaves, three speeds, pause/restart/stop, piano restore and library ok");
}

static void test_faruxue(void)
{
    Audio_Init();
    const SongDef *def = Song_GetBuiltin(4U);
    assert(def && strcmp(def->name, "FaRuXue") == 0 && def->count == 181U);
    assert(!def->loop && def->timbre == TIMBRE_PIANO + 1U);
    assert(def->notes[0].note == SONG_NOTE_REST);
    assert(def->notes[1].freq_hz == 370U && def->notes[2].freq_hz == 415U);
    assert(def->notes[3].freq_hz == 494U && def->notes[4].freq_hz == 622U);
    assert(def->notes[def->count - 1U].freq_hz == 247U); /* B3, held end of first chorus. */
    SongDef melody_only = *def;
    melody_only.accompaniment = NULL;
    melody_only.accompaniment_count = 0U;
    uint32_t total = 0U;
    bool low = false, high = false;
    for (unsigned i = 0U; i < def->count; ++i) {
        const SongNote *n = &def->notes[i];
        assert(n->dur_ms > 0U);
        total += n->dur_ms;
        if (n->note != SONG_NOTE_REST) {
            assert(n->freq_hz >= 185U && n->freq_hz <= 740U);
            total += SONG_ARTICULATION_GAP_MS;
            low |= n->freq_hz == 185U;
            high |= n->freq_hz == 740U;
        }
    }
    assert(total == 93103U && low && high);
    for (uint8_t speed = 0U; speed < 3U; ++speed) {
        Audio_SetTimbre(TIMBRE_MUSICBOX);
        Audio_SetTranspose(-2);
        Audio_SetOctave(OCT_HIGH);
        Song_SetSpeed(speed);
        mock_tick = 0xFFFFFF00U;
        assert(Song_Play(&melody_only));
        assert(Audio_GetTimbre() == TIMBRE_PIANO);
        uint32_t expected = 0U;
        for (unsigned i = 0U; i < def->count; ++i) {
            const SongNote *n = &def->notes[i];
            assert(Audio_GetActiveCount() == (n->note == SONG_NOTE_REST ? 0U : 1U));
            if (n->note != SONG_NOTE_REST) {
                bool found = false;
                for (unsigned id = 0U; id < 128U; ++id) {
                    if (Audio_IsVoiceActive((VoiceId)id)) {
                        assert(Audio_GetVoiceFreq((VoiceId)id) == n->freq_hz);
                        found = true;
                    }
                }
                assert(found);
            }
            uint32_t duration = speed == 0U ? n->dur_ms * 2U :
                                (speed == 2U ? n->dur_ms / 2U : n->dur_ms);
            expected += duration;
            mock_tick += duration;
            Song_Tick();
            if (n->note != SONG_NOTE_REST) {
                assert(Audio_GetActiveCount() == 0U);
                uint32_t gap = speed == 0U ? 60U : (speed == 2U ? 15U : 30U);
                expected += gap;
                mock_tick += gap;
                Song_Tick();
            }
        }
        assert(Song_GetState() == SONG_IDLE && Song_GetTotalMs() == expected);
        assert(Audio_GetTimbre() == TIMBRE_MUSICBOX);
        assert(Audio_GetTranspose() == -2 && Audio_GetOctave() == OCT_HIGH);
        assert(Audio_GetActiveCount() == 0U);
    }
    record_init(NULL);
    ui_page_init();
    Audio_SetTimbre(TIMBRE_MUSICBOX);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    nav_main_to(5U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    for (unsigned i = 0U; i < 4U; ++i) { ui_page_handle_event(BSP_ENCODER_EVENT_CW); }
    assert(ui_page_get_state()->library_sel == 4U);
    capture_framebuffer("build/faruxue_library.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_PLAYBACK && Song_GetCurrent() == def);
    assert(Audio_GetTimbre() == TIMBRE_PIANO);
    capture_framebuffer("build/faruxue_playback.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(Song_GetState() == SONG_PAUSED);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(Song_GetState() == SONG_PLAYING);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    assert(Song_GetState() == SONG_IDLE && Audio_GetTimbre() == TIMBRE_MUSICBOX);
    puts("faruxue: B major/58BPM, 22 bars + 2 beats/93.103s, three speeds, wrap, pause/stop and piano restore ok");
}

static void refresh_measure_begin(void)
{
    spi_bytes = spi_calls = pixel_writes = 0U;
    memset(row_writes, 0, sizeof(row_writes));
    memset(pixel_hits, 0, sizeof(pixel_hits));
}

static void refresh_measure_end(const char *name)
{
    printf("refresh %s: bytes=%lu calls=%lu pixels=%lu header=%lu\n", name,
           (unsigned long)spi_bytes, (unsigned long)spi_calls,
           (unsigned long)pixel_writes, (unsigned long)row_writes[0]);
}

static uint32_t refresh_frame_hash(void)
{
    uint32_t hash = 2166136261U;
    for (unsigned y = 0U; y < ST7735_HEIGHT; ++y) {
        for (unsigned x = 0U; x < ST7735_WIDTH; ++x) {
            hash = (hash ^ framebuffer[y][x]) * 16777619U;
        }
    }
    return hash;
}

static void assert_single_final_write(void)
{
    for (unsigned y = 0U; y < ST7735_HEIGHT; ++y) {
        for (unsigned x = 0U; x < ST7735_WIDTH; ++x) {
            assert(pixel_hits[y][x] <= 1U); /* No visible erase-then-text intermediate pass. */
        }
    }
}

static void compose_fixture(void)
{
    static const uint16_t image[9] = {0xF800,0x07E0,0x001F,0xFFFF,0x0000,0xF81F,0x1234,0x5678,0x9ABC};
    tft_fill_screen(0x0862U);
    tft_fill_rect(-3, -2, 17, 15, 0x001FU);
    tft_fill_gradient_rect(7, 11, 103, 29, 0xF800U, 0x001FU, true);
    tft_fill_gradient_round_rect(3, 44, 121, 28, 0xF800U, 0x07FFU);
    tft_draw_round_rect(3, 44, 121, 28, 0xFFFFU);
    tft_draw_text_font(&font12, 9, 15, "音量", 0xFFFFU, 0x0862U);
    tft_draw_text_font(&font24, 33, 76, "5", 0x07FFU, 0x0862U);
    tft_draw_text_ascii_5x7(-4, 105, "ABC", 2, 0xFFFFU, 0x0862U);
    tft_draw_icon(100, 110, TFT_ICON_VOLUME, 0x07FFU, 16);
    tft_draw_rgb565_image(120, 152, 3, 3, image);
    tft_fill_rect(125, 158, 9, 9, 0xF800U);
}

static void test_compose_strip(void)
{
    static uint16_t reference[ST7735_HEIGHT][ST7735_WIDTH];
    tft_set_y_offset(0);
    compose_fixture();
    memcpy(reference, framebuffer, sizeof(reference));
    tft_fill_screen(0xF81FU);
    refresh_measure_begin();
    tft_redraw_region(0, 0, ST7735_WIDTH, ST7735_HEIGHT, compose_fixture);
    assert(memcmp(reference, framebuffer, sizeof(reference)) == 0);
    assert(pixel_writes == ST7735_WIDTH * ST7735_HEIGHT);
    assert_single_final_write();
    tft_fill_rect(18, 19, 13, 11, 0xF81FU);
    refresh_measure_begin();
    tft_redraw_region(18, 19, 13, 11, compose_fixture);
    assert(memcmp(reference, framebuffer, sizeof(reference)) == 0);
    assert(pixel_writes == 13U * 11U);
    assert_single_final_write();
    tft_set_y_offset(3);
    tft_redraw_region(0, 0, 1, 1, compose_fixture);
    tft_fill_rect(126, 154, 1, 1, 0x1234U);
    assert(framebuffer[157][126] == 0x1234U); /* Caller offset restored after composition. */
    tft_set_y_offset(0);
    puts("compose: clipped fill, gradients, font/icon/image, partial final strip, one final write and offset ok");
}

static void test_refresh_transactions(void)
{
    Audio_Init();
    ui_page_init();
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    nav_main_to(6U);
    refresh_measure_begin();
    ui_page_handle_event(BSP_ENCODER_EVENT_CW); /* List moves from top 0 to 1. */
    refresh_measure_end("menu_scroll");
    assert(spi_bytes < 30000U && spi_calls <= 180U && pixel_writes == 14157U);
    for (unsigned y = 0U; y < 42U; ++y) { assert(row_writes[y] == 0U); }
    assert_single_final_write();
    assert(refresh_frame_hash() == 0xFAA681BEU); /* Pixel-identical to the old full redraw. */
    capture_framebuffer("build/refresh_main_scroll.ppm");
    nav_main_to(8U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    mock_tick += 100U;
    ui_page_tick(mock_tick); /* Finish the existing entry animation before the measurement. */
    refresh_measure_begin();
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    refresh_measure_end("settings_short");
    assert(spi_bytes < 14000U && spi_calls <= 84U && pixel_writes == 6664U);
    assert_single_final_write();
    for (unsigned y = 0U; y < ST7735_HEIGHT; ++y) {
        if (!((y >= 52U && y < 80U) || (y >= 84U && y < 112U))) { assert(row_writes[y] == 0U); }
    }
    assert(refresh_frame_hash() == 0xABC5D82CU);
    capture_framebuffer("build/refresh_settings_short.ppm");
    refresh_measure_begin();
    ui_page_handle_func();
    refresh_measure_end("settings_func");
    assert(spi_bytes < 14000U && pixel_writes == 6664U);
    assert_single_final_write();
    assert(refresh_frame_hash() == 0x18B0009CU);
    capture_framebuffer("build/refresh_settings_func.ppm");
    uint8_t volume = (uint8_t)Audio_GetVolume(), tone = (uint8_t)Audio_GetTimbre();
    uint8_t brightness = ui_page_get_state()->brightness;
    refresh_measure_begin();
    ui_page_handle_func(); /* Tone -> brightness wraps without repainting the middle row. */
    assert(ui_page_get_state()->settings_sel == 0U);
    for (unsigned y = 84U; y < 112U; ++y) { assert(row_writes[y] == 0U); }
    assert_single_final_write();
    assert((uint8_t)Audio_GetVolume() == volume && (uint8_t)Audio_GetTimbre() == tone);
    assert(ui_page_get_state()->brightness == brightness);
    mock_tick += 100U;
    ui_page_tick(mock_tick);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    mock_tick += 500U;
    ui_page_tick(mock_tick);
    assert(ui_page_current() == UI_PAGE_MAIN);
    for (unsigned i = 0U; i < 18U; ++i) {
        uint8_t previous = ui_page_get_state()->main_sel;
        refresh_measure_begin();
        ui_page_handle_event(BSP_ENCODER_EVENT_CW);
        assert(ui_page_get_state()->main_sel == (previous + 1U) % 10U);
        for (unsigned y = 0U; y < 42U; ++y) { assert(row_writes[y] == 0U); }
    }
    for (unsigned i = 0U; i < 18U; ++i) {
        uint8_t previous = ui_page_get_state()->main_sel;
        refresh_measure_begin();
        ui_page_handle_event(BSP_ENCODER_EVENT_CCW);
        assert(ui_page_get_state()->main_sel == (previous + 9U) % 10U);
        for (unsigned y = 0U; y < 42U; ++y) { assert(row_writes[y] == 0U); }
    }
    puts("refresh: unchanged menu/title/values, settings wrap, CW/CCW scrolling and no intermediate blank pixels ok");
}

static uint32_t scaled_score_time(uint32_t ms, uint8_t speed)
{
    return speed == 0U ? ms * 2U : (speed == 2U ? ms / 2U : ms);
}

static uint32_t track_score_total(const SongNote *notes, uint16_t count)
{
    uint32_t total = 0U;
    for (uint16_t i = 0U; i < count; ++i) {
        total += notes[i].dur_ms + (notes[i].note != SONG_NOTE_REST ? SONG_ARTICULATION_GAP_MS : 0U);
    }
    return total;
}

static uint16_t expected_track_pitch(const SongNote *notes, uint16_t count,
                                     uint32_t elapsed, uint8_t speed)
{
    uint32_t start = 0U;
    for (uint16_t i = 0U; i < count; ++i) {
        uint32_t end = start + notes[i].dur_ms;
        uint32_t slot = end + (notes[i].note != SONG_NOTE_REST ? SONG_ARTICULATION_GAP_MS : 0U);
        if (elapsed < scaled_score_time(slot, speed)) {
            return elapsed < scaled_score_time(end, speed) && notes[i].note != SONG_NOTE_REST ? notes[i].freq_hz : 0U;
        }
        start = slot;
    }
    return 0U;
}

static void assert_dual_pitches(const SongDef *def, uint32_t elapsed, uint8_t speed)
{
    uint16_t expected[2] = {
        expected_track_pitch(def->notes, def->count, elapsed, speed),
        expected_track_pitch(def->accompaniment, def->accompaniment_count, elapsed, speed)
    };
    uint8_t remaining = (expected[0] != 0U) + (expected[1] != 0U);
    assert(Audio_GetActiveCount() == remaining);
    for (unsigned id = 0U; id < 128U; ++id) {
        if (Audio_IsVoiceActive((VoiceId)id)) {
            uint16_t hz = Audio_GetVoiceFreq((VoiceId)id);
            bool found = false;
            for (unsigned t = 0U; t < 2U; ++t) {
                if (expected[t] == hz) { expected[t] = 0U; found = true; break; }
            }
            assert(found);
        }
    }
}

static void test_song_accompaniment(void)
{
    Audio_Init();
    assert(AUDIO_VOICE_MAX == 2 && AUDIO_FREQ_MIN_HZ == 65U);
    VoiceId low = AUDIO_VOICE_INVALID;
    assert(Audio_NoteOnPitch(64U, &low) != AUDIO_OK);
    assert(Audio_NoteOnPitch(65U, &low) == AUDIO_OK);
    assert(Audio_GetVoiceFreq(low) == 65U);
    Audio_NoteOff(low);
    const uint8_t dual_indices[] = {3U, 4U, 6U};
    for (unsigned song = 0U; song < sizeof(dual_indices); ++song) {
        uint8_t index = dual_indices[song];
        const SongDef *def = Song_GetBuiltin(index);
        assert(def->accompaniment && def->accompaniment_count ==
               (index == 3U ? 193U : index == 4U ? 146U : 512U));
        uint32_t base = index == 3U ? 122250U : index == 4U ? 93103U : 118442U;
        if (index == 6U) {
            assert(strcmp(def->name, "LuXiaoYu") == 0 && def->count == 197U);
            assert(!def->loop && def->timbre == TIMBRE_PIANO + 1U && !def->sample);
            assert(def->notes[0].note == SONG_NOTE_REST);
            assert(def->accompaniment[0].freq_hz == 131U); /* C3 opening bass. */
            assert(def->notes[def->count - 1U].freq_hz == 1047U); /* 8va C6 coda. */
            assert(def->accompaniment[def->accompaniment_count - 1U].freq_hz == 262U);
        }
        assert(track_score_total(def->notes, def->count) == base);
        assert(track_score_total(def->accompaniment, def->accompaniment_count) == base);
        for (uint16_t n = 0U; n < def->accompaniment_count; ++n) {
            const SongNote *note = &def->accompaniment[n];
            assert(note->dur_ms > 0U);
            assert(note->note == SONG_NOTE_REST || (note->freq_hz >= 65U && note->freq_hz <= 4000U));
        }
        for (uint8_t speed = 0U; speed < 3U; ++speed) {
            Audio_SetTimbre(TIMBRE_GUITAR);
            Song_SetSpeed(speed);
            uint32_t begin = 0xFFFFFF00U;
            mock_tick = begin;
            assert(Song_Play(def));
            assert(Song_GetTotalMs() == scaled_score_time(base, speed));
            uint32_t total = Song_GetTotalMs();
            for (uint32_t ms = 0U; ms < total; ms += 1U + (ms * 17U + 23U) % 73U) {
                mock_tick = begin + ms;
                Audio_Envelope_Tick();
                Song_Tick();
                assert(Song_GetElapsedMs() == ms && Song_GetState() == SONG_PLAYING);
                assert_dual_pitches(def, ms, speed);
            }
            mock_tick = begin + total;
            Song_Tick();
            assert(Song_GetState() == SONG_IDLE && Song_GetElapsedMs() == total);
            assert(Audio_GetActiveCount() == 0U && Audio_GetTimbre() == TIMBRE_GUITAR);
        }
        Song_SetSpeed(1U);
        mock_tick = 100U;
        assert(Song_Play(def));
        mock_tick += 12345U;
        Song_Tick();
        assert_dual_pitches(def, 12345U, 1U);
        Song_Pause();
        uint32_t paused = Song_GetElapsedMs();
        mock_tick += 7000U;
        Song_Tick();
        assert(Song_GetElapsedMs() == paused && Audio_GetActiveCount() == 0U);
        Song_Resume();
        assert_dual_pitches(def, paused, 1U);
        Song_SetSpeed(0U);
        assert(Song_GetElapsedMs() == paused * 2U);
        assert_dual_pitches(def, paused * 2U, 0U);
        Song_Pause();
        Song_SetSpeed(2U);
        assert(Audio_GetActiveCount() == 0U && Song_GetState() == SONG_PAUSED);
        Song_Resume();
        assert_dual_pitches(def, Song_GetElapsedMs(), 2U);
        Song_Stop();
        assert(Audio_GetActiveCount() == 0U && Audio_GetTimbre() == TIMBRE_GUITAR);
    }
    static const SongNote lead[] = {{NOTE_DO, OCT_MID, 1000U, 440U}};
    static const SongNote bass[] = {{NOTE_DO, OCT_MID, 1000U, 131U}};
    static const SongDef pair = {"Pair", lead, 1U, false, TIMBRE_PIANO + 1U, bass, 1U, NULL};
    Song_SetSpeed(1U);
    mock_tick = 0U;
    assert(Song_Play(&pair) && Audio_GetActiveCount() == 2U);
    VoiceId key = Audio_NoteOn(NOTE_MI);
    assert(key >= 0 && Audio_GetActiveCount() == 2U);
    mock_tick = 10U;
    Song_Tick();
    assert(Audio_IsVoiceActive(key));
    bool lead_survived = false;
    for (unsigned id = 0U; id < 128U; ++id) {
        lead_survived |= Audio_IsVoiceActive((VoiceId)id) && Audio_GetVoiceFreq((VoiceId)id) == 440U;
    }
    assert(lead_survived); /* Keyboard steals the lower-priority bass, not the melody. */
    Song_Pause();
    assert(Audio_IsVoiceActive(key) && Audio_GetActiveCount() == 1U);
    Song_Resume();
    Song_Stop();
    assert(Audio_IsVoiceActive(key) && Audio_GetActiveCount() == 1U);
    Audio_NoteOff(key);
    SongDef loop = pair;
    loop.loop = true;
    assert(Song_Play(&loop));
    mock_tick += 2575U;
    Song_Tick();
    assert(Song_GetState() == SONG_PLAYING && Song_GetElapsedMs() == 515U);
    assert(Audio_GetActiveCount() == 2U);
    Song_Stop();
    SongDef invalid = pair;
    invalid.accompaniment = NULL;
    assert(!Song_Play(&invalid));
    puts("accompaniment: two clocks share grid, three speeds/wrap/delays, pause/seek, no stuck notes, keyboard priority and 65Hz bounds ok");
}

static void test_c4_sample(void)
{
    Audio_Init();
    Song_Init();
    assert(record_init(NULL));
    const SongDef *c4 = Song_GetBuiltin(5U);
    assert(c4 != NULL && strcmp(c4->name, "C4") == 0 && c4->sample != NULL);
    assert(!Song_Play(c4)); /* Missing asset must fail, not play random flash. */

    FILE *asset = fopen("assets/c4.pcm.bin", "rb");
    assert(asset != NULL);
    uint8_t block[4096];
    uint32_t address = RECORD_ASSET_BASE;
    size_t length;
    while ((length = fread(block, 1, sizeof(block), asset)) != 0U) {
        assert(record_test_asset_write(address, block, (uint16_t)length));
        address += (uint32_t)length;
    }
    assert(!ferror(asset));
    fclose(asset);
    assert(address - RECORD_ASSET_BASE == c4->sample->samples + 32U);
    assert(!record_asset_read(RECORD_ASSET_BASE - 1U, block, 1U));
    assert(!record_asset_read(RECORD_ASSET_END - 1U, block, 2U));
    assert(record_asset_read(RECORD_ASSET_BASE, block, 32U));
    block[0] ^= 1U;
    assert(record_test_asset_write(RECORD_ASSET_BASE, block, 32U));
    assert(!Song_Play(c4)); /* Header validation. */
    block[0] ^= 1U;
    assert(record_test_asset_write(RECORD_ASSET_BASE, block, 32U));

    /* Advance past the recording's initial silence, then check real PWM samples. */
    assert(Song_PlayGain(c4, 500U));
    assert(Song_GetSpeed() == 1U && Song_GetTotalMs() == 47778U);
    for (unsigned page = 0U; page < 16U; ++page) {
        for (unsigned i = 0U; i < 2048U; ++i) { AudioPwm_SampleIRQ(); }
        Song_Tick();
    }
    assert(record_asset_read(RECORD_ASSET_BASE + 32U + 16384U, block, 512U));
    unsigned nonzero = 0U;
    for (unsigned i = 0U; i < 512U; ++i) {
        AudioPwm_SampleIRQ();
        assert(mock_tim2.CCR1 == (uint32_t)(128 + ((int32_t)block[i] - 128) * 558 / 500));
        nonzero += block[i] != 128U;
        AudioPwm_SampleIRQ();
        assert(mock_tim2.CCR1 == (uint32_t)(128 + ((int32_t)block[i] - 128) * 558 / 500));
    }
    assert(nonzero != 0U);
    Song_Pause();
    uint32_t position = AudioPwm_PcmPosition();
    for (unsigned i = 0U; i < 4000U; ++i) { AudioPwm_SampleIRQ(); }
    mock_tick += 5000U;
    Song_Tick();
    assert(Song_GetState() == SONG_PAUSED && AudioPwm_PcmPosition() == position);
    Song_Resume();
    Song_SetSpeed(2U);
    assert(Song_GetSpeed() == 2U && AudioPwm_PcmPosition() == position);
    AudioPwm_SampleIRQ(); AudioPwm_SampleIRQ();
    assert(AudioPwm_PcmPosition() == position + 2U);
    Song_Stop();
    assert(!AudioPwm_PcmActive());
    Song_SetSpeed(1U);

    Audio_SetVolume(VOL_1);
    assert(Song_Play(c4));
    for (unsigned page = 0U; page < 16U; ++page) {
        for (unsigned i = 0U; i < 2048U; ++i) { AudioPwm_SampleIRQ(); }
        Song_Tick();
    }
    assert(record_asset_read(RECORD_ASSET_BASE + 32U + 16384U, block, 512U));
    for (unsigned i = 0U; i < 512U; ++i) {
        if (i == 256U) { Audio_SetVolume(VOL_5); Sample_Service(); }
        uint16_t gain = i < 256U ? AUDIO_DUTY_PERMILLE_1 : AUDIO_DUTY_PERMILLE_5;
        gain = (uint16_t)((gain * 1116UL + 500U) / 1000U);
        uint32_t expected = (uint32_t)(128 + ((int32_t)block[i] - 128) * gain / 500);
        AudioPwm_SampleIRQ();
        assert(mock_tim2.CCR1 == expected);
        AudioPwm_SampleIRQ();
        assert(mock_tim2.CCR1 == expected);
    }
    Song_Stop();

    /* At the fallback 1.125 MHz bus rate, even full redraws must refill audio. */
    assert(Song_Play(c4));
    sample_spi_hz = 1125000U;
    tft_fill_screen(0x1234U);
    tft_fill_gradient_rect(0, 0, 128, 160, 0x0000U, 0xFFFFU, true);
    sample_spi_hz = 0U;
    assert(AudioPwm_PcmPosition() > 2048U && AudioPwm_PcmUnderruns() == 0U);
    while (Song_GetState() == SONG_PLAYING) {
        for (unsigned i = 0U; i < 2048U; ++i) { AudioPwm_SampleIRQ(); }
        mock_tick += 64U;
        Song_Tick();
    }
    assert(!Sample_Failed() && AudioPwm_PcmUnderruns() == 0U);
    assert(AudioPwm_PcmPosition() == c4->sample->samples);
    assert(Song_GetElapsedMs() == 47778U && !AudioPwm_PcmActive());

    ui_page_init();
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    nav_main_to(5U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_LIBRARY);
    for (unsigned i = 0U; i < 5U; ++i) { ui_page_handle_event(BSP_ENCODER_EVENT_CW); }
    assert(ui_page_get_state()->library_sel == 5U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_PLAYBACK && Song_GetCurrent() == c4);
    assert(Song_GetState() == SONG_PLAYING);
    for (unsigned i = 0U; i < 1600U; ++i) { AudioPwm_SampleIRQ(); }
    Song_Tick();
    position = AudioPwm_PcmPosition();
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    assert(Song_GetSpeed() == 2U && Song_GetState() == SONG_PLAYING);
    assert(AudioPwm_PcmPosition() == position && Song_GetTotalMs() == 23889U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(Song_GetState() == SONG_PAUSED);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(Song_GetState() == SONG_PLAYING);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    assert(ui_page_current() == UI_PAGE_LIBRARY && !AudioPwm_PcmActive());
    Song_SetSpeed(1U);

    for (uint8_t speed = 0U; speed < 3U; ++speed) {
        Song_SetSpeed(speed);
        assert(Song_Play(c4));
        uint32_t expected_total = speed == 0U ? 95556U : speed == 1U ? 47778U : 23889U;
        assert(Song_GetTotalMs() == expected_total && Song_GetSpeed() == speed);
        for (unsigned i = 0U; i < 1600U; ++i) {
            AudioPwm_SampleIRQ();
            if (i % 128U == 0U) { Song_Tick(); }
        }
        assert(AudioPwm_PcmPosition() == 1600U / (4U >> speed));
        position = AudioPwm_PcmPosition();
        Song_Pause();
        uint8_t next_speed = (uint8_t)((speed + 1U) % 3U);
        Song_SetSpeed(next_speed);
        for (unsigned i = 0U; i < 1000U; ++i) { AudioPwm_SampleIRQ(); }
        assert(Song_GetState() == SONG_PAUSED && AudioPwm_PcmPosition() == position);
        Song_Resume();
        for (unsigned i = 0U; i < 400U; ++i) { AudioPwm_SampleIRQ(); }
        assert(AudioPwm_PcmPosition() == position + 400U / (4U >> next_speed));
        Song_SetSpeed(speed);
        while (Song_GetState() == SONG_PLAYING) {
            for (unsigned i = 0U; i < 512U; ++i) { AudioPwm_SampleIRQ(); }
            Song_Tick();
        }
        assert(!Sample_Failed() && AudioPwm_PcmUnderruns() == 0U);
        assert(Song_GetElapsedMs() == expected_total);
    }
    Song_SetSpeed(2U);
    assert(Song_Play(c4));
    sample_spi_hz = 1125000U;
    tft_fill_screen(0x3456U);
    tft_fill_gradient_rect(0, 0, 128, 160, 0U, 0xFFFFU, true);
    sample_spi_hz = 0U;
    assert(AudioPwm_PcmPosition() > 2048U && AudioPwm_PcmUnderruns() == 0U);
    Song_Stop();
    Song_SetSpeed(1U);

    /* A stalled producer recovers without advancing past unavailable samples. */
    assert(Song_Play(c4));
    for (unsigned i = 0U; i < 6000U; ++i) { AudioPwm_SampleIRQ(); }
    assert(AudioPwm_PcmPosition() == 2048U && AudioPwm_PcmUnderruns() != 0U);
    Song_Tick();
    AudioPwm_SampleIRQ(); AudioPwm_SampleIRQ();
    assert(AudioPwm_PcmPosition() == 2049U);
    Song_Stop();

    /* Corrupted payload is rejected by the streaming checksum. */
    uint32_t last = RECORD_ASSET_BASE + 32U + c4->sample->samples - 1U;
    assert(record_asset_read(last, block, 1U));
    block[0] ^= 1U;
    assert(record_test_asset_write(last, block, 1U));
    assert(Song_Play(c4));
    while (Song_GetState() == SONG_PLAYING) {
        for (unsigned i = 0U; i < 2048U; ++i) { AudioPwm_SampleIRQ(); }
        Song_Tick();
    }
    assert(Sample_Failed() && !AudioPwm_PcmActive());
    block[0] ^= 1U;
    assert(record_test_asset_write(last, block, 1U));
    puts("C4 PCM: boosted PWM, three full rates, live/paused switching, ring wrap, fast playback slow-SPI redraw, recovery and checksums ok");
}

static void test_live_speed_ui(void)
{
    for (uint8_t index = 3U; index <= 6U; ++index) {
        Audio_Init();
        Song_Init();
        record_init(NULL);
        ui_page_init();
        ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
        nav_main_to(5U);
        ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
        mock_tick += 200U;
        ui_page_tick(mock_tick);
        mock_tick += 100U;
        ui_page_tick(mock_tick); /* Finish menu feedback before checking song-only silence. */
        for (uint8_t n = 0U; n < index; ++n) { ui_page_handle_event(BSP_ENCODER_EVENT_CW); }
        ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
        assert(Song_GetState() == SONG_PLAYING && Song_GetSpeed() == 1U);
        if (index == 5U) {
            for (unsigned n = 0U; n < 40000U; ++n) {
                AudioPwm_SampleIRQ();
                if (n % 128U == 0U) { Song_Tick(); }
            }
        } else { mock_tick += 10000U; }
        Song_Tick();
        uint32_t elapsed = Song_GetElapsedMs(), total = Song_GetTotalMs();
        uint32_t position = AudioPwm_PcmPosition();
        uint8_t progress = Song_GetProgress();
        assert(progress > 0U);
        ui_page_handle_event(BSP_ENCODER_EVENT_CW); /* 1x -> 2x, no restart. */
        assert(Song_GetState() == SONG_PLAYING && Song_GetSpeed() == 2U);
        assert(Song_GetTotalMs() == total / 2U);
        assert(Song_GetElapsedMs() >= elapsed / 2U - 1U);
        assert(Song_GetElapsedMs() <= elapsed / 2U + 1U);
        assert(Song_GetProgress() + 1U >= progress);
        if (index == 5U) { assert(AudioPwm_PcmPosition() == position); }
        ui_page_handle_event(BSP_ENCODER_EVENT_CW); /* 2x -> 0.5x. */
        assert(Song_GetSpeed() == 0U && Song_GetTotalMs() == total * 2U);
        assert(Song_GetElapsedMs() >= elapsed * 2U - 3U);
        assert(Song_GetElapsedMs() <= elapsed * 2U + 1U);
        ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
        assert(Song_GetState() == SONG_PAUSED);
        ui_page_handle_event(BSP_ENCODER_EVENT_CCW);
        assert(Song_GetSpeed() == 2U && Song_GetState() == SONG_PAUSED);
        if (index == 5U) { assert(AudioPwm_PcmPosition() == position); }
        else { assert(Audio_GetActiveCount() == 0U); }
        ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
        assert(Song_GetState() == SONG_PLAYING);
        ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
        assert(Song_GetState() == SONG_IDLE && !AudioPwm_PcmActive());
    }
    puts("live speed UI: QingHuaCi/FaRuXue/C4/LuXiaoYu keep progress across CW/CCW, pause/resume and exit ok");
}

static void test_faruxue_visibility(void)
{
    Audio_Init();
    Song_Init();
    assert(record_init(NULL));
    uint8_t settings[2] = {0xA6U, 0x46U}; /* Actual device mask. */
    assert(record_setting_save(settings, sizeof(settings)));
    ui_page_init();
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    nav_main_to(5U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    mock_tick += 300U;
    ui_page_tick(mock_tick);
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    capture_framebuffer("build/faruxue_visibility_46.ppm");
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(Song_GetCurrent() == Song_GetBuiltin(4U));
    assert(strcmp(Song_GetCurrent()->name, "FaRuXue") == 0);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    settings[1] = 0U;
    assert(record_setting_save(settings, sizeof(settings)));
    puts("device mask 0x46: third visible item opens FaRuXue");
}

static void ds_room_advance(unsigned ms)
{
    for (unsigned i = 0U; i < ms; ++i) {
        refresh_measure_begin();
        ui_page_tick(++mock_tick);
        assert(pixel_writes <= 2U * ST7735_WIDTH);
    }
}

static void test_ds_room(void)
{
    Audio_Init();
    Song_Init();
    ui_page_init();
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    nav_main_to(9U);
    ui_page_handle_event(BSP_ENCODER_EVENT_SHORT);
    assert(ui_page_current() == UI_PAGE_DS_ROOM);
    assert(panel_madctl == (ST7735_MADCTL_VALUE ^ 0x08U));
    ds_room_advance(100U);
    capture_framebuffer("build/ds_room_idle.ppm");

    uint16_t open_eyes = framebuffer[64][54];
    uint16_t idle_mouth = framebuffer[71][63];
    uint16_t idle[7];
    for (unsigned i = 0U; i < 7U; ++i) {
        idle[i] = framebuffer[115][18U + i * 15U];
    }
    for (unsigned note = 0U; note < 7U; ++note) {
        mock_note_mask = (uint8_t)(1U << note);
        ds_room_advance(45U);
        for (unsigned i = 0U; i < 7U; ++i) {
            assert((framebuffer[115][18U + i * 15U] != idle[i]) == (i == note));
        }
        if (note == 6U) {
            capture_framebuffer("build/ds_room_playing.ppm");
            ds_room_advance(100U);
            assert(framebuffer[71][63] == idle_mouth); /* Preserve the small closed smile. */
            assert(framebuffer[64][54] != open_eyes);
            capture_framebuffer("build/ds_room_happy.ppm");
        }
        mock_note_mask = 0U;
        ds_room_advance(45U);
        for (unsigned i = 0U; i < 7U; ++i) {
            assert(framebuffer[115][18U + i * 15U] == idle[i]);
        }
    }

    mock_note_mask = 0x09U;
    ds_room_advance(45U);
    assert(framebuffer[115][18] != idle[0] && framebuffer[115][63] != idle[3]);
    mock_note_mask = 0U;
    ds_room_advance(850U);
    assert(framebuffer[115][18] == idle[0] && framebuffer[115][63] == idle[3]);
    ds_room_advance(3710U - 100U - 7U * 90U - 100U - 45U - 850U);
    assert(framebuffer[64][54] != open_eyes); /* Idle blink window. */
    ds_room_advance(290U);
    assert(framebuffer[65][54] == open_eyes); /* Eyes reopen after a 1 px bob. */
    ui_page_handle_func();
    ds_room_advance(100U);
    assert(framebuffer[115][18] == idle[0]); /* Missing dance data leaves the piano usable. */
    static uint8_t dance_data[RECORD_DS_ASSET_END - RECORD_DS_ASSET_BASE];
    FILE *dance_asset = fopen("assets/ds_room_dance.bin", "rb");
    assert(dance_asset != NULL);
    size_t dance_size = fread(dance_data, 1, sizeof(dance_data), dance_asset);
    assert(dance_size > 12U && feof(dance_asset));
    fclose(dance_asset);
    assert(!record_test_asset_write(RECORD_DS_ASSET_END - 1U, dance_data, 2U));
    assert(record_test_asset_write(RECORD_DS_ASSET_BASE, dance_data, (uint16_t)dance_size));
    uint8_t corrupted = dance_data[12] ^ 1U;
    assert(record_test_asset_write(RECORD_DS_ASSET_BASE + 12U, &corrupted, 1U));
    ui_page_handle_func();
    ds_room_advance(150U);
    assert(framebuffer[115][18] == idle[0]); /* Wrong checksum is rejected. */
    assert(record_test_asset_write(RECORD_DS_ASSET_BASE + 12U, dance_data + 12, 1U));
    ui_page_handle_func();
    ds_room_advance(155U);
    assert(framebuffer[115][18] != idle[0]); /* Valid data starts the dance. */
    capture_framebuffer("build/ds_room_dance_step.ppm");
    assert(ds_room_test_completed_step() == 0);
    uint32_t dance_start = refresh_frame_hash();
    uint16_t room_edge = framebuffer[44][10];
    static uint16_t delta_frame[ST7735_HEIGHT][ST7735_WIDTH];
    uint32_t total_pixels = 0U, max_pixels = 0U, frame_pixels = 0U;
    unsigned completed = 0U;
    int last_step = 0;
    for (unsigned tick = 0U; tick < 2600U && completed < 26U; ++tick) {
        ds_room_advance(1U);
        frame_pixels += pixel_writes;
        int step = ds_room_test_completed_step();
        if (step < 0 || step == last_step) { continue; }
        assert(step == (last_step + 1) % 26);
        last_step = step;
        ++completed;
        total_pixels += frame_pixels;
        if (frame_pixels > max_pixels) { max_pixels = frame_pixels; }
        frame_pixels = 0U;
        assert(framebuffer[44][10] == room_edge && row_writes[14] == 0U);
        memcpy(delta_frame, framebuffer, sizeof(delta_frame));
        ds_room_test_redraw_dance();
        assert(memcmp(delta_frame, framebuffer, sizeof(delta_frame)) == 0);
        if (step == 8) { capture_framebuffer("build/ds_room_dance_turn.ppm"); }
    }
    assert(completed == 26U && refresh_frame_hash() == dance_start);
    assert(max_pixels < 7000U && total_pixels < 26U * 4500U);
    printf("DS dance: 26 delta frames match full redraw, mean %lu, max %lu pixels/frame\n",
           (unsigned long)(total_pixels / completed), (unsigned long)max_pixels);

    /* Missing lion data leaves dance 1 working. Then provision simulated W25Q64. */
    ui_page_handle_event(BSP_ENCODER_EVENT_CCW);
    ds_room_advance(20U);
    assert(ds_room_test_completed_step() == 0);
    FILE *lion = fopen("assets/ds_room_lion.bin", "rb");
    assert(lion != NULL);
    uint8_t buf[1024];
    uint32_t address = RECORD_LION_ASSET_BASE;
    size_t bytes;
    while ((bytes = fread(buf, 1, sizeof(buf), lion)) != 0U) {
        assert(record_test_asset_write(address, buf, (uint16_t)bytes));
        address += (uint32_t)bytes;
    }
    assert(feof(lion) && address > RECORD_LION_ASSET_BASE);
    fclose(lion);
    assert(!record_test_asset_write(RECORD_LION_ASSET_END - 1U, buf, 2U));
    assert(!record_test_asset_write(RECORD_DS_ASSET_END - 1U, buf, 2U));

    ui_page_handle_event(BSP_ENCODER_EVENT_CW);
    ds_room_advance(55U);
    assert(ds_room_test_completed_step() == 0);
    assert(refresh_frame_hash() != dance_start);
    uint32_t lion_start = refresh_frame_hash();
    assert(panel_madctl == (ST7735_MADCTL_VALUE ^ 0x08U));
    completed = 0U;
    last_step = 0;
    for (unsigned tick = 0U; tick < 9800U && completed < 120U; ++tick) {
        ds_room_advance(1U);
        int step = ds_room_test_completed_step();
        if (step < 0 || step == last_step) { continue; }
        assert(step == (last_step + 1) % 120);
        last_step = step;
        ++completed;
        assert(framebuffer[44][10] == room_edge && row_writes[14] == 0U);
        memcpy(delta_frame, framebuffer, sizeof(delta_frame));
        ds_room_test_redraw_dance();
        assert(memcmp(delta_frame, framebuffer, sizeof(delta_frame)) == 0);
        if (step == 44) {
            unsigned blue = 0U, warm = 0U;
            for (unsigned y = 50U; y < 120U; ++y) {
                for (unsigned x = 44U; x < 82U; ++x) {
                    uint16_t color = framebuffer[y][x];
                    unsigned red = color >> 11, b = color & 31U;
                    if (b >= red + 4U && red < 18U) { ++blue; }
                    if (red >= b + 5U && red > 20U) { ++warm; }
                }
            }
            assert(blue > 100U && warm > 100U);
            capture_framebuffer("build/ds_room_lion.ppm");
        }
    }
    assert(completed == 120U && refresh_frame_hash() == lion_start);
    ui_page_handle_event(BSP_ENCODER_EVENT_CCW);
    ds_room_advance(55U);
    assert(ds_room_test_completed_step() == 0 && refresh_frame_hash() == dance_start);
    puts("DS lion: 120 delta frames, blue hair/warm lion, CW/CCW switching and shared room ok");
    ui_page_handle_func();
    ds_room_advance(100U);
    assert(framebuffer[115][18] == idle[0]);
    mock_note_mask = 0x40U;
    ds_room_advance(45U);
    assert(framebuffer[115][108] != idle[6]);
    mock_note_mask = 0U;
    ui_page_handle_func();
    ds_room_advance(100U);
    ui_page_handle_event(BSP_ENCODER_EVENT_LONG);
    assert(ui_page_current() == UI_PAGE_MAIN && ui_page_get_state()->main_sel == 9U);
    assert(panel_madctl == ST7735_MADCTL_VALUE);
    puts("DS room: RGB order, keys, blink/smile, W25Q64 asset validation, FUNC dance, menu return ok");
}

int main(void)
{
    SPI_HandleTypeDef hspi = {0};
    mock_tick = 0;
    assert(tft_init(&hspi));

    AudioTim_Init();
    test_audio_contract();
    test_boot_logo();
    test_encoder();
    test_runtime_regressions();
    test_audio_loudness();
    test_ui_pages();
    test_overdub_flow();
    test_rhythm_chart_data();
    test_rhythm_difficulty();
    test_rhythm_game();
    test_melody_generation();
    test_melody_playback();
    test_melody_ui();
    test_qinghuaci();
    test_faruxue();
    test_song_accompaniment();
    test_c4_sample();
    test_live_speed_ui();
    test_faruxue_visibility();
    test_compose_strip();
    test_ds_room();
    test_refresh_transactions();

    uint32_t hash = 2166136261U;
    for (uint16_t y = 0; y < ST7735_HEIGHT; ++y) {
        for (uint16_t x = 0; x < ST7735_WIDTH; ++x) {
            hash ^= framebuffer[y][x];
            hash *= 16777619U;
        }
    }
    printf("host test ok, final fnv1a=%08lX\n", (unsigned long)hash);
    return 0;
}
