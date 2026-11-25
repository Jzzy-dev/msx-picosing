// PicoSing - karaoke for the MSX PicoVerse 2350
// cover.c - a song's cover as a 96 x 96 SCREEN 5 picture (see cover.h)
//
// JPEG decoding: TJpgDec R0.03 (C) ChaN, as configured for PicoAmp.

#include "cover.h"
#include "scratch.h"
#include "platform.h"
#include "tjpgd/tjpgd.h"
#include <string.h>

static uint8_t work[TJPGD_WORKSPACE_SIZE] __attribute__((aligned(4)));
static uint8_t *rgb;                    // (the scratch buffer while decoding)
static int src_w, src_h;     // the decoded (scaled) size

static size_t in_func(JDEC *jd, uint8_t *buf, size_t n)
{
    (void)jd;
    if (buf) return plat_stream_read(buf, n);
    // skip n bytes
    uint8_t tmp[64];
    size_t done = 0;
    while (done < n)
    {
        size_t k = n - done > sizeof tmp ? sizeof tmp : n - done;
        size_t r = plat_stream_read(tmp, k);
        if (!r) break;
        done += r;
    }
    return done;
}

// Every decoded block: keep the pixels that the 96 x 96 grid samples
static int out_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    (void)jd;
    const uint8_t *p = bitmap;
    int bw = rect->right - rect->left + 1;
    for (int ty = 0; ty < COVER_PX; ty++)
    {
        int sy = (ty * 2 + 1) * src_h / (COVER_PX * 2);
        if (sy < rect->top || sy > rect->bottom) continue;
        for (int tx = 0; tx < COVER_PX; tx++)
        {
            int sx = (tx * 2 + 1) * src_w / (COVER_PX * 2);
            if (sx < rect->left || sx > rect->right) continue;
            const uint8_t *s = p + ((sy - rect->top) * bw + (sx - rect->left)) * 3;
            uint8_t *d = rgb + (ty * COVER_PX + tx) * 3;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    }
    return 1;
}

// V9938 palette entry -> 8-bit RGB
static void pal_rgb(const uint8_t *pal, int i, int *c)
{
    c[0] = ((pal[i * 2] >> 4) & 7) * 255 / 7;
    c[1] = (pal[i * 2 + 1] & 7) * 255 / 7;
    c[2] = (pal[i * 2] & 7) * 255 / 7;
}

static int dist2(const int *a, const int *b)
{
    int dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
    return 3 * dr * dr + 4 * dg * dg + 2 * db * db;
}

// k-means over the picture for k colours, starting from evenly spread pixels
static void kmeans(int k, int out[][3])
{
    for (int i = 0; i < k; i++)
    {
        int at = ((i * 2 + 1) * COVER_PX * COVER_PX / (k * 2)) * 3;
        out[i][0] = rgb[at]; out[i][1] = rgb[at + 1]; out[i][2] = rgb[at + 2];
    }
    for (int pass = 0; pass < 8; pass++)
    {
        int32_t sum[16][3] = { { 0 } };
        int32_t n[16] = { 0 };
        for (int p = 0; p < COVER_PX * COVER_PX; p += 2)
        {
            int c[3] = { rgb[p * 3], rgb[p * 3 + 1], rgb[p * 3 + 2] };
            int best = 0, bd = 1 << 30;
            for (int i = 0; i < k; i++) { int d = dist2(c, out[i]); if (d < bd) { bd = d; best = i; } }
            sum[best][0] += c[0]; sum[best][1] += c[1]; sum[best][2] += c[2]; n[best]++;
        }
        for (int i = 0; i < k; i++)
            if (n[i]) for (int j = 0; j < 3; j++) out[i][j] = (int)(sum[i][j] / n[i]);
    }
}

bool cover_load(cover_t *c, const char *path, uint32_t off, uint32_t len,
                const uint8_t base[32], uint16_t free_mask)
{
    rgb = scratch(SCR_COVER);
    c->ok = false;
    memcpy(c->pal, base, 32);
    if (!plat_stream_open(path, off, len)) return false;

    JDEC jd;
    JRESULT r = jd_prepare(&jd, in_func, work, sizeof work, NULL);
    if (r != JDR_OK) { plat_stream_close(); return false; }
    uint8_t scale = 0;
    while (scale < 3 && (jd.width >> (scale + 1)) >= COVER_PX && (jd.height >> (scale + 1)) >= COVER_PX) scale++;
    src_w = (jd.width + (1 << scale) - 1) >> scale;
    src_h = (jd.height + (1 << scale) - 1) >> scale;
    memset(rgb, 0, (size_t)COVER_PX * COVER_PX * 3);
    r = jd_decomp(&jd, out_func, scale);
    plat_stream_close();
    if (r != JDR_OK) return false;

    // the picture's own colours, in the V9938's 512
    int nfree = 0, slots[16];
    for (int i = 0; i < 16; i++) if (free_mask & (1u << i)) slots[nfree++] = i;
    if (nfree)
    {
        int cols[16][3];
        kmeans(nfree, cols);
        for (int i = 0; i < nfree; i++)
        {
            int r3 = (cols[i][0] * 7 + 127) / 255, g3 = (cols[i][1] * 7 + 127) / 255, b3 = (cols[i][2] * 7 + 127) / 255;
            c->pal[slots[i] * 2] = (uint8_t)((r3 << 4) | b3);
            c->pal[slots[i] * 2 + 1] = (uint8_t)g3;
        }
    }

    // Floyd-Steinberg with all 16 entries
    int pal[16][3];
    for (int i = 0; i < 16; i++) pal_rgb(c->pal, i, pal[i]);
    static int16_t err[2][COVER_PX + 2][3];
    memset(err, 0, sizeof err);
    for (int y = 0; y < COVER_PX; y++)
    {
        int16_t (*cur)[3] = err[y & 1], (*nxt)[3] = err[(y + 1) & 1];
        memset(nxt, 0, sizeof err[0]);
        for (int x = 0; x < COVER_PX; x++)
        {
            const uint8_t *s = rgb + (y * COVER_PX + x) * 3;
            int want[3];
            for (int j = 0; j < 3; j++)
            {
                int v = s[j] + cur[x + 1][j] / 16;
                want[j] = v < 0 ? 0 : v > 255 ? 255 : v;
            }
            int best = 0, bd = 1 << 30;
            for (int i = 0; i < 16; i++) { int d = dist2(want, pal[i]); if (d < bd) { bd = d; best = i; } }
            c->idx[y * COVER_PX + x] = (uint8_t)best;
            for (int j = 0; j < 3; j++)
            {
                int e = want[j] - pal[best][j];
                cur[x + 2][j] = (int16_t)(cur[x + 2][j] + e * 7);
                nxt[x][j]     = (int16_t)(nxt[x][j] + e * 3);
                nxt[x + 1][j] = (int16_t)(nxt[x + 1][j] + e * 5);
                nxt[x + 2][j] = (int16_t)(nxt[x + 2][j] + e);
            }
        }
    }
    c->ok = true;
    return true;
}

void cover_draw(fb_t *f, const cover_t *c, int x, int y)
{
    if (!c->ok) return;
    for (int yy = 0; yy < COVER_PX; yy++)
        for (int xx = 0; xx < COVER_PX; xx++)
            fb_pixel(f, x + xx, y + yy, c->idx[yy * COVER_PX + xx]);
}
