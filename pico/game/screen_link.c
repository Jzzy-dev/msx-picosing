// PicoSing - karaoke for the MSX PicoVerse 2350
// screen_link.c - the Pico side of the display list handshake (see .h)

#include "screen_link.h"
#include "dlist.h"
#include "game.h"
#include <string.h>

bool link_strict = true;

static volatile uint32_t req_count;
static volatile uint8_t  req_value;
static volatile bool     hello;

static uint32_t handled;
static uint8_t  seq;
static fb_t     want, shown;
static int      refresh_row;
static int      last_state = -1;
static int      stale;
static uint8_t  pal_now[32], pal_sent[32];

void link_on_write(uint16_t addr, uint8_t data)
{
    if (addr == 0x7F81u)
    {
        req_value = data;
        __sync_synchronize();
        req_count = req_count + 1u;
    }
    else if (addr == 0x7F83u && data == 0xA5u)
    {
        hello = true;
    }
}

// Rows to send again in full. They are marked here and spoiled after the
// next picture is drawn: each byte the opposite of what will be wanted, so
// they always differ (spoiling against the old screen instead could, by
// chance, make the spoiled row equal to the new picture: then it was never sent).
static uint8_t resend[FB_H];

static void invalidate_rows(int y, int n)
{
    for (int i = 0; i < n; i++, y++) resend[y % FB_H] = 1;
}

static void spoil_rows(void)
{
    for (int y = 0; y < FB_H; y++)
        if (resend[y])
        {
            resend[y] = 0;
            for (int x = 0; x < FB_STRIDE; x++) shown.px[y][x] = (uint8_t)(want.px[y][x] ^ 0xFFu);
        }
}

bool link_service(uint8_t *window)
{
    uint32_t r = req_count;
    if (r == handled) return false;
    handled = r;
    __sync_synchronize();
    uint8_t v = req_value;

    bool pal = false;
    if (hello)
    {
        hello = false;
        memset(&shown, 0, sizeof shown);      // CHGMOD cleared the screen to colour 0
        pal = true;
        stale = 0;
    }
    else if (link_strict && v != seq)
    {
        // The MSX has not run the newest list yet (it timed out while we
        // were busy and asked again): it will find that list and ask anew.
        // After a few such requests, assume the MSX lost track and go on.
        if (++stale < 4) return false;
    }
    stale = 0;

    int st = (int)game_state();
    if (st != last_state)
    {
        last_state = st;
        invalidate_rows(0, FB_H);             // a new screen goes out in full
    }
    invalidate_rows(refresh_row, 1);
    refresh_row = (refresh_row + 1) % FB_H;

    game_draw(&want);
    spoil_rows();
    // the palette goes out whenever the screen's palette changed (colour
    // cycling on the title, a cover's colours, the beat on the lanes)
    game_palette(pal_now);
    if (memcmp(pal_now, pal_sent, 32) != 0) pal = true;
    if (pal) memcpy(pal_sent, pal_now, 32);
    bool more;
    size_t n = dlist_build(&want, &shown, window + DL_HEADER, LINK_BUDGET, pal ? pal_now : NULL, &more);
    window[1] = (uint8_t)n;
    window[2] = (uint8_t)(n >> 8);
    window[3] = more ? DL_FLAG_MORE : 0;
    __sync_synchronize();
    window[0] = ++seq;                        // last: the MSX waits for this
    return true;
}
