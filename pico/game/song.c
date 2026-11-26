// PicoSing - karaoke for the MSX PicoVerse 2350
// song.c - UltraStar TXT songs
//
// Format: https://usdx.eu/format (tags, ':' '*' 'F' 'R' 'G' notes, '-' line
// breaks, P1/P2 duets, #RELATIVE). One beat lasts 60000 / (BPM * 4) ms,
// counted from #GAP.

#include "song.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ---------------------------------------------------------------- text
static bool valid_utf8(const unsigned char *p, size_t n)
{
    size_t i = 0;
    while (i < n)
    {
        unsigned char c = p[i];
        int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (extra < 0) return false;
        if (i + (size_t)extra >= n + (extra ? 0 : 1)) return false;
        for (int k = 1; k <= extra; k++)
            if ((p[i + k] & 0xC0) != 0x80) return false;
        i += (size_t)extra + 1;
    }
    return true;
}

static bool g_utf8 = true;

// Windows-1252's 0x80-0x9F (a Latin-1 .txt is often that)
static uint32_t cp1252(unsigned char b)
{
    static const uint16_t w[32] = {
        0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
        0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178 };
    return b >= 0x80 && b < 0xA0 ? w[b - 0x80] : b;
}

// a letter outside Latin-1 as the nearest one in it (the font has Latin-1):
// Latin Extended-A (Polish, Czech, Hungarian, Turkish ...) without its accent,
// Cyrillic and Greek letter by letter, the typographic signs as plain ones;
// 0: left out (zero width, emoji). The same tables as PicoAmp's engine.
static const char lat_a[] =                     // U+0100 - U+017F
    "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
static const char cyr[] =                       // U+0410 - U+044F
    "ABVGDEZZIJKLMNOPRSTUFHCCSS'Y'EUAabvgdezzijklmnoprstufhccss'y'eua";
static const char grk[] =                       // U+0391 - U+03C9
    "ABGDEZITIKLMNXOPR?STYFHPOiyaeiiyabgdezitiklmnxoprsstyfhpo";

char song_fold_cp(uint32_t c)
{
    if (c == '\t') return ' ';
    if (c < 0x20 || c == 0x7F) return c ? '?' : 0;
    if (c < 0x7F || (c >= 0xA0 && c <= 0xFF)) return c == 0xAD ? 0 : (char)c;
    if (c >= 0x100 && c < 0x180) return lat_a[c - 0x100];
    if (c == 0x178) return 'Y';
    if (c == 0x192) return 'f';
    if (c == 0x218 || c == 0x21A) return c == 0x218 ? 'S' : 'T';         // Ș Ț
    if (c == 0x219 || c == 0x21B) return c == 0x219 ? 's' : 't';
    if (c == 0x2C6) return '^';
    if (c == 0x2DC) return '~';
    if (c == 0x401) return (char)0xCB;                                   // Ё: Ë
    if (c == 0x451) return (char)0xEB;
    if (c >= 0x410 && c < 0x450) return cyr[c - 0x410];
    if (c == 0x404 || c == 0x454) return c == 0x404 ? 'E' : 'e';
    if (c == 0x406 || c == 0x407) return c == 0x406 ? 'I' : (char)0xCF;  // І Ї
    if (c == 0x456 || c == 0x457) return c == 0x456 ? 'i' : (char)0xEF;
    if (c >= 0x391 && c <= 0x3C9) return grk[c - 0x391];
    if (c >= 0x386 && c <= 0x38F) return "A.EII?O?YO"[c - 0x386];
    if (c >= 0x3AC && c <= 0x3B0) return "aeiiy"[c - 0x3AC];
    if (c >= 0x3CA && c <= 0x3CE) return "iyoyo"[c - 0x3CA];
    if (c == 0x1E9E) return (char)0xDF;                                  // ẞ
    if (c >= 0x1EA0 && c <= 0x1EF9)                                       // Vietnamese: the vowel
        return "AaAaAaAaAaAaAaAaAaAaAaAaEeEeEeEeEeEeEeEeIiIiOoOoOoOoOoOoOoOoOoOoOoOoUuUuUuUuUuUuUuYyYyYyYy"[c - 0x1EA0];
    if (c == 0x2022 || c == 0x2027 || c == 0x2020 || c == 0x2021) return '*';
    if (c == 0x2032 || c == 0x2018 || c == 0x2019 || c == 0x201B || c == 0x2039 || c == 0x203A) return '\'';
    if (c == 0x201A) return ',';
    if (c == 0x201C || c == 0x201D || c == 0x201E || c == 0x201F || c == 0x2033) return '"';
    if (c >= 0x2010 && c <= 0x2015) return '-';
    if (c == 0x2026) return '.';
    if (c == 0x2030) return '%';
    if (c == 0x20AC) return 'E';
    if (c == 0x2122) return 0;
    if (c == 0x266A || c == 0x266B || c == 0x266C) return '*';          // ♪ ♫ (in some lyrics)
    if (c == 0x200B || c == 0x200C || c == 0x200D || c == 0x2060 || c == 0xFEFF || (c >= 0xFE00 && c <= 0xFE0F)) return 0;
    if ((c >= 0x2190 && c <= 0x21FF) || (c >= 0x2300 && c <= 0x23FF) || (c >= 0x25A0 && c <= 0x27BF) ||
        (c >= 0x2B00 && c <= 0x2BFF) || c >= 0x1F000) return 0;
    if (c >= 0x2000 && c <= 0x200A) return ' ';
    if (c >= 0x300 && c <= 0x36F) return 0;                              // a combining accent: the letter before stays
    return '?';
}

static size_t to_latin1(char *dst, size_t size, const char *src, size_t len, bool utf8)
{
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)src;
    size_t i = 0;
    while (i < len && o + 1 < size)
    {
        unsigned char c = p[i];
        if (c == '\r' || c == '\n') break;
        if (!utf8 || c < 0x80)
        {
            char f = song_fold_cp(utf8 ? c : cp1252(c));
            if (f) dst[o++] = f;
            i++;
            continue;
        }
        uint32_t cp;
        int extra;
        if ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { i++; dst[o++] = '?'; continue; }
        if (i + (size_t)extra >= len + 1) break;
        for (int k = 1; k <= extra && i + (size_t)k < len; k++) cp = (cp << 6) | (p[i + k] & 0x3F);
        i += (size_t)extra + 1;
        char f = song_fold_cp(cp);
        if (f) dst[o++] = f;
    }
    if (size) dst[o < size ? o : size - 1] = 0;
    return o;
}

size_t song_to_latin1(char *dst, size_t size, const char *src, size_t len)
{
    return to_latin1(dst, size, src, len, valid_utf8((const unsigned char *)src, len));
}

// ---------------------------------------------------------------- numbers
static const char *skip_sp(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\t')) p++;
    return p;
}

static bool parse_int(const char **pp, const char *e, long *out)
{
    const char *p = skip_sp(*pp, e);
    bool neg = false;
    if (p < e && (*p == '-' || *p == '+')) { neg = (*p == '-'); p++; }
    if (p >= e || !isdigit((unsigned char)*p)) return false;
    long v = 0;
    while (p < e && isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); p++; }
    *out = neg ? -v : v;
    *pp = p;
    return true;
}

// "300", "300.5" or "300,5"
static float parse_float(const char *p, const char *e)
{
    p = skip_sp(p, e);
    bool neg = false;
    if (p < e && *p == '-') { neg = true; p++; }
    float v = 0, scale = 0;
    for (; p < e; p++)
    {
        if (isdigit((unsigned char)*p))
        {
            if (scale == 0) v = v * 10 + (float)(*p - '0');
            else { v += (float)(*p - '0') * scale; scale *= 0.1f; }
        }
        else if ((*p == '.' || *p == ',') && scale == 0) scale = 0.1f;
        else break;
    }
    return neg ? -v : v;
}

// ---------------------------------------------------------------- parser
static bool tag_is(const char *p, const char *e, const char *tag, const char **val)
{
    size_t n = strlen(tag);
    if ((size_t)(e - p) < n + 2 || p[0] != '#') return false;
    for (size_t i = 0; i < n; i++)
        if (toupper((unsigned char)p[1 + i]) != tag[i]) return false;
    if (p[1 + n] != ':') return false;
    *val = p + 2 + n;
    return true;
}

static void set_str(char *dst, size_t size, const char *v, const char *e)
{
    v = skip_sp(v, e);
    while (e > v && (e[-1] == ' ' || e[-1] == '\t')) e--;
    to_latin1(dst, size, v, (size_t)(e - v), g_utf8);
}

// The YouTube video's id in #VIDEO: "v=ID,co=...,bg=..." (USDB), or a link
// (watch?v=ID, youtu.be/ID, embed/ID). Ids are 11 characters: A-Z a-z 0-9 - _
static void youtube_id(char *out, size_t size, const char *v, const char *e)
{
    out[0] = 0;
    static const char *marks[] = { "v=", "youtu.be/", "embed/", "shorts/" };
    for (int m = 0; m < 4; m++)
    {
        size_t ml = strlen(marks[m]);
        for (const char *p = v; p + ml <= e; p++)
        {
            if (strncmp(p, marks[m], ml)) continue;
            // "v=" only at the start of the value or of a field (",v=", "?v=", "&v=")
            if (m == 0 && p != v && p[-1] != ',' && p[-1] != '?' && p[-1] != '&') continue;
            const char *q = p + ml;
            size_t n = 0;
            while (q + n < e && (isalnum((unsigned char)q[n]) || q[n] == '-' || q[n] == '_')) n++;
            if (n == 11 && n < size) { memcpy(out, q, n); out[n] = 0; return; }
        }
    }
}

bool song_parse(const char *text, size_t len, song_t *s, bool header_only)
{
    memset(s, 0, offsetof(song_t, lines));
    s->n_lines = 0;
    s->n_notes = 0;
    s->pool_used = 0;
    s->bad_lines = 0;
    s->n_tracks = 1;

    const char *p = text, *end = text + len;
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    g_utf8 = valid_utf8((const unsigned char *)p, (size_t)(end - p));

    bool have_bpm = false, in_body = false;
    int track = 0;
    int32_t rel_base = 0;
    bool line_open = false;
    line_t *cur = NULL;
    bool seen_track[2] = { false, false };

    while (p < end)
    {
        const char *e = p;
        while (e < end && *e != '\n' && *e != '\r') e++;
        const char *next = e;
        while (next < end && (*next == '\n' || *next == '\r')) next++;

        const char *q = skip_sp(p, e);
        const char *v;
        if (q >= e) { p = next; continue; }          // empty line

        if (*q == '#' && !in_body)
        {
            if (tag_is(q, e, "TITLE", &v))        set_str(s->title, sizeof s->title, v, e);
            else if (tag_is(q, e, "ARTIST", &v))  set_str(s->artist, sizeof s->artist, v, e);
            else if (tag_is(q, e, "AUDIO", &v))   set_str(s->audio, sizeof s->audio, v, e);
            else if (tag_is(q, e, "MP3", &v))     { if (!s->audio[0]) set_str(s->audio, sizeof s->audio, v, e); }
            else if (tag_is(q, e, "GENRE", &v))   set_str(s->genre, sizeof s->genre, v, e);
            else if (tag_is(q, e, "LANGUAGE", &v)) set_str(s->language, sizeof s->language, v, e);
            else if (tag_is(q, e, "COVER", &v))   set_str(s->cover, sizeof s->cover, v, e);
            else if (tag_is(q, e, "BACKGROUND", &v)) set_str(s->background, sizeof s->background, v, e);
            else if (tag_is(q, e, "PREVIEWSTART", &v)) s->preview_ms = (int32_t)(parse_float(v, e) * 1000.0f);
            else if (tag_is(q, e, "VIDEOGAP", &v)) s->videogap_ms = (int32_t)(parse_float(v, e) * 1000.0f);
            else if (tag_is(q, e, "VIDEO", &v)) youtube_id(s->youtube, sizeof s->youtube, v, e);
            else if (tag_is(q, e, "YEAR", &v))    s->year = (int)parse_float(v, e);
            else if (tag_is(q, e, "BPM", &v))     { s->bpm = parse_float(v, e); have_bpm = s->bpm > 0; }
            else if (tag_is(q, e, "GAP", &v))     s->gap_ms = (int32_t)parse_float(v, e);
            else if (tag_is(q, e, "START", &v))   s->start_ms = (int32_t)(parse_float(v, e) * 1000.0f);
            else if (tag_is(q, e, "END", &v))     s->end_ms = (int32_t)parse_float(v, e);
            else if (tag_is(q, e, "RELATIVE", &v))
            {
                v = skip_sp(v, e);
                s->relative = (e - v >= 3) && toupper((unsigned char)v[0]) == 'Y';
            }
            p = next;
            continue;
        }

        char c = *q;
        if (!in_body)
        {
            if (!have_bpm) return false;              // no #BPM: not an UltraStar file
            in_body = true;
            if (header_only) return true;
            s->tracks[0].first_line = 0;
        }

        if (c == 'E') break;

        if (c == 'P' && q + 1 < e && isdigit((unsigned char)q[1]))
        {
            int t = q[1] - '0';
            int nt = (t == 2) ? 1 : 0;                 // P1, P3 (both) -> track 0; P2 -> track 1
            if (nt != track || !seen_track[nt])
            {
                if (seen_track[nt] && nt < track) s->bad_lines++;   // going back: not supported
                track = nt;
                if (track == 1)
                {
                    s->duet = true;
                    s->n_tracks = 2;
                    s->tracks[1].first_line = s->n_lines;
                }
                rel_base = 0;
                line_open = false;
            }
            seen_track[nt] = true;
            p = next;
            continue;
        }

        if (c == '-')
        {
            const char *r = q + 1;
            long a = 0, b = 0;
            bool has_a = parse_int(&r, e, &a);
            bool has_b = parse_int(&r, e, &b);
            if (s->relative && has_a) rel_base += has_b ? (int32_t)b : (int32_t)a;
            line_open = false;
            p = next;
            continue;
        }

        uint8_t type;
        switch (c)
        {
            case ':': type = NOTE_NORMAL; break;
            case '*': type = NOTE_GOLDEN; break;
            case 'F': type = NOTE_FREESTYLE; break;
            case 'R': type = NOTE_RAP; break;
            case 'G': type = NOTE_RAP_GOLDEN; break;
            default:  s->bad_lines++; p = next; continue;
        }

        const char *r = q + 1;
        long st, ln, pi;
        if (!parse_int(&r, e, &st) || !parse_int(&r, e, &ln) || !parse_int(&r, e, &pi))
        {
            s->bad_lines++;
            p = next;
            continue;
        }
        // the syllable follows one separating space and may begin with a space
        if (r < e && (*r == ' ' || *r == '\t')) r++;
        const char *txt = r;
        size_t tlen = (size_t)(e - r);

        if (s->n_notes >= SONG_MAX_NOTES) { s->bad_lines++; p = next; continue; }
        if (!line_open)
        {
            if (s->n_lines >= SONG_MAX_LINES) { s->bad_lines++; p = next; continue; }
            cur = &s->lines[s->n_lines++];
            memset(cur, 0, sizeof *cur);
            cur->first = s->n_notes;
            s->tracks[track].n_lines++;
            line_open = true;
        }

        note_t *n = &s->notes[s->n_notes++];
        n->start = rel_base + (int32_t)st;
        n->len = (int16_t)(ln < 1 ? 1 : ln > 30000 ? 30000 : ln);
        n->pitch = (int8_t)(pi < -60 ? -60 : pi > 60 ? 60 : pi);
        n->type = type;

        char buf[96];
        size_t bl = to_latin1(buf, sizeof buf, txt, tlen, g_utf8);
        // '~' marks a held syllable (melisma): the '~' is not shown, the
        // rest is: "~ms" (the end of "dreams") shows "ms", and "~ " keeps
        // its space, which ends the word.
        {
            size_t a = 0;
            while (a < bl && buf[a] == ' ') a++;
            if (a < bl && buf[a] == '~')
            {
                memmove(buf + a, buf + a + 1, bl - a - 1);
                bl--;
            }
        }
        if (bl > 255) bl = 255;
        if (s->pool_used + bl > SONG_TEXT_POOL) bl = 0;
        memcpy(s->pool + s->pool_used, buf, bl);
        n->text = s->pool_used;
        n->text_len = (uint8_t)bl;
        s->pool_used += (uint16_t)bl;

        if (cur->count == 0) cur->start = n->start;
        cur->count++;
        if (n->start + n->len > cur->end) cur->end = n->start + n->len;

        p = next;
    }

    if (header_only) return have_bpm;
    if (s->n_tracks == 1) s->tracks[0].n_lines = s->n_lines;
    return have_bpm && s->n_notes > 0;
}

// ---------------------------------------------------------------- timing
float song_beat_ms(const song_t *s)
{
    return s->bpm > 0 ? 60000.0f / (s->bpm * 4.0f) : 100.0f;
}

float song_beat_at(const song_t *s, int32_t ms)
{
    return (float)(ms - s->gap_ms) / song_beat_ms(s);
}

int32_t song_ms_at(const song_t *s, float beat)
{
    return s->gap_ms + (int32_t)(beat * song_beat_ms(s));
}

int32_t song_length_ms(const song_t *s)
{
    int32_t last = 0;
    for (uint16_t i = 0; i < s->n_lines; i++)
        if (s->lines[i].end > last) last = s->lines[i].end;
    return song_ms_at(s, (float)last);
}

int song_line_at(const song_t *s, int track, float beat)
{
    if (track >= s->n_tracks) return -1;
    const track_t *t = &s->tracks[track];
    for (uint16_t i = 0; i < t->n_lines; i++)
    {
        const line_t *l = &s->lines[t->first_line + i];
        if ((float)l->end > beat) return t->first_line + i;
    }
    return -1;
}
