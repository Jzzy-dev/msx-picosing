// PicoSing - karaoke for the MSX PicoVerse 2350
// id3.c - read and change an MP3's title, artist, album and year
//
// Writing keeps every other frame of the ID3v2 tag (the cover picture, the
// lyrics...). When the new frames fit in the old tag (tags usually carry
// padding), the tag is rewritten in place. Otherwise the file is written
// again with a bigger tag: "<name>.tmp", then it takes the old file's place.
// An ID3v1 tag at the end, when there is one, gets the new text too.

#include "id3.h"
#include "scratch.h"
#include "platform.h"
#include "song.h"
#include <string.h>
#include <stdio.h>

#define BUF      8192
#define MAX_KEEP 96

static uint8_t *buf;                    // (the scratch buffer)

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t syncsafe(const uint8_t *p) { return (uint32_t)(p[0] & 127) << 21 | (uint32_t)(p[1] & 127) << 14 | (uint32_t)(p[2] & 127) << 7 | (p[3] & 127); }
static void put_syncsafe(uint8_t *p, uint32_t v) { p[0] = (v >> 21) & 127; p[1] = (v >> 14) & 127; p[2] = (v >> 7) & 127; p[3] = v & 127; }
static void put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static bool is_ours(const uint8_t *id)
{
    return !memcmp(id, "TIT2", 4) || !memcmp(id, "TPE1", 4) || !memcmp(id, "TALB", 4) ||
           !memcmp(id, "TYER", 4) || !memcmp(id, "TDRC", 4);
}

// a text frame's body (encoding byte first) as display text
static void text_of(const uint8_t *d, uint32_t n, char *out, size_t size)
{
    out[0] = 0;
    if (n < 2) return;
    uint8_t enc = d[0];
    d++; n--;
    char tmp[320];
    size_t k = 0;
    if (enc == 1 || enc == 2)
    {
        // UTF-16 (the byte-order mark decides the order) to UTF-8, then as the rest
        bool le = enc == 1;
        if (n >= 2 && d[0] == 0xFF && d[1] == 0xFE) { le = true; d += 2; n -= 2; }
        else if (n >= 2 && d[0] == 0xFE && d[1] == 0xFF) { le = false; d += 2; n -= 2; }
        for (uint32_t i = 0; i + 1 < n && k + 4 < sizeof tmp; i += 2)
        {
            uint32_t c = le ? (uint32_t)(d[i] | d[i + 1] << 8) : (uint32_t)(d[i] << 8 | d[i + 1]);
            if (!c) break;
            if (c >= 0xD800 && c < 0xDC00 && i + 3 < n)
            {
                // a surrogate pair (an emoji, say)
                uint32_t c2 = le ? (uint32_t)(d[i + 2] | d[i + 3] << 8) : (uint32_t)(d[i + 2] << 8 | d[i + 3]);
                if (c2 >= 0xDC00 && c2 < 0xE000) { c = 0x10000u + ((c - 0xD800u) << 10) + (c2 - 0xDC00u); i += 2; }
            }
            if (c < 0x80) tmp[k++] = (char)c;
            else if (c < 0x800) { tmp[k++] = (char)(0xC0 | c >> 6); tmp[k++] = (char)(0x80 | (c & 0x3F)); }
            else if (c < 0x10000) { tmp[k++] = (char)(0xE0 | c >> 12); tmp[k++] = (char)(0x80 | (c >> 6 & 0x3F)); tmp[k++] = (char)(0x80 | (c & 0x3F)); }
            else { tmp[k++] = (char)(0xF0 | c >> 18); tmp[k++] = (char)(0x80 | (c >> 12 & 0x3F)); tmp[k++] = (char)(0x80 | (c >> 6 & 0x3F)); tmp[k++] = (char)(0x80 | (c & 0x3F)); }
        }
        tmp[k] = 0;
        song_to_latin1(out, size, tmp, k);
        return;
    }
    while (k < n && k < sizeof tmp - 1 && d[k]) { tmp[k] = (char)d[k]; k++; }
    tmp[k] = 0;
    song_to_latin1(out, size, tmp, k);   // UTF-8 or Latin-1
}

typedef struct {
    uint32_t tag_size;      // after the 10-byte header, 0 = no tag
    uint8_t  ver;           // 3 or 4
    bool     raw;           // unsynchronised or old: frames can't be copied
    uint32_t audio;         // where the audio starts
} tag_t;

static bool read_header(int h, tag_t *t)
{
    uint8_t hd[10];
    memset(t, 0, sizeof *t);
    t->ver = 3;
    if (!plat_fseek(h, 0) || plat_fread(h, hd, 10) != 10) return false;
    if (memcmp(hd, "ID3", 3)) return true;
    t->tag_size = syncsafe(hd + 6);
    t->ver = hd[3];
    t->audio = 10 + t->tag_size + ((t->ver == 4 && (hd[5] & 0x10)) ? 10 : 0);
    if (t->ver < 3 || t->ver > 4 || (hd[5] & 0x80)) { t->raw = true; if (t->ver != 4) t->ver = 3; }
    if (hd[5] & 0x40 && !t->raw)
    {
        // an extended header: skip it
        uint8_t eh[4];
        if (plat_fread(h, eh, 4) != 4) return false;
        uint32_t es = t->ver == 4 ? syncsafe(eh) : be32(eh) + 4;
        plat_fseek(h, 10 + es);
    }
    return true;
}

bool id3_read(const char *path, id3_text_t *out)
{
    buf = scratch(SCR_ID3);
    memset(out, 0, sizeof *out);
    int h = plat_fopen(path, false);
    if (h < 0) return false;
    tag_t t;
    bool ok = read_header(h, &t);
    if (ok && t.tag_size && !t.raw)
    {
        uint32_t end = 10 + t.tag_size;
        // the frames start where read_header left the file
        uint32_t pos = 10;
        uint8_t fh[10];
        plat_fseek(h, 0);
        plat_fread(h, fh, 10);
        if (fh[5] & 0x40)
        {
            uint8_t eh[4];
            plat_fread(h, eh, 4);
            pos = 10 + (t.ver == 4 ? syncsafe(eh) : be32(eh) + 4);
        }
        while (pos + 10 <= end)
        {
            plat_fseek(h, pos);
            if (plat_fread(h, fh, 10) != 10 || fh[0] == 0) break;
            uint32_t n = t.ver == 4 ? syncsafe(fh + 4) : be32(fh + 4);
            if (n > end - pos - 10) break;
            char *dst = NULL;
            size_t dsz = 0;
            if (!memcmp(fh, "TIT2", 4)) { dst = out->title; dsz = sizeof out->title; }
            else if (!memcmp(fh, "TPE1", 4)) { dst = out->artist; dsz = sizeof out->artist; }
            else if (!memcmp(fh, "TALB", 4)) { dst = out->album; dsz = sizeof out->album; }
            else if (!memcmp(fh, "TYER", 4) || !memcmp(fh, "TDRC", 4)) { dst = out->year; dsz = sizeof out->year; }
            if (dst && n < 300 && plat_fread(h, buf, n) == n)
            {
                text_of(buf, n, dst, dsz);
                out->had_v2 = true;
            }
            pos += 10 + n;
        }
    }
    // ID3v1 fills what is still empty
    uint32_t size = plat_fsize(h);
    if (size >= 128 && plat_fseek(h, size - 128) && plat_fread(h, buf, 128) == 128 && !memcmp(buf, "TAG", 3))
    {
        out->had_v1 = true;
        char tmp[32];
        struct { char *d; size_t n; int at, len; } f[4] = {
            { out->title, sizeof out->title, 3, 30 }, { out->artist, sizeof out->artist, 33, 30 },
            { out->album, sizeof out->album, 63, 30 }, { out->year, sizeof out->year, 93, 4 } };
        for (int i = 0; i < 4; i++)
        {
            if (f[i].d[0]) continue;
            memcpy(tmp, buf + f[i].at, (size_t)f[i].len);
            tmp[f[i].len] = 0;
            int k = (int)strlen(tmp);
            while (k > 0 && tmp[k - 1] == ' ') tmp[--k] = 0;
            song_to_latin1(f[i].d, f[i].n, tmp, (size_t)k);
        }
    }
    plat_fclose(h);
    return ok;
}

// our four frames, for tag version ver
static uint32_t make_frames(uint8_t *p, const id3_text_t *t, uint8_t ver)
{
    const char *val[4] = { t->title, t->artist, t->album, t->year };
    const char *ids[4] = { "TIT2", "TPE1", "TALB", ver == 4 ? "TDRC" : "TYER" };
    uint32_t n = 0;
    for (int i = 0; i < 4; i++)
    {
        size_t len = strlen(val[i]);
        if (!len) continue;
        memcpy(p + n, ids[i], 4);
        if (ver == 4) put_syncsafe(p + n + 4, (uint32_t)len + 1);
        else put_be32(p + n + 4, (uint32_t)len + 1);
        p[n + 8] = p[n + 9] = 0;
        p[n + 10] = 0;                          // ISO-8859-1
        memcpy(p + n + 11, val[i], len);
        n += 11 + (uint32_t)len;
    }
    return n;
}

static bool copy(int from, uint32_t src, int to, uint32_t dst, uint32_t n)
{
    while (n)
    {
        uint32_t k = n > BUF ? BUF : n;
        if (!plat_fseek(from, src) || plat_fread(from, buf, k) != k) return false;
        if (!plat_fseek(to, dst) || plat_fwrite(to, buf, k) != k) return false;
        src += k; dst += k; n -= k;
    }
    return true;
}

static bool zeros(int h, uint32_t at, uint32_t n)
{
    memset(buf, 0, n < BUF ? n : BUF);
    if (!plat_fseek(h, at)) return false;
    while (n)
    {
        uint32_t k = n > BUF ? BUF : n;
        if (plat_fwrite(h, buf, k) != k) return false;
        n -= k;
    }
    return true;
}

static void write_v1(const char *path, const id3_text_t *t)
{
    int h = plat_fopen(path, true);
    if (h < 0) return;
    uint32_t size = plat_fsize(h);
    uint8_t v1[128];
    if (size >= 128 && plat_fseek(h, size - 128) && plat_fread(h, v1, 128) == 128 && !memcmp(v1, "TAG", 3))
    {
        const char *val[4] = { t->title, t->artist, t->album, t->year };
        static const int at[4] = { 3, 33, 63, 93 }, len[4] = { 30, 30, 30, 4 };
        for (int i = 0; i < 4; i++)
        {
            memset(v1 + at[i], 0, (size_t)len[i]);
            size_t n = strlen(val[i]);
            memcpy(v1 + at[i], val[i], n < (size_t)len[i] ? n : (size_t)len[i]);
        }
        plat_fseek(h, size - 128);
        plat_fwrite(h, v1, 128);
    }
    plat_fclose(h);
}

const char *id3_write(const char *path, const id3_text_t *t)
{
    buf = scratch(SCR_ID3);
    static struct { uint32_t at, len; } keep[MAX_KEEP];
    int nk = 0;
    int h = plat_fopen(path, true);
    if (h < 0) return "CANNOT OPEN THE MP3";
    tag_t tg;
    if (!read_header(h, &tg)) { plat_fclose(h); return "CANNOT READ THE MP3"; }

    // the frames to keep
    uint32_t kept = 0;
    if (tg.tag_size && !tg.raw)
    {
        uint8_t fh[10];
        uint32_t end = 10 + tg.tag_size;
        plat_fseek(h, 0);
        plat_fread(h, fh, 10);
        uint32_t pos = 10;
        if (fh[5] & 0x40)
        {
            uint8_t eh[4];
            plat_fread(h, eh, 4);
            pos = 10 + (tg.ver == 4 ? syncsafe(eh) : be32(eh) + 4);
        }
        while (pos + 10 <= end)
        {
            plat_fseek(h, pos);
            if (plat_fread(h, fh, 10) != 10 || fh[0] == 0) break;
            uint32_t n = tg.ver == 4 ? syncsafe(fh + 4) : be32(fh + 4);
            if (n > end - pos - 10) break;
            if (!is_ours(fh))
            {
                if (nk == MAX_KEEP) { plat_fclose(h); return "TOO MANY TAGS IN THE MP3"; }
                keep[nk].at = pos;
                keep[nk].len = 10 + n;
                kept += 10 + n;
                nk++;
            }
            pos += 10 + n;
        }
    }
    uint8_t ours[4 * 80];
    uint32_t n_ours = make_frames(ours, t, tg.ver);
    uint32_t need = kept + n_ours;
    uint8_t hd[10] = { 'I', 'D', '3', tg.ver, 0, 0 };

    if (tg.tag_size && need <= tg.tag_size)
    {
        // in place: the kept frames move up (never down), then ours, then padding
        uint32_t dst = 10;
        for (int i = 0; i < nk; i++)
        {
            if (keep[i].at != dst && !copy(h, keep[i].at, h, dst, keep[i].len)) { plat_fclose(h); return "WRITING FAILED"; }
            dst += keep[i].len;
        }
        bool ok = plat_fseek(h, dst) && plat_fwrite(h, ours, n_ours) == n_ours;
        dst += n_ours;
        ok = ok && zeros(h, dst, 10 + tg.tag_size - dst);
        put_syncsafe(hd + 6, tg.tag_size);
        ok = ok && plat_fseek(h, 0) && plat_fwrite(h, hd, 10) == 10;
        plat_fclose(h);
        if (!ok) return "WRITING FAILED";
        write_v1(path, t);
        return NULL;
    }

    // a new file with room to spare
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    plat_remove(tmp);
    int o = plat_fopen(tmp, true);
    if (o < 0) { plat_fclose(h); return "CANNOT WRITE ON THE CARD"; }
    uint32_t tag = need + 2048;
    put_syncsafe(hd + 6, tag);
    uint32_t size = plat_fsize(h);
    bool ok = plat_fwrite(o, hd, 10) == 10;
    uint32_t dst = 10;
    for (int i = 0; ok && i < nk; i++)
    {
        ok = copy(h, keep[i].at, o, dst, keep[i].len);
        dst += keep[i].len;
    }
    ok = ok && plat_fseek(o, dst) && plat_fwrite(o, ours, n_ours) == n_ours;
    dst += n_ours;
    ok = ok && zeros(o, dst, 10 + tag - dst);
    if (ok && size > tg.audio) ok = copy(h, tg.audio, o, 10 + tag, size - tg.audio);
    plat_fclose(h);
    plat_fclose(o);
    if (!ok) { plat_remove(tmp); return "WRITING FAILED (CARD FULL?)"; }
    if (!plat_remove(path)) { plat_remove(tmp); return "CANNOT REPLACE THE MP3"; }
    if (!plat_rename(tmp, path)) return "RENAME FAILED: SEE .TMP";
    write_v1(path, t);
    return NULL;
}
