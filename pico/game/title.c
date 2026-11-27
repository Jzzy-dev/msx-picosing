// PicoSing - karaoke for the MSX PicoVerse 2350
// title.c - the title screen
//
// What an MSX could not do on its own: a full-screen plasma, worked out by
// the Pico once and set flowing by cycling its 10 colours through a rainbow
// every frame; a big slanted logo whose colour bands run through the
// rainbow and flash white on the beat of the title tune (synth.c, 132 BPM);
// bouncing letters and a scroller.
//
// The moving effects are in the palette: the MSX gets 32 bytes a frame for
// them, and only the letters and the scroller (under 2 KB a frame) as
// pixels, so every frame is written before the TV picture reaches it.
//
// Palette: 0 black, 1-10 plasma, 11 logo shadow, 12-14 logo bands, 15 text.
// The credits use the logo's bands and the text colour, one per letter,
// moving on by a letter on every beat.

#include "game_int.h"
#include "scratch.h"
extern char font8x8_basic[128][8];   // fb.c
#include <math.h>
#include <string.h>

#define PLASMA_FIRST 1
#define PLASMA_N     10
#define T_SHADOW     11
#define T_LOGO       12
#define T_TEXT       15
#define BEAT_MS      (60000.0f / 132.0f)

static fb_t *plasma;                     // (the scratch buffer: made again when another job used it)

static void make_plasma(void)
{
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++)
        {
            float dx = (float)x - 128.0f, dy = (float)y - 96.0f;
            float v = sinf((float)x * 0.045f) + sinf((float)y * 0.061f) + sinf((float)(x + y) * 0.033f)
                    + sinf(sqrtf(dx * dx + dy * dy) * 0.055f);
            int k = (int)((v + 4.0f) / 8.0f * PLASMA_N);
            if (k < 0) k = 0;
            if (k >= PLASMA_N) k = PLASMA_N - 1;
            fb_pixel(plasma, x, y, (uint8_t)(PLASMA_FIRST + k));
        }

}

static void hsv(float h, float v, int *r, int *g, int *b)
{
    h -= floorf(h);
    float h6 = h * 6.0f, f = h6 - floorf(h6);
    float q = v * (1 - f), t = v * f;
    float rr, gg, bb;
    switch ((int)h6)
    {
        case 0: rr = v; gg = t; bb = 0; break;
        case 1: rr = q; gg = v; bb = 0; break;
        case 2: rr = 0; gg = v; bb = t; break;
        case 3: rr = 0; gg = q; bb = v; break;
        case 4: rr = t; gg = 0; bb = v; break;
        default: rr = v; gg = 0; bb = q; break;
    }
    *r = (int)lrintf(rr * 7); *g = (int)lrintf(gg * 7); *b = (int)lrintf(bb * 7);
}

static void set(uint8_t *pal, int i, int r, int g, int b)
{
    pal[i * 2] = (uint8_t)((r << 4) | b);
    pal[i * 2 + 1] = (uint8_t)g;
}

void title_palette(uint8_t pal[32], uint32_t t)
{
    // the plasma: a rainbow, dimmed so the logo stands out, flowing
    float flow = (float)t / 2600.0f;
    for (int k = 0; k < PLASMA_N; k++)
    {
        int r, g, b;
        hsv(flow + (float)k / (PLASMA_N * 1.6f), 0.55f, &r, &g, &b);
        set(pal, PLASMA_FIRST + k, r, g, b);
    }
    set(pal, 0, 0, 0, 0);
    set(pal, T_SHADOW, 0, 0, 1);
    // the logo: three bands, warm rainbow colours running down it; white on the beat
    float beat = fmodf((float)t, BEAT_MS) / BEAT_MS;
    for (int k = 0; k < 3; k++)
    {
        int r, g, b;
        hsv((float)t / 1800.0f + (float)k * 0.09f, 1.0f, &r, &g, &b);
        if (beat < 0.12f) { r = 7; g = 7; b = b + 4 > 7 ? 7 : b + 4; }
        else
        {
            // keep them light: mix with white
            r = (r + 7 * (2 - k) / 2 + 1) / 2 + (k == 0); g = (g + 7 * (2 - k) / 2 + 1) / 2; b = (b + 1) / 2;
            if (r > 7) r = 7;
        }
        set(pal, T_LOGO + k, r, g, b);
    }
    set(pal, T_TEXT, 7, 7, 7);
}

// A glyph 3x wide and 4x tall, each screen row shifted by its own amount
static void big_char(fb_t *f, char ch, int x, int y, const int *dx, uint8_t band_base, bool shadow)
{
    const char *g = font8x8_basic[(unsigned char)ch & 0x7F];
    for (int sy = 0; sy < 32; sy++)
    {
        unsigned bits = (unsigned char)g[sy / 4];
        uint8_t c = shadow ? T_SHADOW : (uint8_t)(band_base + (sy < 11 ? 0 : sy < 22 ? 1 : 2));
        for (int b = 0; bits && b < 8; b++, bits >>= 1)
            if (bits & 1) fb_rect(f, x + b * 3 + dx[sy], y + sy, 3, 1, c);
    }
}

static const char scroll_text[] =
    "PICOSING - KARAOKE FOR THE MSX WITH A PICOVERSE 2350 AND PLAYSTATION SINGSTAR MICS ...  "
    "THE PICO DOES THE WORK: SONGS, MUSIC, MICS, PITCH AND SCORES ... THE MSX STEALS THE SHOW ...  "
    "GRAB A MIC, PICK A SONG AND SING!  ...  GREETINGS TO CRISTIANO, THE MSX.ORG CREW "
    "AND EVERY MSX SINGER OUT THERE  ...  HAPPY MSX-SING!          ";

void title_draw(fb_t *f, uint32_t t)
{
    if (!scratch_mine(SCR_TITLE)) { plasma = scratch(SCR_TITLE); make_plasma(); }
    plasma = (fb_t *)g_scratch;
    memcpy(f->px, plasma->px, sizeof f->px);
    float ts = (float)t / 1000.0f;

    // logo
    static const char logo[] = "PicoSing";
    int dx[32];
    for (int sy = 0; sy < 32; sy++) dx[sy] = (31 - sy) / 5;     // slanted
    int lx = (FB_W - 8 * 24) / 2, ly = 36;
    fb_rect(f, lx - 10, ly - 8, 8 * 24 + 20, 48, 0);
    for (int i = 0; i < 8; i++) big_char(f, logo[i], lx + i * 24 + 3, ly + 3, dx, 0, true);
    for (int i = 0; i < 8; i++) big_char(f, logo[i], lx + i * 24, ly, dx, T_LOGO, false);

    // bouncing letters
    static const char sub[] = "HAPPY MSX-SING!";
    int n = (int)strlen(sub);
    int sx = (FB_W - n * 8) / 2;
    fb_rect(f, sx - 6, 92, n * 8 + 12, 22, 0);
    for (int i = 0; i < n; i++)
    {
        int y = 98 + (int)lrintf(3.0f * sinf(ts * 7.0f + (float)i * 0.55f));
        fb_text(f, sx + i * 8, y, sub + i, 1, T_TEXT);
    }

    // press a key (blinks)
    if (((t / 500) & 1) == 0)
    {
        static const char press[] = "PRESS SPACE OR FIRE";
        int pn = (int)strlen(press);
        fb_rect(f, (FB_W - pn * 8) / 2 - 4, 146, pn * 8 + 8, 12, 0);
        fb_text(f, (FB_W - pn * 8) / 2, 148, press, pn, T_TEXT);
    }

    // scroller, 60 pixels a second
    fb_rect(f, 0, 184, FB_W, 12, 0);
    int len = (int)strlen(scroll_text);
    int off = (int)(t * 60 / 1000) % (len * 8);
    int first = off / 8, shift = off % 8;
    for (int i = 0; i <= 33; i++)
    {
        int x = i * 8 - shift;
        char ch = scroll_text[(first + i) % len];
        if (x > -8 && x < FB_W) fb_text(f, x, 186, &ch, 1, T_TEXT);
    }
    fb_text(f, 4, 202, "v0.8", 4, T_SHADOW + 1);

    // the credits, bottom right: the letters change colour on the beat
    static const char by[] = "BY JZZY & CLAUDE 2026";
    int bl = (int)strlen(by);
    int bx = FB_W - 4 - bl * 8;
    fb_rect(f, bx - 3, 199, bl * 8 + 5, 12, 0);
    int beat = (int)((float)t / BEAT_MS);
    static const uint8_t cyc[4] = { T_LOGO, T_TEXT, T_LOGO + 1, T_LOGO + 2 };
    for (int i = 0; i < bl; i++)
        if (by[i] != ' ') fb_text(f, bx + i * 8, 201, by + i, 1, cyc[(i + beat) & 3]);
}
