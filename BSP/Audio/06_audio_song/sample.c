#include "sample.h"
#include "audio.h"
#include "audio_config.h"
#include "audio_pwm.h"
#include "record.h"

static const SampleDef *s_sample;
static uint32_t s_loaded;
static uint32_t s_hash;
static uint16_t s_gain;
static bool s_failed;

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void Sample_Stop(void)
{
    AudioPwm_PcmStop();
    s_sample = NULL;
}

void Sample_Service(void)
{
    if (s_sample == NULL || s_failed) { return; }
    static const uint16_t levels[] = {
        AUDIO_DUTY_PERMILLE_1, AUDIO_DUTY_PERMILLE_2, AUDIO_DUTY_PERMILLE_3,
        AUDIO_DUTY_PERMILLE_4, AUDIO_DUTY_PERMILLE_5
    };
    /* Current C4 resource peaks at 114 counts; use the remaining 127-count
     * headroom (about +0.95 dB) without rewriting or clipping the PCM asset. */
    uint16_t level = s_gain != 0U ? s_gain : levels[Audio_GetVolume()];
    AudioPwm_PcmGain((uint16_t)((level * 1116UL + 500U) / 1000U));
    uint8_t *dest;
    uint16_t available = AudioPwm_PcmWritable(&dest);
    if (available == 0U) { return; }
    if (!record_asset_read(s_sample->address + 32U + s_loaded, dest, available)) {
        s_failed = true;
        AudioPwm_PcmStop();
        return;
    }
    for (uint16_t i = 0U; i < available; ++i) {
        s_hash = (s_hash ^ dest[i]) * 16777619UL;
    }
    s_loaded += available;
    if (s_loaded == s_sample->samples && s_hash != s_sample->checksum) {
        s_failed = true;
        AudioPwm_PcmStop();
        return;
    }
    AudioPwm_PcmCommit(available);
}

bool Sample_Play(const SampleDef *def, uint16_t gain, uint8_t speed)
{
    uint8_t header[32];
    Sample_Stop();
    s_failed = false;
    if (def == NULL || def->address < RECORD_ASSET_BASE ||
        def->address > RECORD_ASSET_END - 32U || def->samples == 0U ||
        def->samples > RECORD_ASSET_END - def->address - 32U ||
        !record_asset_read(def->address, header, sizeof(header)) ||
        read_u32(header) != 0x314D4350UL || read_u32(header + 4U) != 16000U ||
        read_u32(header + 8U) != def->samples ||
        read_u32(header + 12U) != def->checksum) { return false; }
    Audio_AllOff();
    s_sample = def;
    s_loaded = 0U;
    s_hash = 2166136261UL;
    s_gain = gain;
    AudioPwm_PcmBegin(def->samples, speed);
    Sample_Service(); /* Prefill the ring before enabling consumption. */
    if (s_failed || !AudioPwm_PcmActive()) { Sample_Stop(); return false; }
    AudioPwm_PcmPause(false);
    return true;
}

void Sample_Pause(bool paused) { AudioPwm_PcmPause(paused); }
void Sample_SetSpeed(uint8_t speed) { AudioPwm_PcmSetSpeed(speed); }
uint32_t Sample_PositionMs(void) { return AudioPwm_PcmPosition() / 16U; }
bool Sample_Failed(void) { return s_failed; }
bool Sample_Finished(void)
{
    return s_sample != NULL && AudioPwm_PcmPosition() >= s_sample->samples;
}
