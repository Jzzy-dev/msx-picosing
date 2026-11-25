// PicoSing - karaoke for the MSX PicoVerse 2350
// dlist.c - display lists (see dlist.h)

#include "dlist.h"
#include <string.h>

#define GAP_MERGE   4      // changed runs this close on a row become one
#define MAX_SPANS   24     // per row
#define MAX_OPEN    48     // rectangles growing downwards
#define FILL_MIN    12     // uniform areas from this many bytes go as FILL

typedef struct { uint8_t x, w; } span_t;
typedef struct { uint8_t x, w, y, h; bool live; } rect_t;

typedef struct {
    const fb_t *want;
    fb_t       *shown;
    uint8_t    *out;
    size_t      used, budget;
    bool        full;
} ctx_t;

static bool uniform(const fb_t *f, int x, int y, int w, int h, uint8_t *c)
{
    uint8_t v = f->px[y][x];
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            if (f->px[yy][xx] != v) return false;
    *c = v;
    return true;
}

// Write one rectangle, or as many rows of it as fit. Returns rows written.
static int emit(ctx_t *k, int x, int y, int w, int h)
{
    if (k->full || w <= 0 || h <= 0) return 0;
    uint8_t c;
    if (w * h >= FILL_MIN && uniform(k->want, x, y, w, h, &c))
    {
        if (k->used + 6 > k->budget) { k->full = true; return 0; }
        uint8_t *o = k->out + k->used;
        o[0] = DL_OP_FILL; o[1] = (uint8_t)x; o[2] = (uint8_t)y; o[3] = (uint8_t)w; o[4] = (uint8_t)h; o[5] = c;
        k->used += 6;
        for (int yy = y; yy < y + h; yy++) memset(&k->shown->px[yy][x], c, (size_t)w);
        return h;
    }
    size_t room = k->budget > k->used + 5 ? k->budget - k->used - 5 : 0;
    int rows = (int)(room / (size_t)w);
    if (rows > h) rows = h;
    if (rows <= 0) { k->full = true; return 0; }
    uint8_t *o = k->out + k->used;
    o[0] = DL_OP_COPY; o[1] = (uint8_t)x; o[2] = (uint8_t)y; o[3] = (uint8_t)w; o[4] = (uint8_t)rows;
    o += 5;
    for (int yy = y; yy < y + rows; yy++)
    {
        memcpy(o, &k->want->px[yy][x], (size_t)w);
        memcpy(&k->shown->px[yy][x], o, (size_t)w);
        o += w;
    }
    k->used += 5 + (size_t)w * (size_t)rows;
    if (rows < h) k->full = true;
    return rows;
}

static int row_spans(const fb_t *a, const fb_t *b, int y, span_t *sp)
{
    const uint8_t *ra = a->px[y], *rb = b->px[y];
    if (!memcmp(ra, rb, FB_STRIDE)) return 0;
    int n = 0, x = 0;
    while (x < FB_STRIDE)
    {
        while (x < FB_STRIDE && ra[x] == rb[x]) x++;
        if (x >= FB_STRIDE) break;
        int s = x;
        int last = x;
        while (x < FB_STRIDE)
        {
            if (ra[x] != rb[x]) { last = x; x++; continue; }
            int g = x;
            while (g < FB_STRIDE && g - last <= GAP_MERGE && ra[g] == rb[g]) g++;
            if (g >= FB_STRIDE || g - last > GAP_MERGE) break;
            x = g;
        }
        if (n == MAX_SPANS)
        {
            // too many pieces: widen the last one to the end of this run
            sp[n - 1].w = (uint8_t)(last + 1 - sp[n - 1].x);
        }
        else
        {
            sp[n].x = (uint8_t)s;
            sp[n].w = (uint8_t)(last + 1 - s);
            n++;
        }
        x = last + 1;
    }
    return n;
}

size_t dlist_build(const fb_t *want, fb_t *shown, uint8_t *ops, size_t budget,
                   const uint8_t *palette, bool *more)
{
    ctx_t k = { want, shown, ops, 0, budget > 1 ? budget - 1 : 0, false };  // keep 1 byte for END

    if (palette && k.used + 33 <= k.budget)
    {
        ops[0] = DL_OP_PALETTE;
        memcpy(ops + 1, palette, 32);
        k.used = 33;
    }

    rect_t open[MAX_OPEN];
    int n_open = 0;
    span_t sp[MAX_SPANS];

    for (int y = 0; y < FB_H && !k.full; y++)
    {
        int n = row_spans(want, shown, y, sp);
        bool matched[MAX_OPEN] = { false };

        for (int i = 0; i < n && !k.full; i++)
        {
            int j;
            for (j = 0; j < n_open; j++)
                if (open[j].live && !matched[j] && open[j].x == sp[i].x && open[j].w == sp[i].w &&
                    open[j].y + open[j].h == y && open[j].h < 255)
                    break;
            if (j < n_open)
            {
                open[j].h++;
                matched[j] = true;
                continue;
            }
            // a new rectangle; make room if needed
            int slot = -1;
            for (j = 0; j < n_open; j++) if (!open[j].live) { slot = j; break; }
            if (slot < 0 && n_open < MAX_OPEN) slot = n_open++;
            if (slot < 0)
            {
                // full: write the oldest that did not grow on this row
                for (j = 0; j < n_open; j++)
                    if (!matched[j]) { emit(&k, open[j].x, open[j].y, open[j].w, open[j].h); open[j].live = false; slot = j; break; }
                if (slot < 0) { emit(&k, sp[i].x, y, sp[i].w, 1); continue; }
            }
            open[slot] = (rect_t){ sp[i].x, sp[i].w, (uint8_t)y, 1, true };
            matched[slot] = true;
        }

        // rectangles that did not continue on this row are complete
        for (int j = 0; j < n_open && !k.full; j++)
            if (open[j].live && !matched[j])
            {
                emit(&k, open[j].x, open[j].y, open[j].w, open[j].h);
                open[j].live = false;
            }
    }
    for (int j = 0; j < n_open && !k.full; j++)
        if (open[j].live) emit(&k, open[j].x, open[j].y, open[j].w, open[j].h);

    ops[k.used++] = DL_OP_END;

    // anything still different is for the next list
    *more = k.full || memcmp(want->px, shown->px, sizeof want->px) != 0;
    return k.used;
}

void dlist_apply(const uint8_t *ops, size_t len, fb_t *screen, uint8_t *palette)
{
    size_t i = 0;
    while (i < len)
    {
        uint8_t op = ops[i];
        if (op == DL_OP_END) return;
        if (op == DL_OP_PALETTE)
        {
            if (palette) memcpy(palette, ops + i + 1, 32);
            i += 33;
        }
        else if (op == DL_OP_FILL)
        {
            int x = ops[i + 1], y = ops[i + 2], w = ops[i + 3], h = ops[i + 4];
            for (int yy = y; yy < y + h; yy++) memset(&screen->px[yy][x], ops[i + 5], (size_t)w);
            i += 6;
        }
        else if (op == DL_OP_COPY)
        {
            int x = ops[i + 1], y = ops[i + 2], w = ops[i + 3], h = ops[i + 4];
            const uint8_t *d = ops + i + 5;
            for (int yy = y; yy < y + h; yy++, d += w) memcpy(&screen->px[yy][x], d, (size_t)w);
            i += 5 + (size_t)w * (size_t)h;
        }
        else return;    // unknown: stop
    }
}
