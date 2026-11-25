// PicoSing - test_id3: change MP3 tags and check nothing else changed
// usage: test_id3 <an mp3> <work folder>
#include "../pico/game/id3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint8_t *load(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n + 1);
    if (fread(b, 1, *n, f) != *n) { free(b); fclose(f); return NULL; }
    fclose(f);
    return b;
}
static void save(const char *p, const uint8_t *b, size_t n) { FILE *f = fopen(p, "wb"); fwrite(b, 1, n, f); fclose(f); }
static size_t frame(uint8_t *o, const char *id, const uint8_t *d, size_t n, int ver)
{
    memcpy(o, id, 4);
    if (ver == 4) { o[4] = (n >> 21) & 127; o[5] = (n >> 14) & 127; o[6] = (n >> 7) & 127; o[7] = n & 127; }
    else { o[4] = (uint8_t)(n >> 24); o[5] = (uint8_t)(n >> 16); o[6] = (uint8_t)(n >> 8); o[7] = (uint8_t)n; }
    o[8] = o[9] = 0;
    memcpy(o + 10, d, n);
    return 10 + n;
}
static uint32_t ss(const uint8_t *p) { return (uint32_t)(p[0] & 127) << 21 | (uint32_t)(p[1] & 127) << 14 | (uint32_t)(p[2] & 127) << 7 | (p[3] & 127); }

static uint8_t apic[3000];

// a file with a tag (ver 3 or 4) holding old text, a picture and a comment, padding pad
static size_t make(uint8_t *o, const uint8_t *audio, size_t an, int ver, size_t pad, bool v1)
{
    size_t n = 10;
    static const uint8_t t16[] = { 1, 0xFF, 0xFE, 'O', 0, 'l', 0, 'd', 0, ' ', 0, 0xE9, 0 };   // "Old é", UTF-16
    n += frame(o + n, "TIT2", t16, sizeof t16, ver);
    n += frame(o + n, "TPE1", (const uint8_t *)"\0Old artist", 11, ver);
    for (int i = 0; i < 3000; i++) apic[i] = (uint8_t)(i * 7 + 3);
    n += frame(o + n, "APIC", apic, sizeof apic, ver);
    n += frame(o + n, "COMM", (const uint8_t *)"\0engKeep me", 11, ver);
    memset(o + n, 0, pad);
    n += pad;
    uint32_t tag = (uint32_t)(n - 10);
    memcpy(o, "ID3", 3); o[3] = (uint8_t)ver; o[4] = 0; o[5] = 0;
    o[6] = (tag >> 21) & 127; o[7] = (tag >> 14) & 127; o[8] = (tag >> 7) & 127; o[9] = tag & 127;
    memcpy(o + n, audio, an);
    n += an;
    if (v1)
    {
        memset(o + n, 0, 128);
        memcpy(o + n, "TAG", 3);
        memcpy(o + n + 3, "Old title v1", 12);
        n += 128;
    }
    return n;
}

// the edited file: its tag keeps the picture and comment, then the same audio
static void verify(const char *p, const uint8_t *audio, size_t an, bool v1, const char *what)
{
    size_t n;
    uint8_t *b = load(p, &n);
    CHECK(b && !memcmp(b, "ID3", 3), "%s: a tag", what);
    if (!b) return;
    size_t at = 10 + ss(b + 6);
    CHECK(n >= at + an && !memcmp(b + at, audio, an), "%s: the audio is unchanged", what);
    CHECK(memmem(b, at, apic, sizeof apic) != NULL, "%s: the picture is kept", what);
    CHECK(memmem(b, at, "Keep me", 7) != NULL, "%s: the comment is kept", what);
    if (v1) CHECK(n == at + an + 128 && !memcmp(b + n - 128, "TAG", 3) && !memcmp(b + n - 125, "New Title", 9), "%s: ID3v1 updated", what);
    free(b);
}

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: test_id3 <mp3 without tags> <work folder>\n"); return 2; }
    size_t an;
    uint8_t *audio = load(argv[1], &an);
    if (!audio) { printf("cannot read %s\n", argv[1]); return 2; }
    if (!memcmp(audio, "ID3", 3))       // only the audio: drop a tag it has
    {
        size_t skip = 10 + ss(audio + 6);
        memmove(audio, audio + skip, an - skip);
        an -= skip;
    }
    uint8_t *f = malloc(an + 20000);
    char p[600], tmp[620];
    id3_text_t t;

    for (int ver = 3; ver <= 4; ver++)
    {
        // room in the tag: changed in place
        snprintf(p, sizeof p, "%s/v%d.mp3", argv[2], ver);
        size_t n = make(f, audio, an, ver, 200, true);
        save(p, f, n);
        CHECK(id3_read(p, &t) && !strcmp(t.title, "Old \xE9") && !strcmp(t.artist, "Old artist") && t.had_v1,
              "v2.%d read: '%s' '%s'", ver, t.title, t.artist);
        snprintf(t.title, sizeof t.title, "New Title");
        snprintf(t.artist, sizeof t.artist, "New Artist");
        snprintf(t.album, sizeof t.album, "An Album");
        snprintf(t.year, sizeof t.year, "1993");
        const char *e = id3_write(p, &t);
        CHECK(!e, "v2.%d write in place: %s", ver, e ? e : "");
        size_t n2; uint8_t *b = load(p, &n2);
        CHECK(n2 == n, "v2.%d in place: same size (%zu %zu)", ver, n, n2);
        free(b);
        verify(p, audio, an, true, ver == 3 ? "v2.3 in place" : "v2.4 in place");
        id3_text_t r;
        CHECK(id3_read(p, &r) && !strcmp(r.title, "New Title") && !strcmp(r.artist, "New Artist") &&
              !strcmp(r.album, "An Album") && !strcmp(r.year, "1993"), "v2.%d read back: '%s' '%s' '%s' '%s'", ver, r.title, r.artist, r.album, r.year);

        // no room: a new file
        n = make(f, audio, an, ver, 0, true);
        save(p, f, n);
        id3_read(p, &t);
        snprintf(t.title, sizeof t.title, "New Title and a much longer name here");
        snprintf(t.album, sizeof t.album, "An Album With A Long Name Too, Deluxe Edition");
        e = id3_write(p, &t);
        CHECK(!e, "v2.%d write, new file: %s", ver, e ? e : "");
        verify(p, audio, an, true, ver == 3 ? "v2.3 new file" : "v2.4 new file");
        snprintf(tmp, sizeof tmp, "%s.tmp", p);
        CHECK(fopen(tmp, "rb") == NULL, "no .tmp left");
        CHECK(id3_read(p, &r) && !strcmp(r.title, t.title) && !strcmp(r.album, t.album), "v2.%d long read back", ver);
        // and again: now it fits
        snprintf(t.title, sizeof t.title, "Short");
        size_t before; free(load(p, &before));
        CHECK(!id3_write(p, &t), "rewrite");
        size_t after; free(load(p, &after));
        CHECK(before == after, "the second change fits in the padding (%zu %zu)", before, after);
    }

    // no tag at all
    snprintf(p, sizeof p, "%s/none.mp3", argv[2]);
    save(p, audio, an);
    CHECK(id3_read(p, &t) && !t.title[0] && !t.had_v2, "no tag read");
    snprintf(t.title, sizeof t.title, "Twinkle");
    snprintf(t.artist, sizeof t.artist, "Traditional");
    CHECK(!id3_write(p, &t), "write without a tag");
    size_t n; uint8_t *b = load(p, &n);
    size_t at = 10 + ss(b + 6);
    CHECK(n == at + an && !memcmp(b + at, audio, an), "audio unchanged after adding a tag");
    free(b);
    id3_text_t r;
    CHECK(id3_read(p, &r) && !strcmp(r.title, "Twinkle") && !strcmp(r.artist, "Traditional"), "new tag read back");
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails != 0;
}
