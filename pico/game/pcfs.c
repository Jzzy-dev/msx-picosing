// PicoSing - karaoke for the MSX PicoVerse 2350
// pcfs.c - the song folder on a PC (see pcfs.h)
//
// PicoAmp for Windows (its MSX link, g_set.pc) answers:
//   /sing/songs?path=a   "<path of the .txt> TAB <artist> TAB <title> TAB <1: duet>" a line each
//   /sing/list?path=a    "d name" / "f name" a line each
//   /sing/file?path=a/b  the file
// Paths are its karaoke folder's (UTF-8, '/' between the folders); here
// they are "PC:/" and that.

#include "pcfs.h"
#include "library.h"
#include "settings.h"
#include "song.h"
#include "../net/web.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define T_REQ_MS     8000u       // a request's answer, at most
#define CACHE_N      40          // the names of the folder last looked at
#define RING         4096u

static web_t W, WS;              // the requests; a file streamed
static bool ready;
static char err[40];

const char *pcfs_error(void) { return err; }
bool pcfs_is(const char *path) { return path && !strncmp(path, PCFS_ROOT, 3); }
const char *pcfs_rel(const char *path)
{
    const char *p = path + 3;
    while (*p == '/') p++;
    return p;
}

static void init(void)
{
    if (ready) return;
    web_init(&W);
    web_init(&WS);
    ready = true;
}

static bool url_of(char *url, size_t n, const char *what, const char *rel)
{
    if (!g_set.pc[0]) { snprintf(err, sizeof err, "SET THE PC IN SETTINGS"); return false; }
    int k = snprintf(url, n, "http://%s/sing/%s?path=", g_set.pc, what);
    if (k <= 0 || (size_t)k >= n || !web_escape(url + k, n - (size_t)k, rel)) { snprintf(err, sizeof err, "THE PATH IS TOO LONG"); return false; }
    return true;
}

// ---------------------------------------------------------------- a request, waited for
static bool r_done, r_ok;
static void on_done(web_t *w, bool ok) { (void)w; r_done = true; r_ok = ok; }

static bool fetch(const char *url, web_body_fn body)
{
    init();
    err[0] = 0;
    if (web_busy(&W)) web_abort(&W);
    while (web_busy(&W)) { plat_idle(); web_poll(&W); }
    r_done = r_ok = false;
    if (!web_start(&W, url, NULL, 0, body, on_done, false))
    {
        snprintf(err, sizeof err, "NO PC LINK (%s)", web_error_text(&W));
        return false;
    }
    uint32_t t0 = plat_millis();
    bool gave_up = false;
    while (!r_done)
    {
        plat_idle();
        web_poll(&W);
        if (!gave_up && plat_millis() - t0 > T_REQ_MS) { web_abort(&W); gave_up = true; }
    }
    if (!r_ok)
    {
        if (web_http(&W) == 404 || web_http(&W) == 400) snprintf(err, sizeof err, web_http(&W) == 404 ? "NOT ON THE PC" : "UPDATE PICOAMP FOR WINDOWS");
        else snprintf(err, sizeof err, "NO ANSWER FROM THE PC");
    }
    return r_ok;
}

// lines of an answer
static char line[600];
static size_t line_len;
static void (*on_line)(char *l);
static bool lines_body(web_t *w, const uint8_t *p, size_t n)
{
    (void)w;
    for (size_t i = 0; i < n; i++)
    {
        char c = (char)p[i];
        if (c == '\r') continue;
        if (c == '\n') { line[line_len] = 0; on_line(line); line_len = 0; continue; }
        if (line_len < sizeof line - 1) line[line_len++] = c;
    }
    return false;
}

static bool fetch_lines(const char *url, void (*fn)(char *l))
{
    line_len = 0;
    on_line = fn;
    bool ok = fetch(url, lines_body);
    if (ok && line_len) { line[line_len] = 0; fn(line); }
    line_len = 0;
    return ok;
}

// ---------------------------------------------------------------- the songs
static int s_found;
static bool s_full, s_cleared;
static void song_line(char *l)
{
    if (!s_cleared) { lib_clear(); s_cleared = true; }     // (the list before stays when the PC does not answer)
    // path TAB artist TAB title TAB duet
    char *f[4] = { l, NULL, NULL, NULL };
    for (int i = 1; i < 4; i++)
    {
        f[i] = f[i - 1] ? strchr(f[i - 1], '\t') : NULL;
        if (f[i]) *f[i]++ = 0;
    }
    if (!f[2] || !l[0] || s_full) return;
    char path[300], artist[32], title[40];
    if (snprintf(path, sizeof path, "%s/%s", PCFS_ROOT, l) >= (int)sizeof path) return;
    song_to_latin1(artist, sizeof artist, f[1], strlen(f[1]));
    song_to_latin1(title, sizeof title, f[2], strlen(f[2]));
    if (!title[0])
    {
        // (no title: the folder's name)
        const char *e = strrchr(l, '/'), *b = e;
        while (b && b > l && b[-1] != '/') b--;
        if (e && b) song_to_latin1(title, sizeof title, b, (size_t)(e - b));
    }
    if (!lib_add(artist[0] ? artist : "?", title, f[3] && f[3][0] == '1', path)) { s_full = true; return; }
    s_found++;
}

int pcfs_scan(const char *root)
{
    char url[460];
    if (!url_of(url, sizeof url, "songs", pcfs_rel(root))) return -1;
    s_found = 0;
    s_full = s_cleared = false;
    if (!fetch_lines(url, song_line)) { if (s_cleared) lib_sort(); return -1; }
    if (!s_cleared) lib_clear();         // (no songs)
    lib_sort();
    return s_found;
}

// ---------------------------------------------------------------- folders
static plat_dirent_t *l_out;
static int l_n, l_max;
static void list_line(char *l)
{
    if ((l[0] != 'd' && l[0] != 'f') || l[1] != ' ' || l_n >= l_max) return;
    if (strlen(l + 2) >= sizeof l_out[0].name) return;     // (a path to it would not fit)
    snprintf(l_out[l_n].name, sizeof l_out[l_n].name, "%s", l + 2);
    l_out[l_n].dir = l[0] == 'd';
    l_out[l_n].size = 0;
    l_n++;
}

static int list_into(const char *path, plat_dirent_t *out, int max)
{
    char url[460];
    if (!url_of(url, sizeof url, "list", pcfs_rel(path))) return -1;
    l_out = out;
    l_n = 0;
    l_max = max;
    if (!fetch_lines(url, list_line)) return -1;
    return l_n;
}

// the folder last listed: for pcfs_exists
static plat_dirent_t cache[CACHE_N];
static int cache_n = -1;
static char cache_dir[300];

int pcfs_list(const char *path, plat_dirent_t *out, int max)
{
    int n = list_into(path, out, max);
    if (n >= 0 && n <= CACHE_N)
    {
        memcpy(cache, out, (size_t)n * sizeof cache[0]);
        cache_n = n;
        snprintf(cache_dir, sizeof cache_dir, "%s", path);
    }
    return n;
}

bool pcfs_exists(const char *path)
{
    const char *sl = strrchr(path, '/');
    if (!sl) return false;
    size_t dl = (size_t)(sl - path);
    if (cache_n < 0 || strlen(cache_dir) != dl || strncmp(cache_dir, path, dl))
    {
        char dir[300];
        snprintf(dir, sizeof dir, "%.*s", (int)dl, path);
        cache_n = -1;
        int n = list_into(dir, cache, CACHE_N);
        if (n < 0) return false;
        cache_n = n;
        snprintf(cache_dir, sizeof cache_dir, "%s", dir);
    }
    for (int i = 0; i < cache_n; i++)
        if (!strcasecmp(cache[i].name, sl + 1)) return true;
    return false;
}

// ---------------------------------------------------------------- files
static char *f_buf;
static size_t f_max, f_len;
static bool f_over;
static bool file_body(web_t *w, const uint8_t *p, size_t n)
{
    (void)w;
    if (f_len + n > f_max) { f_over = true; return true; }
    memcpy(f_buf + f_len, p, n);
    f_len += n;
    return false;
}

bool pcfs_read(const char *path, char *buf, size_t max, size_t *len)
{
    char url[460];
    if (!url_of(url, sizeof url, "file", pcfs_rel(path))) return false;
    f_buf = buf;
    f_max = max;
    f_len = 0;
    f_over = false;
    bool ok = fetch(url, file_body) && !f_over;
    if (f_over) snprintf(err, sizeof err, "THE FILE IS TOO BIG");
    buf[f_len < max ? f_len : max] = 0;
    *len = f_len;
    return ok;
}

// a file streamed: a ring the request fills; it waits while the ring is full
static uint8_t ring[RING];
static uint32_t rh, rt, rc, s_left;
static bool s_on, s_end;
static void s_hold(void) { WS.hold = RING - rc < 1100u; }
static bool stream_body(web_t *w, const uint8_t *p, size_t n)
{
    (void)w;
    for (size_t i = 0; i < n && rc < RING; i++) { ring[rh] = p[i]; rh = (rh + 1) % RING; rc++; }
    s_hold();
    return false;
}
static void stream_done(web_t *w, bool ok) { (void)w; (void)ok; s_end = true; }

bool pcfs_stream_open(const char *path, uint32_t off, uint32_t len)
{
    char url[460];
    pcfs_stream_close();
    if (off) return false;                  // (a picture inside an MP3: not from the PC)
    if (!url_of(url, sizeof url, "file", pcfs_rel(path))) return false;
    init();
    rh = rt = rc = 0;
    s_left = len ? len : 0xFFFFFFFFu;
    s_end = false;
    WS.hold = false;
    if (!web_start(&WS, url, NULL, 0, stream_body, stream_done, false)) return false;
    s_on = true;
    return true;
}

size_t pcfs_stream_read(uint8_t *buf, size_t n)
{
    if (!s_on) return 0;
    uint32_t t0 = plat_millis();
    while (rc == 0 && !s_end)
    {
        plat_idle();
        web_poll(&WS);
        if (plat_millis() - t0 > T_REQ_MS) { web_abort(&WS); break; }
    }
    if (n > s_left) n = s_left;
    size_t k = 0;
    while (k < n && rc) { buf[k++] = ring[rt]; rt = (rt + 1) % RING; rc--; }
    s_left -= (uint32_t)k;
    s_hold();
    return k;
}

void pcfs_stream_close(void)
{
    if (!s_on) return;
    s_on = false;
    WS.hold = false;
    if (web_busy(&WS))
    {
        web_abort(&WS);
        uint32_t t0 = plat_millis();
        while (web_busy(&WS) && plat_millis() - t0 < 2000u) { plat_idle(); web_poll(&WS); }
    }
}
