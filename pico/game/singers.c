// PicoSing - karaoke for the MSX PicoVerse 2350
// singers.c - singers, session scores, score history (see singers.h)

#include "singers.h"
#include "scratch.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static singer_t list[SINGERS_MAX];
static int count;

static void path(char *p, size_t n, const char *file)
{
    snprintf(p, n, "%s/%s", plat_data_dir(), file);
}

static void defaults(void)
{
    count = 2;
    memset(list, 0, sizeof list);
    snprintf(list[0].name, sizeof list[0].name, "BLUE");
    list[0].avatar = 0;
    snprintf(list[1].name, sizeof list[1].name, "RED");
    list[1].avatar = 5;
}

void singers_load(void)
{
    char p[160], buf[600];
    size_t len;
    path(p, sizeof p, "SINGERS.TXT");
    if (!plat_read_file(p, buf, sizeof buf - 1, &len)) { defaults(); return; }
    count = 0;
    memset(list, 0, sizeof list);
    char *line = buf;
    while (*line && count < SINGERS_MAX)
    {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        char *comma = strchr(line, ',');
        if (comma)
        {
            singer_t *s = &list[count];
            s->avatar = (uint8_t)(atoi(line) % AVATARS);
            char *n = comma + 1;
            size_t k = 0;
            while (n[k] && n[k] != '\r' && k < NAME_MAX) { s->name[k] = n[k]; k++; }
            s->name[k] = 0;
            if (k) count++;
        }
        if (!end) break;
        line = end + 1;
    }
    if (!count) defaults();
}

void singers_save(void)
{
    char p[160], buf[600];
    int n = 0;
    for (int i = 0; i < count; i++)
        n += snprintf(buf + n, sizeof buf - (size_t)n, "%d,%s\n", list[i].avatar, list[i].name);
    path(p, sizeof p, "SINGERS.TXT");
    plat_write_file(p, buf, (size_t)n);
}

int singers_count(void) { return count; }
singer_t *singer_get(int i) { return (i >= 0 && i < count) ? &list[i] : NULL; }

bool singer_add(void)
{
    if (count >= SINGERS_MAX) return false;
    singer_t *s = &list[count];
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "SINGER %d", count + 1);
    s->avatar = (uint8_t)((count * 5) % AVATARS);
    count++;
    return true;
}

void singer_remove(int i)
{
    if (i < 0 || i >= count || count <= 1) return;
    memmove(&list[i], &list[i + 1], sizeof list[0] * (size_t)(count - i - 1));
    count--;
}

void singers_record(int singer, int score, const char *artist, const char *title)
{
    singer_t *s = singer_get(singer);
    if (!s) return;
    s->songs++;
    s->total += score;
    if (score > s->best) s->best = score;
    char p[160], line[160];
    path(p, sizeof p, "SCORES.TXT");
    int n = snprintf(line, sizeof line, "%d\t%s\t%s - %s\n", score, s->name, artist, title);
    if (n > 0) plat_append_file(p, line, (size_t)(n < (int)sizeof line ? n : (int)sizeof line - 1));
}

int singers_top(const char *artist, const char *title, top_t *top, int max)
{
    char p[160], key[120];
    char *buf = scratch(SCR_SCORES);         // (16 KB of it)
    size_t len;
    path(p, sizeof p, "SCORES.TXT");
    if (!plat_read_file(p, buf, sizeof buf - 1, &len)) return 0;
    snprintf(key, sizeof key, "%s - %s", artist, title);
    int n = 0;
    char *line = buf;
    while (*line)
    {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        char *t1 = strchr(line, '\t');
        char *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        if (t2)
        {
            char *song = t2 + 1;
            size_t sl = strlen(song);
            if (sl && song[sl - 1] == '\r') song[--sl] = 0;
            if (!strcmp(song, key))
            {
                top_t e = { atoi(line), "" };
                size_t k = (size_t)(t2 - t1 - 1);
                if (k > NAME_MAX) k = NAME_MAX;
                memcpy(e.name, t1 + 1, k);
                e.name[k] = 0;
                // keep the best 'max', sorted
                int pos = n;
                while (pos > 0 && top[pos - 1].score < e.score) pos--;
                if (pos < max)
                {
                    int last = n < max ? n : max - 1;
                    for (int j = last; j > pos; j--) top[j] = top[j - 1];
                    top[pos] = e;
                    if (n < max) n++;
                }
            }
        }
        if (!end) break;
        line = end + 1;
    }
    return n;
}

// ---------------------------------------------------------------- avatars
// Faces made of parts: skin, hair colour and style, extras.
typedef struct { uint8_t skin, hair, style, extra; } face_t;
// style: 0 short, 1 long, 2 spiky, 3 bald, 4 pigtails, 5 cap
// extra: 0 none, 1 glasses, 2 beard, 3 bow, 4 star cheeks
static const face_t faces[AVATARS] = {
    { C_YELLOW, C_P1,    0, 1 }, { C_NOTE, C_GOLD, 1, 0 }, { C_GOLD, C_DARK, 2, 0 },
    { C_NOTE,   C_P2,    4, 3 }, { C_GOLD, C_GREY, 3, 2 }, { C_NOTE, C_P2,   1, 4 },
    { C_YELLOW, C_GREEN, 5, 0 }, { C_GOLD, C_RAP,  2, 1 }, { C_NOTE, C_DARK, 0, 2 },
    { C_YELLOW, C_CYAN,  1, 3 }, { C_NOTE, C_P1,   5, 4 }, { C_GOLD, C_GOLD, 4, 1 },
};

static void px(fb_t *f, int x, int y, int s, int cx, int cy, uint8_t c)
{
    fb_rect(f, x + cx * s, y + cy * s, s, s, c);
}

static void row(fb_t *f, int x, int y, int s, int cy, int x0, int x1, uint8_t c)
{
    fb_rect(f, x + x0 * s, y + cy * s, (x1 - x0 + 1) * s, s, c);
}

void avatar_draw(fb_t *f, int a, int x, int y, int s)
{
    const face_t *fc = &faces[((a % AVATARS) + AVATARS) % AVATARS];
    // head: a rounded 10 x 11 block
    for (int r = 3; r <= 13; r++)
    {
        int in = (r == 3 || r == 13) ? 2 : (r == 4 || r == 12) ? 1 : 0;
        row(f, x, y, s, r, 3 + in, 12 - in, fc->skin);
    }
    // hair
    switch (fc->style)
    {
        case 0: row(f, x, y, s, 2, 4, 11, fc->hair); row(f, x, y, s, 3, 3, 12, fc->hair); row(f, x, y, s, 4, 3, 4, fc->hair); row(f, x, y, s, 4, 11, 12, fc->hair); break;
        case 1: row(f, x, y, s, 2, 4, 11, fc->hair); row(f, x, y, s, 3, 3, 12, fc->hair);
                for (int r = 4; r <= 14; r++) { px(f, x, y, s, 2, r, fc->hair); px(f, x, y, s, 13, r, fc->hair); px(f, x, y, s, 3, r, fc->hair); px(f, x, y, s, 12, r, fc->hair); } break;
        case 2: for (int k = 0; k < 5; k++) { px(f, x, y, s, 3 + k * 2, 1, fc->hair); row(f, x, y, s, 2, 3 + k * 2, 4 + k * 2, fc->hair); } row(f, x, y, s, 3, 3, 12, fc->hair); break;
        case 3: break;
        case 4: row(f, x, y, s, 2, 4, 11, fc->hair); row(f, x, y, s, 3, 3, 12, fc->hair);
                row(f, x, y, s, 5, 0, 2, fc->hair); row(f, x, y, s, 6, 0, 2, fc->hair); row(f, x, y, s, 5, 13, 15, fc->hair); row(f, x, y, s, 6, 13, 15, fc->hair); break;
        case 5: row(f, x, y, s, 1, 4, 11, fc->hair); row(f, x, y, s, 2, 3, 12, fc->hair); row(f, x, y, s, 3, 3, 15, fc->hair); break;
    }
    // eyes and mouth
    px(f, x, y, s, 6, 7, C_BLACK); px(f, x, y, s, 9, 7, C_BLACK);
    row(f, x, y, s, 10, 6, 9, C_P2); px(f, x, y, s, 5, 9, C_P2); px(f, x, y, s, 10, 9, C_P2);
    switch (fc->extra)
    {
        case 1: row(f, x, y, s, 6, 5, 7, C_DARK); row(f, x, y, s, 6, 8, 10, C_DARK); px(f, x, y, s, 5, 7, C_DARK); px(f, x, y, s, 10, 7, C_DARK); break;
        case 2: row(f, x, y, s, 11, 4, 11, fc->hair); row(f, x, y, s, 12, 5, 10, fc->hair); row(f, x, y, s, 13, 6, 9, fc->hair); break;
        case 3: row(f, x, y, s, 1, 6, 9, C_P2_HIT); px(f, x, y, s, 7, 2, C_P2_HIT); px(f, x, y, s, 8, 2, C_P2_HIT); break;
        case 4: px(f, x, y, s, 4, 9, C_P2_HIT); px(f, x, y, s, 11, 9, C_P2_HIT); break;
    }
}
