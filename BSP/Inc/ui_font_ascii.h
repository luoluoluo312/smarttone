#ifndef UI_FONT_ASCII_H
#define UI_FONT_ASCII_H

#include <stdint.h>

#define UI_ASCII_W 5U
#define UI_ASCII_H 7U

typedef struct {
    char code;
    uint8_t rows[UI_ASCII_H];
} ui_ascii_glyph_t;

extern const ui_ascii_glyph_t ui_ascii_glyphs[];
extern const uint16_t ui_ascii_glyph_count;

#endif
