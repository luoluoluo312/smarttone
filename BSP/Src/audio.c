#include "audio.h"

#include <stddef.h>

typedef struct {
    bool active;
    uint16_t freq_hz;
    uint8_t prio;
    uint8_t generation;
} audio_voice_t;

#define AUDIO_VOICES 2U
#define AUDIO_GEN_MASK 0x1FU

static audio_voice_t s_voices[AUDIO_VOICES];
static uint8_t s_octave = OCT_MID;
static int8_t s_transpose = 0;
static uint8_t s_volume = VOL_MID;
static TimbreId s_timbre = TIMBRE_ORGAN;

/* C4~B4 半音频率，移调后按八度倍增/减半。 */
static const uint16_t s_note_freq[12] = {
    262U, 277U, 294U, 311U, 330U, 349U,
    370U, 392U, 415U, 440U, 466U, 494U
};

static const uint8_t s_note_semitone[7] = {0U, 2U, 4U, 5U, 7U, 9U, 11U};

static uint16_t note_frequency(uint8_t note)
{
    int16_t semitone = (int16_t)s_note_semitone[note] + s_transpose;
    int16_t octave = (int16_t)s_octave - OCT_MID;
    while (semitone < 0) {
        semitone = (int16_t)(semitone + 12);
        --octave;
    }
    while (semitone >= 12) {
        semitone = (int16_t)(semitone - 12);
        ++octave;
    }
    uint32_t freq = s_note_freq[semitone];
    while (octave > 0) {
        freq <<= 1;
        --octave;
    }
    while (octave < 0) {
        freq >>= 1;
        ++octave;
    }
    return (uint16_t)freq;
}

static VoiceId make_id(uint8_t slot)
{
    return (VoiceId)(s_voices[slot].generation << 2 | slot);
}

static bool valid_voice(VoiceId id)
{
    if (id < 0) return false;
    uint8_t slot = (uint8_t)id & 0x03U;
    if (slot >= AUDIO_VOICES) return false;
    return s_voices[slot].active &&
           ((uint8_t)(id >> 2) == s_voices[slot].generation);
}

void Audio_Init(void)
{
    for (uint8_t i = 0; i < AUDIO_VOICES; ++i) {
        s_voices[i].active = false;
        s_voices[i].freq_hz = 0U;
        s_voices[i].prio = AUDIO_PRIO_LOWEST;
        s_voices[i].generation = 0U;
    }
    s_octave = OCT_MID;
    s_transpose = 0;
    s_volume = VOL_MID;
    s_timbre = TIMBRE_ORGAN;
}

static AudioStatus allocate(uint16_t freq, uint8_t prio, VoiceId *out)
{
    uint8_t slot = AUDIO_VOICES;
    for (uint8_t i = 0; i < AUDIO_VOICES; ++i) {
        if (!s_voices[i].active) {
            slot = i;
            break;
        }
    }

    if (slot == AUDIO_VOICES) {
        /* 优先级数值越小越重要；只有更重要的请求才能抢占。 */
        for (uint8_t i = 0; i < AUDIO_VOICES; ++i) {
            if (s_voices[i].prio > prio) {
                slot = i;
                break;
            }
        }
    }
    if (slot == AUDIO_VOICES) {
        return AUDIO_ERR_NO_VOICE;
    }

    if (s_voices[slot].active) {
        s_voices[slot].generation = (uint8_t)((s_voices[slot].generation + 1U) & AUDIO_GEN_MASK);
    }
    s_voices[slot].active = true;
    s_voices[slot].freq_hz = freq;
    s_voices[slot].prio = prio;
    if (out != NULL) {
        *out = make_id(slot);
    }
    return AUDIO_OK;
}

VoiceId Audio_NoteOn(uint8_t note)
{
    if (note >= 7U) {
        return AUDIO_VOICE_INVALID;
    }
    VoiceId id = AUDIO_VOICE_INVALID;
    if (allocate(note_frequency(note), AUDIO_PRIO_KEYBOARD, &id) != AUDIO_OK) {
        return AUDIO_VOICE_INVALID;
    }
    return id;
}

AudioStatus Audio_NoteOff(VoiceId id)
{
    if (id < 0) return AUDIO_ERR_BAD_PARAM;
    uint8_t slot = (uint8_t)id & 0x03U;
    if (slot >= AUDIO_VOICES || !valid_voice(id)) {
        /* 已被抢占的旧标识按幂等空操作处理。 */
        return AUDIO_OK;
    }
    s_voices[slot].active = false;
    s_voices[slot].freq_hz = 0U;
    s_voices[slot].generation = (uint8_t)((s_voices[slot].generation + 1U) & AUDIO_GEN_MASK);
    return AUDIO_OK;
}

AudioStatus Audio_NoteOnPitch(uint16_t freq, VoiceId *out)
{
    return allocate(freq, AUDIO_PRIO_MELODY, out);
}

AudioStatus Audio_NoteOnPitchEx(uint16_t freq, uint8_t prio, VoiceId *out)
{
    if (prio > AUDIO_PRIO_LOWEST) {
        return AUDIO_ERR_BAD_PARAM;
    }
    return allocate(freq, prio, out);
}

AudioStatus Audio_Retune(VoiceId id, uint16_t freq)
{
    if (!valid_voice(id)) {
        return AUDIO_ERR_BAD_PARAM;
    }
    uint8_t slot = (uint8_t)id & 0x03U;
    s_voices[slot].freq_hz = freq;
    return AUDIO_OK;
}

void Audio_AllOff(void)
{
    for (uint8_t i = 0; i < AUDIO_VOICES; ++i) {
        s_voices[i].active = false;
        s_voices[i].freq_hz = 0U;
        s_voices[i].generation = (uint8_t)((s_voices[i].generation + 1U) & AUDIO_GEN_MASK);
    }
}

uint8_t Audio_GetActiveCount(void)
{
    uint8_t count = 0U;
    for (uint8_t i = 0; i < AUDIO_VOICES; ++i) {
        if (s_voices[i].active) ++count;
    }
    return count;
}

bool Audio_IsVoiceActive(VoiceId id)
{
    return valid_voice(id);
}

uint16_t Audio_GetVoiceFreq(VoiceId id)
{
    if (!valid_voice(id)) {
        return 0U;
    }
    return s_voices[(uint8_t)id & 0x03U].freq_hz;
}

void Audio_SetOctave(uint8_t octave)
{
    if (octave <= OCT_HIGH) {
        s_octave = octave;
    }
}

uint8_t Audio_GetOctave(void)
{
    return s_octave;
}

void Audio_SetVolume(uint8_t volume)
{
    if (volume <= VOL_HIGH) {
        s_volume = volume;
    }
}

uint8_t Audio_GetVolume(void)
{
    return s_volume;
}

void Audio_SetTranspose(int8_t semitones)
{
    if (semitones >= -2 && semitones <= 2) {
        s_transpose = semitones;
    }
}

int8_t Audio_GetTranspose(void)
{
    return s_transpose;
}

void Audio_SetTimbre(TimbreId timbre)
{
    if (timbre <= TIMBRE_BELL) {
        s_timbre = timbre;
    }
}

TimbreId Audio_GetTimbre(void)
{
    return s_timbre;
}

void Audio_Envelope_Tick(void)
{
}

void Metronome_Tick(void)
{
}

void Song_Tick(void)
{
}

void Drum_Tick(void)
{
}
