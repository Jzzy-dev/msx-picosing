// PicoSing PC simulator: the platform (songs from a folder, a simulated
// clock and music) and simulated singers, shared by sim.c and e2e.c
#include "simplat.h"
#include "../pico/game/game.h"
#include "../pico/game/platform.h"
#include "../pico/game/library.h"
#include "../pico/game/id3.h"
#include "../pico/game/music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <dirent.h>
#include <unistd.h>
#include <strings.h>
#include <sys/stat.h>

uint32_t now;                   // ms
int sim_late_ms;
static int32_t audio_t0 = -1, audio_len;

// ---------------------------------------------------------------- platform
uint32_t plat_millis(void) { return now; }
const char *plat_card_problem(void) { return NULL; }

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    *len = fread(b, 1, (size_t)n, f);
    b[*len] = 0;
    fclose(f);
    return b;
}

bool plat_song_load(int i, song_t *s, char *err, int n)
{
    size_t len; char *b = read_file(lib_path(i), &len);
    if (!b) { snprintf(err, (size_t)n, "CANNOT READ THE SONG"); return false; }
    bool ok = song_parse(b, len, s, false);
    free(b);
    if (!ok) snprintf(err, (size_t)n, "NOT AN ULTRASTAR SONG");
    return ok;
}

bool plat_audio_start(int i, const song_t *s, char *err, int n)
{
    (void)i; (void)err; (void)n;
    audio_t0 = (int32_t)now;
    audio_len = song_length_ms(s) + 2500;
    return true;
}
static int32_t sim_paused_at = -1;
void plat_audio_stop(void) { audio_t0 = -1; sim_paused_at = -1; }
void plat_audio_pause(bool on)
{
    if (audio_t0 < 0) return;
    if (on && sim_paused_at < 0) sim_paused_at = (int32_t)now - audio_t0;
    else if (!on && sim_paused_at >= 0) { audio_t0 = (int32_t)now - sim_paused_at; sim_paused_at = -1; }
}
void plat_audio_seek(int32_t ms)
{
    if (audio_t0 < 0) return;
    sim_paused_at = -1;
    audio_t0 = (int32_t)now - (ms / 1000) * 1000;    // whole seconds, as on the Pico
}
int32_t plat_audio_ms(void)
{
    if (audio_t0 >= 0 && sim_paused_at >= 0) return sim_paused_at;
    if (audio_t0 < 0) return -1;
    int32_t t = (int32_t)now - audio_t0;
    return t >= audio_len ? -1 : t;
}

bool plat_audio_preview(int i, const song_t *s, int32_t from_ms)
{
    (void)i;
    audio_t0 = (int32_t)now - from_ms;
    audio_len = song_length_ms(s) + 2500;
    return true;
}

int sim_music_on;
void plat_music(bool on) { sim_music_on = on; }

// ---------------------------------------------------------------- files
const char *sim_data = "/tmp/picosing-sim";
const char *plat_data_dir(void) { mkdir(sim_data, 0777); return sim_data; }

bool plat_read_file(const char *path, char *buf, size_t max, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    *len = fread(buf, 1, max, f);
    buf[*len < max ? *len : max - 1] = 0;
    fclose(f);
    return true;
}
static bool put(const char *path, const char *mode, const char *d, size_t n)
{
    FILE *f = fopen(path, mode);
    if (!f) return false;
    bool ok = fwrite(d, 1, n, f) == n;
    fclose(f);
    return ok;
}
bool plat_write_file(const char *p, const char *d, size_t n) { return put(p, "wb", d, n); }
bool plat_append_file(const char *p, const char *d, size_t n) { return put(p, "ab", d, n); }
bool plat_exists(const char *path) { struct stat st; return stat(path, &st) == 0; }
bool plat_song_dir(int i, char *dir, size_t size)
{
    snprintf(dir, size, "%s", lib_path(i));
    char *sl = strrchr(dir, '/');
    if (!sl) return false;
    *sl = 0;
    return true;
}

static FILE *sf;
static uint32_t s_left;
bool plat_stream_open(const char *path, uint32_t off, uint32_t len)
{
    sf = fopen(path, "rb");
    if (!sf) return false;
    fseek(sf, off, SEEK_SET);
    s_left = len ? len : 0xFFFFFFFFu;
    return true;
}
size_t plat_stream_read(uint8_t *buf, size_t n)
{
    if (!sf) return 0;
    if (n > s_left) n = s_left;
    size_t r = fread(buf, 1, n, sf);
    s_left -= (uint32_t)r;
    return r;
}
void plat_stream_close(void) { if (sf) fclose(sf); sf = NULL; }

bool plat_song_tags(int i, const song_t *s, song_tags_t *t)
{
    memset(t, 0, sizeof *t);
    char dir[600], p[800];
    if (!s->audio[0] || !plat_song_dir(i, dir, sizeof dir)) return false;
    snprintf(p, sizeof p, "%s/%s", dir, s->audio);
    id3_text_t x;
    if (!id3_read(p, &x)) return false;
    t->ok = true;
    snprintf(t->title, sizeof t->title, "%s", x.title);
    snprintf(t->artist, sizeof t->artist, "%s", x.artist);
    snprintf(t->album, sizeof t->album, "%s", x.album);
    return true;
}

static char sim_root[1024];

static int scan_dir(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d) return -1;
    int found = 0;
    struct dirent *e;
    while ((e = readdir(d)))
    {
        if (e->d_name[0] == '.') continue;
        char sub[1200]; snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(sub, &st)) continue;
        if (S_ISDIR(st.st_mode))
        {
            if (strcasecmp(e->d_name, "PICOSING") && depth < 4) { int r = scan_dir(sub, depth + 1); if (r > 0) found += r; }
            continue;
        }
        size_t l = strlen(e->d_name);
        if (depth == 0 || l < 4 || strcasecmp(e->d_name + l - 4, ".txt")) continue;
        size_t len; char *b = read_file(sub, &len);
        song_t *s = game_song();
        if (b && strstr(b, "#BPM") && song_parse(b, len, s, true))
        {
            lib_add(s->artist, s->title, strstr(b, "\nP2") != NULL, sub);
            found++;
        }
        free(b);
    }
    closedir(d);
    return found;
}

const char *plat_song_root(void) { return sim_root; }
bool plat_set_song_root(const char *path)
{
    snprintf(sim_root, sizeof sim_root, "%s", path);
    lib_clear();
    int n = scan_dir(sim_root, 0);
    lib_sort();
    return n >= 0;
}

void sim_scan(const char *root)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/KARAOKE", root);
    plat_set_song_root(dir);
}

int plat_list_dir(const char *path, plat_dirent_t *out, int max)
{
    DIR *d = opendir(path);
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while (n < max && (e = readdir(d)))
    {
        if (e->d_name[0] == '.') continue;
        char sub[1200]; snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(sub, &st)) continue;
        snprintf(out[n].name, sizeof out[n].name, "%s", e->d_name);
        out[n].dir = S_ISDIR(st.st_mode);
        out[n].size = (uint32_t)st.st_size;
        n++;
    }
    closedir(d);
    return n;
}
bool plat_remove(const char *path) { return remove(path) == 0; }
bool plat_rename(const char *a, const char *b) { return rename(a, b) == 0; }

static FILE *fh[2];
int plat_fopen(const char *path, bool write)
{
    for (int h = 0; h < 2; h++)
        if (!fh[h])
        {
            fh[h] = fopen(path, write ? "r+b" : "rb");
            if (!fh[h] && write) fh[h] = fopen(path, "w+b");
            return fh[h] ? h : -1;
        }
    return -1;
}
size_t plat_fread(int h, void *b, size_t n) { return fread(b, 1, n, fh[h]); }
size_t plat_fwrite(int h, const void *b, size_t n) { return fwrite(b, 1, n, fh[h]); }
bool plat_fseek(int h, uint32_t o) { return fseek(fh[h], (long)o, SEEK_SET) == 0; }
uint32_t plat_fsize(int h) { long p = ftell(fh[h]); fseek(fh[h], 0, SEEK_END); long n = ftell(fh[h]); fseek(fh[h], p, SEEK_SET); return (uint32_t)n; }
bool plat_ftruncate(int h) { fflush(fh[h]); return ftruncate(fileno(fh[h]), ftell(fh[h])) == 0; }
void plat_fclose(int h) { if (h >= 0 && h < 2 && fh[h]) { fclose(fh[h]); fh[h] = NULL; } }

// ---------------------------------------------------------------- singers
// The note a track wants at a time (UltraStar pitch), or 99 in a pause
static int target(const song_t *s, int track, int32_t ms)
{
    float b = song_beat_at(s, ms);
    const track_t *t = &s->tracks[track];
    for (int li = 0; li < t->n_lines; li++)
    {
        const line_t *ln = &s->lines[t->first_line + li];
        for (int i = 0; i < ln->count; i++)
        {
            const note_t *n = &s->notes[ln->first + i];
            if (b >= n->start && b < n->start + n->len) return n->pitch;
        }
    }
    return 99;
}

// p0: sings an octave lower than written (a man), in tune
// p1: sings at the written octave but goes a whole tone flat in the second half of each note
int32_t (*sim_heard_ms)(void);      // what the singers hear (default: the game's music)
void sim_sing(int p, uint32_t t, int skill)
{
    const song_t *s = game_song();
    int32_t pos = sim_heard_ms ? sim_heard_ms() : music_ms();
    pitch_result_t r = { .dbfs = -70.0f, .note = -1 };
    if (pos >= 0)
    {
        int32_t heard = pos - 46 - sim_late_ms;    // the analysis window's middle, and a late singer
        int tp = target(s, s->duet ? p : 0, heard);
        if (tp != 99)
        {
            float b = song_beat_at(s, heard);
            float frac = b - floorf(b);
            float sung = 60 + tp + (p == 0 ? -12 : 0);
            if (skill == 1 && frac > 0.5f) sung -= 2;
            r.dbfs = -20; r.voiced = true;
            r.note = (int)lrintf(sung); r.cents = 0; r.freq = 440.0f * powf(2, (sung - 69) / 12); r.clarity = 95;
        }
    }
    game_mic(p, &r, t);
}


// ---------------------------------------------------------------- volume, folders, new songs
int sim_volume = -1;
void plat_set_volume(int v) { sim_volume = v; }

bool plat_mkdir(const char *path)
{
    char p[1200];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = strchr(p + 1, '/'); ; s = strchr(s + 1, '/'))
    {
        if (s) *s = 0;
        mkdir(p, 0777);
        if (!s) break;
        *s = '/';
    }
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool plat_add_song(const char *txt_path)
{
    size_t len;
    char *b = read_file(txt_path, &len);
    song_t *s = game_song();
    bool ok = b && song_parse(b, len, s, true);
    if (ok) { lib_add(s->artist, s->title, strstr(b, "\nP2") != NULL, txt_path); lib_sort(); }
    free(b);
    return ok;
}
