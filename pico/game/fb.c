// PicoSing - karaoke for the MSX PicoVerse 2350
// fb.c - drawing into the SCREEN 5 picture
//
// Font: font8x8 by Daniel Hepper (public domain), basic ASCII and Latin-1.

#include "fb.h"
#include <string.h>
#include "font8x8_basic.h"
#include "font8x8_ext_latin.h"

#define RGB(r, g, b) (uint8_t)(((r) << 4) | (b)), (uint8_t)(g)

const uint8_t fb_palette[32] = {
    RGB(0, 0, 0),   // C_BLACK
    RGB(0, 0, 2),   // C_LANE    lane background
    RGB(1, 1, 3),   // C_GRID    lane lines
    RGB(5, 5, 6),   // C_NOTE    target note
    RGB(7, 6, 1),   // C_GOLD    golden note
    RGB(1, 3, 7),   // C_P1      blue mic
    RGB(4, 6, 7),   // C_P1_HIT
    RGB(6, 1, 1),   // C_P2      red mic
    RGB(7, 5, 4),   // C_P2_HIT
    RGB(7, 7, 7),   // C_WHITE
    RGB(4, 4, 4),   // C_GREY
    RGB(7, 7, 2),   // C_YELLOW
    RGB(2, 7, 7),   // C_CYAN
    RGB(2, 2, 2),   // C_DARK
    RGB(2, 6, 2),   // C_GREEN
    RGB(5, 3, 6),   // C_RAP     freestyle / rap note
};

void fb_clear(fb_t *f, uint8_t c)
{
    memset(f->px, (c << 4) | c, sizeof f->px);
}

void fb_pixel(fb_t *f, int x, int y, uint8_t c)
{
    if ((unsigned)x >= FB_W || (unsigned)y >= FB_H) return;
    uint8_t *b = &f->px[y][x >> 1];
    *b = (x & 1) ? (uint8_t)((*b & 0xF0) | c) : (uint8_t)((*b & 0x0F) | (c << 4));
}

void fb_rect(fb_t *f, int x, int y, int w, int h, uint8_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB_W) w = FB_W - x;
    if (y + h > FB_H) h = FB_H - y;
    if (w <= 0 || h <= 0) return;
    uint8_t cc = (uint8_t)((c << 4) | c);
    for (int yy = y; yy < y + h; yy++)
    {
        uint8_t *row = f->px[yy];
        int xx = x, xe = x + w;
        if (xx & 1) { row[xx >> 1] = (uint8_t)((row[xx >> 1] & 0xF0) | c); xx++; }
        int full = (xe - xx) >> 1;
        if (full > 0) { memset(row + (xx >> 1), cc, (size_t)full); xx += full * 2; }
        if (xx < xe) row[xx >> 1] = (uint8_t)((row[xx >> 1] & 0x0F) | (c << 4));
    }
}

void fb_frame(fb_t *f, int x, int y, int w, int h, uint8_t c)
{
    fb_rect(f, x, y, w, 1, c);
    fb_rect(f, x, y + h - 1, w, 1, c);
    fb_rect(f, x, y, 1, h, c);
    fb_rect(f, x + w - 1, y, 1, h, c);
}

static const char *glyph(unsigned char ch)
{
    if (ch < 128) return font8x8_basic[ch];
    if (ch >= 0xA0) return font8x8_ext_latin[ch - 0xA0];
    return font8x8_basic['?'];
}

int fb_text(fb_t *f, int x, int y, const char *s, int len, uint8_t c)
{
    int x0 = x;
    for (int i = 0; i < len; i++, x += 8)
    {
        const char *g = glyph((unsigned char)s[i]);
        for (int r = 0; r < 8; r++)
        {
            unsigned bits = (unsigned char)g[r];
            for (int b = 0; bits && b < 8; b++, bits >>= 1)
                if (bits & 1) fb_pixel(f, x + b, y + r, c);
        }
    }
    return x - x0;
}

int fb_textz(fb_t *f, int x, int y, const char *s, uint8_t c)
{
    return fb_text(f, x, y, s, (int)strlen(s), c);
}

int fb_text_width(const char *s, int len)
{
    (void)s;
    return len * 8;
}
