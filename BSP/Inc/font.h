#ifndef FONT_H
#define FONT_H

#include <stdint.h>

typedef struct {
    uint32_t codepoint;
    const uint8_t *data;
} font_glyph_t;

typedef struct {
    uint8_t width;
    uint8_t height;
    uint8_t bpp;
    uint8_t gap;
    uint16_t bytes_per_glyph;
    const font_glyph_t *glyphs;
    uint16_t count;
} font_t;

extern const font_t font12;
extern const font_t font16;
extern const font_t font24;

#endif
