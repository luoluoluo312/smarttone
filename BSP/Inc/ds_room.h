#ifndef DS_ROOM_H
#define DS_ROOM_H

#include <stdint.h>

void ds_room_enter(uint32_t now);
void ds_room_toggle_dance(uint32_t now);
void ds_room_next_dance(void);
void ds_room_tick(uint32_t now, uint8_t pressed);

#ifdef RECORD_HOST_TEST
int ds_room_test_completed_step(void);
void ds_room_test_redraw_dance(void);
#endif

#endif
