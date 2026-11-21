#ifndef MELODY_MAKER_H
#define MELODY_MAKER_H

#include "audio_types.h"
#include "bsp_encoder.h"

#define MELODY_MAX_EVENTS 64U
#define MELODY_REST 0xFFU

typedef enum { MELODY_CALM, MELODY_HAPPY, MELODY_DREAM } melody_style_t;
typedef enum { MELODY_SELECT, MELODY_PLAYING, MELODY_FINISHED } melody_state_t;
typedef struct {
    uint32_t start_ms;
    uint16_t duration_ms;
    uint8_t note;                 /* NOTE_DO..NOTE_SI, or MELODY_REST */
    uint8_t octave;
} melody_event_t;
typedef struct {
    uint32_t elapsed_ms;
    uint32_t total_ms;
    uint16_t bpm;
    uint8_t count, bars, style, timbre, state, current_note;
} melody_info_t;

void melody_maker_enter(void);
void melody_maker_leave(void);
bool melody_maker_active(void);
void melody_maker_draw(void);
void melody_maker_handle_event(bsp_encoder_event_t event);
bool melody_maker_tick(uint32_t now_ms); /* true requests return to main menu */

/* Fixed seed gives identical music; generation is bounded and never plays it. */
bool melody_maker_generate(TimbreId timbre, uint32_t seed);
bool melody_maker_replay(uint32_t now_ms);
const melody_event_t *melody_maker_events(void);
void melody_maker_get_info(melody_info_t *out);

#endif
