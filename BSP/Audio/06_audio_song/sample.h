#ifndef SAMPLE_H
#define SAMPLE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t address;
    uint32_t samples;
    uint32_t checksum;
} SampleDef;

bool Sample_Play(const SampleDef *def, uint16_t gain, uint8_t speed);
void Sample_SetSpeed(uint8_t speed);
void Sample_Stop(void);
void Sample_Pause(bool paused);
/* Main context only, also called between TFT transactions on shared SPI1. */
void Sample_Service(void);
uint32_t Sample_PositionMs(void);
bool Sample_Finished(void);
bool Sample_Failed(void);

#endif
