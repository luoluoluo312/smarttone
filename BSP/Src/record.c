#include "record.h"

#include "audio.h"
#include "main.h"

#include <string.h>

#define RECORD_STORAGE_BASE   0x001000UL   /* 4 KB 对齐，与其它 W25Q64 数据分区隔离 */
#define RECORD_SLOT_SIZE      8192U
#define RECORD_HEADER_SIZE    32U
#define RECORD_MAGIC          0x534D5452UL
#define RECORD_VERSION        2U
#define RECORD_FLAG_VALID     0x01U
#define RECORD_SETTING_ADDR   0x7F0000UL
#define RECORD_SECTOR_SIZE    4096U
_Static_assert(RECORD_HEADER_SIZE + RECORD_MAX_EVENTS * sizeof(NoteEvent) <= RECORD_SLOT_SIZE,
               "Recording does not fit its flash slot");

#define W25_CMD_WRITE_ENABLE  0x06U
#define W25_CMD_READ_STATUS   0x05U
#define W25_CMD_READ_DATA     0x03U
#define W25_CMD_PAGE_PROGRAM  0x02U
#define W25_CMD_SECTOR_ERASE  0x20U
#define W25_CMD_JEDEC_ID      0x9FU

#define W25_CS_LOW()  HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_RESET)
#define W25_CS_HIGH() HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_SET)

static SPI_HandleTypeDef *s_spi;
static bool s_ready;
static bool s_recording;
static bool s_event_full;
static uint32_t s_start_ms;
static uint16_t s_event_count;
static NoteEvent s_events[RECORD_MAX_EVENTS];

static uint8_t s_track_count;
static uint8_t s_slot[RECORD_MAX_TRACKS];
static record_info_t s_info[RECORD_MAX_TRACKS];

/* ---------------- 叠录（overdub）：回放原曲的同时再录一层 ----------
 * 原曲留在 s_events 里供回放使用，新弹的音符进 s_od_events，
 * 停止时合并、按时间排序后写回原槽位。合并前先把原曲备份到空槽。 */
#define RECORD_OD_MAX   200U
static bool     s_overdub;
static uint8_t  s_od_src;                 /* 原曲在 s_slot/s_info 里的下标 */
static uint16_t s_od_n;
static NoteEvent s_od_events[RECORD_OD_MAX];
static bool s_pb_active;
static bool s_pb_paused;
static uint16_t s_pb_count;
static uint16_t s_pb_index;
static uint32_t s_pb_start;
static uint32_t s_pb_elapsed;
static uint32_t s_pb_duration;
static uint8_t s_pb_speed = 1U;     /* 0=0.5x 1=1.0x 2=2.0x */
static int8_t s_pb_voice[RECORD_MAX_EVENTS];

#ifdef RECORD_HOST_TEST
static uint8_t s_host_flash[8UL * 1024UL * 1024UL];
#endif

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint32_t event_checksum(const NoteEvent *events, uint16_t count)
{
    uint32_t hash = 2166136261UL;
    const uint8_t *raw = (const uint8_t *)events;
    uint32_t len = (uint32_t)count * sizeof(NoteEvent);
    for (uint32_t i = 0; i < len; ++i) {
        hash ^= raw[i];
        hash *= 16777619UL;
    }
    return hash;
}

static void build_header(uint8_t *hdr, uint16_t count, uint32_t duration,
                         uint16_t track_id, uint32_t checksum)
{
    memset(hdr, 0, RECORD_HEADER_SIZE);
    put_u32(&hdr[0], RECORD_MAGIC);
    hdr[4] = RECORD_VERSION;
    hdr[5] = RECORD_FLAG_VALID;
    put_u16(&hdr[6], count);
    put_u32(&hdr[8], duration);
    put_u16(&hdr[12], track_id);
    put_u32(&hdr[16], checksum);
}

static bool header_valid(const uint8_t *hdr, uint16_t *count,
                         uint32_t *duration, uint16_t *track_id)
{
    if (get_u32(&hdr[0]) != RECORD_MAGIC || hdr[4] != RECORD_VERSION ||
        (hdr[5] & RECORD_FLAG_VALID) == 0U) {
        return false;
    }
    uint16_t n = get_u16(&hdr[6]);
    if (n > RECORD_MAX_EVENTS) {
        return false;
    }
    *count = n;
    *duration = get_u32(&hdr[8]);
    *track_id = get_u16(&hdr[12]);
    return true;
}

/* ---------- W25Q64 存储层 ---------- */

#ifdef RECORD_HOST_TEST

static bool storage_read(uint32_t addr, uint8_t *buf, uint16_t len)
{
    if (addr + len > sizeof(s_host_flash)) return false;
    memcpy(buf, &s_host_flash[addr], len);
    return true;
}

static bool storage_write(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    if (addr + len > sizeof(s_host_flash)) return false;
    memcpy(&s_host_flash[addr], buf, len);
    return true;
}

static bool storage_erase(uint32_t addr, uint32_t size)
{
    if (addr + size > sizeof(s_host_flash)) return false;
    memset(&s_host_flash[addr], 0xFF, size);
    return true;
}

static bool storage_init(void)
{
    return true;
}

#else

static bool w25_wait_ready(void)
{
    uint8_t status = 0x01U;
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 1000U) {
        W25_CS_LOW();
        uint8_t cmd = W25_CMD_READ_STATUS;
        if (HAL_SPI_Transmit(s_spi, &cmd, 1, 100) != HAL_OK ||
            HAL_SPI_Receive(s_spi, &status, 1, 100) != HAL_OK) {
            W25_CS_HIGH();
            return false;
        }
        W25_CS_HIGH();
        if ((status & 0x01U) == 0U) {
            return true;
        }
    }
    return false;
}

static bool w25_write_enable(void)
{
    uint8_t cmd = W25_CMD_WRITE_ENABLE;
    W25_CS_LOW();
    bool ok = HAL_SPI_Transmit(s_spi, &cmd, 1, 100) == HAL_OK;
    W25_CS_HIGH();
    return ok;
}

static bool storage_read(uint32_t addr, uint8_t *buf, uint16_t len)
{
    uint8_t cmd[4] = {
        W25_CMD_READ_DATA,
        (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr
    };
    W25_CS_LOW();
    bool ok = HAL_SPI_Transmit(s_spi, cmd, 4, 100) == HAL_OK &&
              HAL_SPI_Receive(s_spi, buf, len, 1000) == HAL_OK;
    W25_CS_HIGH();
    return ok;
}

static bool storage_write_page(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    uint8_t cmd[4] = {
        W25_CMD_PAGE_PROGRAM,
        (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr
    };
    if (!w25_write_enable()) return false;
    W25_CS_LOW();
    bool ok = HAL_SPI_Transmit(s_spi, cmd, 4, 100) == HAL_OK &&
              HAL_SPI_Transmit(s_spi, (uint8_t *)buf, len, 1000) == HAL_OK;
    W25_CS_HIGH();
    return ok && w25_wait_ready();
}

static bool storage_write(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    while (len > 0U) {
        uint16_t room = (uint16_t)(256U - (addr & 0xFFU));
        uint16_t chunk = (len < room) ? len : room;
        if (!storage_write_page(addr, buf, chunk)) return false;
        addr += chunk;
        buf += chunk;
        len = (uint16_t)(len - chunk);
    }
    return true;
}

static bool storage_erase(uint32_t addr, uint32_t size)
{
    uint8_t cmd[4];
    uint32_t off;

    /* W25Q64 一个扇区只有 4 KB，而一个录音槽是 RECORD_SLOT_SIZE，
     * 所以要按扇区循环擦，否则后半槽会残留旧数据。 */
    for (off = 0U; off < size; off += RECORD_SECTOR_SIZE) {
        cmd[0] = W25_CMD_SECTOR_ERASE;
        cmd[1] = (uint8_t)((addr + off) >> 16);
        cmd[2] = (uint8_t)((addr + off) >> 8);
        cmd[3] = (uint8_t)(addr + off);
        if (!w25_write_enable()) return false;
        W25_CS_LOW();
        bool ok = HAL_SPI_Transmit(s_spi, cmd, 4, 100) == HAL_OK;
        W25_CS_HIGH();
        if (!(ok && w25_wait_ready())) return false;
    }
    return true;
}

static bool storage_init(void)
{
    uint8_t cmd = W25_CMD_JEDEC_ID;
    uint8_t id[3] = {0};
    W25_CS_LOW();
    bool ok = HAL_SPI_Transmit(s_spi, &cmd, 1, 100) == HAL_OK &&
              HAL_SPI_Receive(s_spi, id, 3, 100) == HAL_OK;
    W25_CS_HIGH();
    return ok && id[0] != 0x00U && id[0] != 0xFFU;
}

#endif

/* ---------- 录音与曲目管理 ---------- */

static bool asset_range_ok(uint32_t address, uint16_t len)
{
    if (address >= RECORD_LION_ASSET_BASE) {
        return address < RECORD_LION_ASSET_END && len <= RECORD_LION_ASSET_END - address;
    }
    if (address >= RECORD_DS_ASSET_BASE) {
        return address < RECORD_DS_ASSET_END && len <= RECORD_DS_ASSET_END - address;
    }
    return address >= RECORD_ASSET_BASE && address < RECORD_ASSET_END &&
           len <= RECORD_ASSET_END - address;
}

bool record_asset_read(uint32_t address, uint8_t *buf, uint16_t len)
{
    if (!s_ready || buf == NULL || !asset_range_ok(address, len)) {
        return false;
    }
    return storage_read(address, buf, len);
}

#ifdef RECORD_HOST_TEST
bool record_test_asset_write(uint32_t address, const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || !asset_range_ok(address, len)) { return false; }
    return storage_write(address, buf, len);
}
#endif

static uint32_t slot_address(uint8_t slot)
{
    return RECORD_STORAGE_BASE + (uint32_t)slot * RECORD_SLOT_SIZE;
}

static bool find_free_slot(uint8_t *slot)
{
    for (uint8_t i = 0; i < RECORD_MAX_TRACKS; ++i) {
        bool used = false;
        for (uint8_t j = 0; j < s_track_count; ++j) {
            if (s_slot[j] == i) {
                used = true;
                break;
            }
        }
        if (!used) {
            *slot = i;
            return true;
        }
    }
    return false;
}

static void scan_tracks(void)
{
    s_track_count = 0;
    for (uint8_t slot = 0; slot < RECORD_MAX_TRACKS; ++slot) {
        uint8_t hdr[RECORD_HEADER_SIZE];
        uint16_t count;
        uint32_t duration;
        uint16_t track_id;
        if (!storage_read(slot_address(slot), hdr, sizeof(hdr))) {
            continue;
        }
        if (!header_valid(hdr, &count, &duration, &track_id)) {
            continue;
        }
        s_slot[s_track_count] = slot;
        s_info[s_track_count].track_id = track_id;
        s_info[s_track_count].event_count = count;
        s_info[s_track_count].duration_ms = duration;
        ++s_track_count;
    }
}

bool record_init(SPI_HandleTypeDef *hspi)
{
#ifndef RECORD_HOST_TEST
    if (hspi == NULL) {
        s_ready = false;
        return false;
    }
#endif
    s_spi = hspi;
    s_recording = false;
    s_event_count = 0;
    s_event_full = false;
    s_pb_active = false;
    s_ready = storage_init();
    if (s_ready) {
        scan_tracks();
    }
    return s_ready;
}

bool record_start(uint32_t now_ms)
{
    uint8_t slot;
    if (!s_ready || s_recording || s_pb_active || s_track_count >= RECORD_MAX_TRACKS ||
        !find_free_slot(&slot)) {
        return false;
    }
    s_recording = true;
    s_event_full = false;
    s_event_count = 0;
    s_start_ms = now_ms;
    return true;
}

void record_note_press(uint8_t note, int8_t voice_id, uint8_t octave,
                       uint16_t freq_hz, uint32_t now_ms)
{
    if (!s_recording || note >= 7U || voice_id < 0) {
        return;
    }
    if (s_overdub) {
        uint16_t orig = s_info[s_od_src].event_count;
        if (orig > RECORD_MAX_EVENTS) {
            orig = RECORD_MAX_EVENTS;
        }
        /* 原文件事件 + 新层事件总上限仍为 600。 */
        if ((s_od_n >= RECORD_OD_MAX) ||
            ((uint32_t)orig + s_od_n >= RECORD_MAX_EVENTS)) {
            s_event_full = true;
            return;
        }
        NoteEvent *oe = &s_od_events[s_od_n];
        memset(oe, 0, sizeof(*oe));
        oe->note     = note;
        oe->octave   = octave;
        oe->freq_hz  = freq_hz;
        oe->start_ms = now_ms - s_start_ms;
        oe->dur_ms   = 0U;
        ++s_od_n;
        return;
    }
    if (s_event_count >= RECORD_MAX_EVENTS) {
        s_event_full = true;
        return;
    }
    NoteEvent *event = &s_events[s_event_count];
    memset(event, 0, sizeof(*event));
    event->note = note;
    event->octave = octave;
    event->freq_hz = freq_hz;
    event->start_ms = now_ms - s_start_ms;
    event->dur_ms = 0U;
    ++s_event_count;
    if (s_event_count >= RECORD_MAX_EVENTS) {
        s_event_full = true;
    }
}

void record_note_release(uint8_t note, uint32_t now_ms)
{
    if (!s_recording || note >= 7U) {
        return;
    }
    if (s_overdub) {
        for (uint16_t i = s_od_n; i > 0U; --i) {
            NoteEvent *oe = &s_od_events[i - 1U];
            if (oe->note == note && oe->dur_ms == 0U) {
                uint32_t sa = s_start_ms + oe->start_ms;
                uint32_t du = now_ms - sa;
                if (du == 0U) { du = 1U; }
                oe->dur_ms = (du > 0xFFFFUL) ? 0xFFFFU : (uint16_t)du;
                return;
            }
        }
        return;
    }
    for (uint16_t i = s_event_count; i > 0U; --i) {
        NoteEvent *event = &s_events[i - 1U];
        if (event->note == note && event->dur_ms == 0U) {
            uint32_t start_abs = s_start_ms + event->start_ms;
            uint32_t dur = now_ms - start_abs;
            if (dur == 0U) { dur = 1U; }
            event->dur_ms = (dur > 0xFFFFUL) ? 0xFFFFU : (uint16_t)dur;
            return;
        }
    }
}

uint16_t record_event_count(void)
{
    return s_event_count;
}

uint32_t record_elapsed_ms(uint32_t now_ms)
{
    return s_recording ? (now_ms - s_start_ms) : 0U;
}

bool record_is_recording(void)
{
    return s_recording;
}

bool record_is_full(void)
{
    return s_event_full;
}

static uint32_t recording_duration(void)
{
    uint32_t max_end = 0U;
    for (uint16_t i = 0; i < s_event_count; ++i) {
        uint32_t end = s_events[i].start_ms + s_events[i].dur_ms;
        if (end > max_end) {
            max_end = end;
        }
    }
    return max_end;
}

record_result_t record_stop_and_save(uint32_t now_ms)
{
    if (!s_recording) {
        return RECORD_RESULT_EMPTY;
    }
    s_recording = false;
    if (s_event_count == 0U) {
        return RECORD_RESULT_EMPTY;
    }

    /* 停止时仍按下的音符，以停止时刻补全持续时间。 */
    for (uint16_t i = 0; i < s_event_count; ++i) {
        NoteEvent *event = &s_events[i];
        if (event->dur_ms == 0U) {
            uint32_t start_abs = s_start_ms + event->start_ms;
            uint32_t dur = now_ms - start_abs;
            if (dur == 0U) { dur = 1U; }
            event->dur_ms = (dur > 0xFFFFUL) ? 0xFFFFU : (uint16_t)dur;
        }
    }

    uint8_t slot;
    if (!s_ready || s_track_count >= RECORD_MAX_TRACKS || !find_free_slot(&slot)) {
        return RECORD_RESULT_FULL;
    }

    uint32_t checksum = event_checksum(s_events, s_event_count);
    uint32_t duration = recording_duration();
    uint16_t track_id = (uint16_t)(slot + 1U);
    uint8_t hdr[RECORD_HEADER_SIZE];
    build_header(hdr, s_event_count, duration, track_id, checksum);

    uint32_t base = slot_address(slot);
    if (!storage_erase(base, RECORD_SLOT_SIZE) ||
        !storage_write(base + RECORD_HEADER_SIZE, (const uint8_t *)s_events,
                       (uint16_t)(s_event_count * sizeof(NoteEvent))) ||
        !storage_write(base, hdr, sizeof(hdr))) {
        return RECORD_RESULT_STORAGE;
    }

    s_slot[s_track_count] = slot;
    s_info[s_track_count].track_id = track_id;
    s_info[s_track_count].event_count = s_event_count;
    s_info[s_track_count].duration_ms = duration;
    ++s_track_count;
    s_event_count = 0;
    s_event_full = false;
    return RECORD_RESULT_OK;
}

void record_cancel(void)
{
    if (s_overdub) {
        s_overdub = false;
        record_playback_stop();
        s_od_n = 0U;
    }
    s_recording = false;
    s_event_full = false;
    s_event_count = 0;
}

uint8_t record_track_count(void)
{
    return s_track_count;
}

bool record_get_info(uint8_t index, record_info_t *info)
{
    if (!s_ready || index >= s_track_count || info == NULL) {
        return false;
    }
    *info = s_info[index];
    return true;
}

bool record_delete(uint8_t index)
{
    if (!s_ready || index >= s_track_count || s_recording) {
        return false;
    }
    uint8_t slot = s_slot[index];
    if (!storage_erase(slot_address(slot), RECORD_SLOT_SIZE)) {
        return false;
    }
    for (uint8_t i = index; i + 1U < s_track_count; ++i) {
        s_slot[i] = s_slot[i + 1U];
        s_info[i] = s_info[i + 1U];
    }
    --s_track_count;
    record_playback_stop();
    return true;
}

/* ---------- 回放调度：统一使用 Audio_NoteOnPitch ---------- */

bool record_playback_start(uint8_t index, uint32_t now_ms)
{
    if (!s_ready || s_recording || index >= s_track_count) {
        return false;
    }
    record_playback_stop();
    uint8_t slot = s_slot[index];
    uint32_t base = slot_address(slot);
    uint8_t hdr[RECORD_HEADER_SIZE];
    uint16_t count;
    uint32_t duration;
    uint16_t track_id;

    if (!storage_read(base, hdr, sizeof(hdr)) ||
        !header_valid(hdr, &count, &duration, &track_id)) {
        return false;
    }
    if (count > 0U) {
        if (!storage_read(base + RECORD_HEADER_SIZE, (uint8_t *)s_events,
                          (uint16_t)(count * sizeof(NoteEvent)))) {
            return false;
        }
        if (event_checksum(s_events, count) != get_u32(&hdr[16])) {
            return false;
        }
    }

    for (uint16_t i = 0; i < RECORD_MAX_EVENTS; ++i) {
        s_pb_voice[i] = -1;
    }
    s_pb_count = (uint16_t)count;
    s_pb_index = 0U;
    s_pb_start = now_ms;
    s_pb_elapsed = 0U;
    s_pb_duration = duration;
    s_pb_paused = false;
    s_pb_active = true;
    return true;
}

/* Keep the timeline in half-milliseconds so 0.5x never loses odd ticks. */
static void pb_advance(uint32_t now_ms)
{
    uint32_t scale = (s_pb_speed == 0U) ? 1U : (s_pb_speed == 2U ? 4U : 2U);
    s_pb_elapsed += (now_ms - s_pb_start) * scale;
    s_pb_start = now_ms;
}

void record_playback_set_speed(uint8_t level)
{
    if (level > 2U) { return; }
    if (s_pb_active && !s_pb_paused) { pb_advance(HAL_GetTick()); }
    s_pb_speed = level;
}

uint8_t record_playback_get_speed(void)
{
    return s_pb_speed;
}

void record_playback_tick(uint32_t now_ms)
{
    if (!s_pb_active || s_pb_paused) {
        return;
    }
    pb_advance(now_ms);
    uint32_t elapsed = s_pb_elapsed / 2U;

    for (uint16_t i = 0; i < s_pb_index; ++i) {
        const NoteEvent *event = &s_events[i];
        if (s_pb_voice[i] >= 0 && !Audio_IsVoiceActive(s_pb_voice[i])) {
            s_pb_voice[i] = AUDIO_VOICE_INVALID;
        }
        if (s_pb_voice[i] >= 0 &&
            elapsed >= event->start_ms + event->dur_ms) {
            Audio_NoteOff(s_pb_voice[i]);
            s_pb_voice[i] = -1;
        }
    }

    while (s_pb_index < s_pb_count &&
           s_events[s_pb_index].start_ms <= elapsed) {
        uint16_t i = s_pb_index++;
        /* Do not replay notes whose complete interval elapsed during a redraw. */
        if (elapsed >= s_events[i].start_ms + s_events[i].dur_ms) { continue; }
        VoiceId id = AUDIO_VOICE_INVALID;
        if (Audio_NoteOnPitchEx(s_events[i].freq_hz, AUDIO_PRIO_MELODY, &id) == AUDIO_OK) {
            s_pb_voice[i] = id;
        } else {
            s_pb_voice[i] = AUDIO_VOICE_INVALID;
        }
    }

    if (s_pb_index >= s_pb_count && elapsed >= s_pb_duration) {
        for (uint16_t i = 0; i < s_pb_count; ++i) {
            if (s_pb_voice[i] >= 0) {
                Audio_NoteOff(s_pb_voice[i]);
                s_pb_voice[i] = -1;
            }
        }
        s_pb_active = false;
    }
}

void record_playback_set_paused(bool paused, uint32_t now_ms)
{
    if (!s_pb_active) return;
    if (paused && !s_pb_paused) {
        pb_advance(now_ms);
        s_pb_paused = true;
        for (uint16_t i = 0; i < s_pb_index; ++i) {
            if (s_pb_voice[i] >= 0) {
                Audio_NoteOff(s_pb_voice[i]);
                s_pb_voice[i] = -1;
            }
        }
    } else if (!paused && s_pb_paused) {
        s_pb_start = now_ms;
        s_pb_paused = false;
        uint32_t elapsed = s_pb_elapsed / 2U;
        for (uint16_t i = 0U; i < s_pb_index; ++i) {
            if (elapsed < s_events[i].start_ms + s_events[i].dur_ms) {
                VoiceId id = AUDIO_VOICE_INVALID;
                if (Audio_NoteOnPitchEx(s_events[i].freq_hz, AUDIO_PRIO_MELODY, &id) == AUDIO_OK) {
                    s_pb_voice[i] = id;
                }
            }
        }
    }
}

void record_playback_stop(void)
{
    if (s_pb_active) {
        for (uint16_t i = 0; i < s_pb_index; ++i) {
            if (s_pb_voice[i] >= 0) {
                Audio_NoteOff(s_pb_voice[i]);
                s_pb_voice[i] = -1;
            }
        }
    }
    s_pb_active = false;
    s_pb_paused = false;
}

bool record_playback_active(void)
{
    return s_pb_active;
}


/* ---------------------------------------------------------------- 小设置区
 *
 * 放在 W25Q64 的最末尾（8 MB 芯片的 0x7F0000），与录音区 0x001000 起的
 * 数据完全隔离，擦除时不会波及录音。只用 2 个字节，一个扇区够用。
 */

bool record_setting_load(uint8_t *buf, uint16_t len)
{
    if (!s_ready || buf == NULL || len == 0U || len > RECORD_SECTOR_SIZE) {
        return false;
    }
    return storage_read(RECORD_SETTING_ADDR, buf, len);
}

bool record_setting_save(const uint8_t *buf, uint16_t len)
{
    if (!s_ready || buf == NULL || len == 0U || len > RECORD_SECTOR_SIZE) {
        return false;
    }
    if (!storage_erase(RECORD_SETTING_ADDR, RECORD_SECTOR_SIZE)) {
        return false;
    }
    return storage_write(RECORD_SETTING_ADDR, buf, len);
}

/* ---------------- 叠录：开始 / 结束 ----------------
 * 开始：先把原曲载入并开始回放，再打开录音（新音符进 s_od_events）。
 * 结束：补全未松手的音符 -> 备份原曲到空槽 -> 合并排序 -> 写回原槽。 */
bool record_overdub_start(uint8_t index, uint32_t now_ms)
{
    if (!s_ready || s_recording || s_overdub || index >= s_track_count) {
        return false;
    }
    s_od_n = 0U;
    s_od_src = index;
    if (!record_playback_start(index, now_ms)) {
        return false;
    }
    s_overdub = true;
    s_recording = true;
    s_event_full = false;
    s_start_ms = now_ms;
    return true;
}

bool record_overdub_active(void)
{
    return s_overdub;
}

static void od_sort_events(NoteEvent *a, uint16_t n)
{
    for (uint16_t i = 1U; i < n; ++i) {
        NoteEvent key = a[i];
        int16_t j = (int16_t)i - 1;
        while (j >= 0 && a[j].start_ms > key.start_ms) {
            a[j + 1] = a[j];
            --j;
        }
        a[j + 1] = key;
    }
}

record_result_t record_overdub_stop(uint32_t now_ms)
{
    if (!s_overdub) {
        return RECORD_RESULT_EMPTY;
    }
    s_overdub = false;
    s_recording = false;
    record_playback_stop();

    for (uint16_t i = 0; i < s_od_n; ++i) {
        NoteEvent *e = &s_od_events[i];
        if (e->dur_ms == 0U) {
            uint32_t sa = s_start_ms + e->start_ms;
            uint32_t du = now_ms - sa;
            if (du == 0U) { du = 1U; }
            e->dur_ms = (du > 0xFFFFUL) ? 0xFFFFU : (uint16_t)du;
        }
    }
    if (s_od_n == 0U) {
        return RECORD_RESULT_EMPTY;
    }

    uint16_t orig = s_info[s_od_src].event_count;
    if (orig > RECORD_MAX_EVENTS) { orig = RECORD_MAX_EVENTS; }

    /* 备份原曲到空槽（没有空槽就跳过，不影响合并） */
    uint8_t backup_slot;
    if (find_free_slot(&backup_slot) && s_track_count < RECORD_MAX_TRACKS) {
        uint32_t ck = event_checksum(s_events, orig);
        uint32_t dur0 = recording_duration();
        uint8_t hdr0[RECORD_HEADER_SIZE];
        build_header(hdr0, orig, dur0, (uint16_t)(backup_slot + 1U), ck);
        uint32_t baddr = slot_address(backup_slot);
        if (storage_erase(baddr, RECORD_SLOT_SIZE) &&
            storage_write(baddr + RECORD_HEADER_SIZE, (const uint8_t *)s_events,
                          (uint16_t)(orig * sizeof(NoteEvent))) &&
            storage_write(baddr, hdr0, sizeof(hdr0))) {
            s_slot[s_track_count] = backup_slot;
            s_info[s_track_count].track_id = (uint16_t)(backup_slot + 1U);
            s_info[s_track_count].event_count = orig;
            s_info[s_track_count].duration_ms = dur0;
            ++s_track_count;
        }
    }

    /* 合并：原曲 + 新层，超出 600 则丢弃新层尾部 */
    uint16_t total = orig;
    for (uint16_t i = 0; i < s_od_n && total < RECORD_MAX_EVENTS; ++i) {
        s_events[total++] = s_od_events[i];
    }
    od_sort_events(s_events, total);

    uint32_t ck = event_checksum(s_events, total);
    uint32_t dur = recording_duration();
    uint8_t hdr[RECORD_HEADER_SIZE];
    build_header(hdr, total, dur, s_info[s_od_src].track_id, ck);
    uint32_t addr = slot_address(s_slot[s_od_src]);
    if (!storage_erase(addr, RECORD_SLOT_SIZE) ||
        !storage_write(addr + RECORD_HEADER_SIZE, (const uint8_t *)s_events,
                       (uint16_t)(total * sizeof(NoteEvent))) ||
        !storage_write(addr, hdr, sizeof(hdr))) {
        return RECORD_RESULT_STORAGE;
    }
    s_info[s_od_src].event_count = total;
    s_info[s_od_src].duration_ms = dur;
    s_od_n = 0U;
    return RECORD_RESULT_OK;
}
