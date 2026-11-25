// PicoSing - karaoke for the MSX PicoVerse 2350
// fb.h - a SCREEN 5 picture in the Pico's RAM (256x212, 4 bits per pixel,
// two pixels per byte, left pixel in the high nibble: the V9938 layout)

#ifndef PICOSING_FB_H
#define PICOSING_FB_H

#include <stdint.h>
#include <stddef.h>

#define FB_W       256
#define FB_H       212
#define FB_STRIDE  128

typedef struct { uint8_t px[FB_H][FB_STRIDE]; } fb_t;

// Palette: 16 entries in V9938 format (byte 0 = 0RRR0BBB, byte 1 = 00000GGG)
extern const uint8_t fb_palette[32];

enum {
    C_BLACK = 0, C_LANE, C_GRID, C_NOTE, C_GOLD, C_P1, C_P1_HIT, C_P2,
    C_P2_HIT, C_WHITE, C_GREY, C_YELLOW, C_CYAN, C_DARK, C_GREEN, C_RAP
};

void fb_clear(fb_t *f, uint8_t c);
void fb_pixel(fb_t *f, int x, int y, uint8_t c);
void fb_rect(fb_t *f, int x, int y, int w, int h, uint8_t c);
void fb_frame(fb_t *f, int x, int y, int w, int h, uint8_t c);   // outline
// Latin-1 text with the 8x8 font, transparent background. Returns the width.
int  fb_text(fb_t *f, int x, int y, const char *s, int len, uint8_t c);
int  fb_textz(fb_t *f, int x, int y, const char *s, uint8_t c);
int  fb_text_width(const char *s, int len);

#endif
