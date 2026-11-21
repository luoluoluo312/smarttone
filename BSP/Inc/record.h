#ifndef RECORD_H
#define RECORD_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f1xx_hal.h"

#define RECORD_MAX_EVENTS      600U
#define RECORD_MAX_TRACKS      10U

/* SmartTone_软件共识清单_V2.0 标准事件，必须保持 12 字节。 */
typedef struct {
    uint8_t note;       /* 0~6，NOTE_DO~NOTE_SI */
    uint8_t octave;     /* 按下时的音区，Audio_GetOctave() */
    uint16_t freq_hz;   /* 按下时的实际频率，Audio_GetVoiceFreq(voiceId) */
    uint32_t start_ms;  /* 相对录音起点的毫秒数 */
    uint16_t dur_ms;    /* 音符持续毫秒数 */
} NoteEvent;

typedef char note_event_size_check[(sizeof(NoteEvent) == 12U) ? 1 : -1];

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(NoteEvent) == 12U, "NoteEvent must be exactly 12 bytes");
#endif

typedef struct {
    uint16_t track_id;      /* 1..10，用于曲库显示 REC_xx */
    uint16_t event_count;
    uint32_t duration_ms;
} record_info_t;

typedef enum {
    RECORD_RESULT_OK = 0,
    RECORD_RESULT_EMPTY,
    RECORD_RESULT_FULL,
    RECORD_RESULT_STORAGE,
    RECORD_RESULT_NOT_READY
} record_result_t;

bool record_init(SPI_HandleTypeDef *hspi);

bool record_start(uint32_t now_ms);
void record_note_press(uint8_t note, int8_t voice_id, uint8_t octave,
                       uint16_t freq_hz, uint32_t now_ms);
void record_note_release(uint8_t note, uint32_t now_ms);
uint16_t record_event_count(void);
uint32_t record_elapsed_ms(uint32_t now_ms);
bool record_is_recording(void);
bool record_is_full(void);
record_result_t record_stop_and_save(uint32_t now_ms);
void record_cancel(void);

uint8_t record_track_count(void);
bool record_get_info(uint8_t index, record_info_t *info);
bool record_delete(uint8_t index);
/* 通用小设置存取：写在 W25Q64 末尾保留扇区，与录音分区完全隔离。
 * 用于保存内置曲目是否被删除之类的少量状态。 */
bool record_setting_load(uint8_t *buf, uint16_t len);
bool record_setting_save(const uint8_t *buf, uint16_t len);

/* PCM asset partition [1 MiB, 2 MiB); recording and settings stay separate. */
#define RECORD_ASSET_BASE  0x100000UL
#define RECORD_ASSET_END   0x200000UL
#define RECORD_DS_ASSET_BASE 0x200000UL
#define RECORD_DS_ASSET_END  0x208000UL
#define RECORD_LION_ASSET_BASE 0x208000UL
#define RECORD_LION_ASSET_END  0x308000UL
bool record_asset_read(uint32_t address, uint8_t *buf, uint16_t len);
#ifdef RECORD_HOST_TEST
bool record_test_asset_write(uint32_t address, const uint8_t *buf, uint16_t len);
#endif

/* ---- 叠录：回放原曲的同时再录一层，停止时合并成一条 ---- */
bool record_overdub_start(uint8_t index, uint32_t now_ms);
record_result_t record_overdub_stop(uint32_t now_ms);
bool record_overdub_active(void);

bool record_playback_start(uint8_t index, uint32_t now_ms);
void record_playback_tick(uint32_t now_ms);
void record_playback_set_paused(bool paused, uint32_t now_ms);
void record_playback_stop(void);
bool record_playback_active(void);

/* 回放速度：0 = 0.5 倍（慢），1 = 1.0 倍（正常），2 = 2.0 倍（快）。
 * 只缩放时间轴，音高不变。任意时刻调用都安全，下次 tick 生效。 */
void record_playback_set_speed(uint8_t level);
uint8_t record_playback_get_speed(void);

#endif
