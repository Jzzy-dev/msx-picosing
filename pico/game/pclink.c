// PicoSing - karaoke for the MSX PicoVerse 2350
// pclink.c - the music from a player on the PC (see pclink.h)
//
// The song's clock is the player's time: PicoAmp for Windows reads it from
// Windows (the player says where it is and when it said so) and sends it
// twice a second; in between it runs on here. Small differences are taken
// in a quarter at a time, so the notes don't jump; a big one (a seek, an ad
// that ended) at once.

#include "pclink.h"
#include "platform.h"
#include "settings.h"
#include "../net/web.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QN 6

static web_t w_scr, w_cmd;
static bool ready;
static int state = PC_OFF;
static char msg[48], title[96], title_at_open[96];
static bool testing;

// the command queue (paths on the PC)
static char q[QN][440];
static int qh, qt;
static bool open_sent;
static uint32_t t_start, t_retry;

// the player
static int32_t base_el;           // its time at base_at
static uint32_t base_at;
static bool have_time, playing, local_pause;
static char pstate;                // p, a, s
static int vol = -1;
static uint32_t ignore_until;     // after a seek: its old time still comes for a moment
static bool seen_stop_after_open;
static int32_t gap;                // the video's start (#VIDEOGAP)

static char line[200];
static size_t line_len;

static void set_msg(const char *m) { snprintf(msg, sizeof msg, "%s", m); }
const char *pc_message(void) { return msg; }
const char *pc_title(void) { return title; }
int pc_state(void) { return state; }
int pc_volume(void) { return vol; }
bool pc_paused(void) { return local_pause || (have_time && !playing); }

static void init(void)
{
    if (ready) return;
    web_init(&w_scr);
    web_init(&w_cmd);
    ready = true;
}

static void queue(const char *path)
{
    if ((qt + 1) % QN == qh) return;           // full: dropped
    snprintf(q[qt], sizeof q[qt], "%s", path);
    qt = (qt + 1) % QN;
}

static int32_t clock_now(void)
{
    if (!have_time) return -1;
    if (!playing || local_pause) return base_el;
    return base_el + (int32_t)(plat_millis() - base_at);
}

static void t_line(const char *l)
{
    // T <elapsed ms> <length s> <p|a|s> <volume> <muted>
    char st = 's';
    int el = 0, len = 0, v = -1, m = 0;
    if (sscanf(l + 1, "%d %d %c %d %d", &el, &len, &st, &v, &m) < 3) return;
    (void)len; (void)m;
    vol = v;
    pstate = st;
    uint32_t now = plat_millis();
    if (state == PC_WAITING && st != 'p') seen_stop_after_open = true;
    if (state == PC_WAITING && st == 'p' && (seen_stop_after_open || strcmp(title, title_at_open)))
    {
        state = PC_PLAYING;
        set_msg("");
        have_time = false;
    }
    if (state != PC_PLAYING) return;
    if ((int32_t)(now - ignore_until) < 0) return;
    if (st != 'p')
    {
        base_el = el;
        base_at = now;
        playing = false;
        have_time = true;
        return;
    }
    if (local_pause) local_pause = false;        // it plays (paused here, played on the PC)
    int32_t pred = clock_now();
    if (!have_time || !playing || pred < 0 || abs(el - pred) > 400)
    {
        base_el = el;
        base_at = now;
    }
    else
    {
        base_el = pred + (el - pred) / 4;
        base_at = now;
    }
    playing = true;
    have_time = true;
}

static void a_line(const char *l)
{
    if (l[0] == 'T') t_line(l);
    else if (l[0] == 'S')
    {
        // S<artist> TAB <title> TAB <album>
        char a[48] = "", t[48] = "";
        const char *tab = strchr(l + 1, '\t');
        size_t al = tab ? (size_t)(tab - l - 1) : strlen(l + 1);
        snprintf(a, sizeof a, "%.*s", (int)(al < 47 ? al : 47), l + 1);
        if (tab)
        {
            const char *t2 = strchr(tab + 1, '\t');
            size_t tl = t2 ? (size_t)(t2 - tab - 1) : strlen(tab + 1);
            snprintf(t, sizeof t, "%.*s", (int)(tl < 47 ? tl : 47), tab + 1);
        }
        snprintf(title, sizeof title, "%s%s%s", a, a[0] && t[0] ? " - " : "", t);
    }
}

static bool scr_body(web_t *w, const uint8_t *p, size_t n)
{
    (void)w;
    for (size_t i = 0; i < n; i++)
    {
        char c = (char)p[i];
        if (c == '\n')
        {
            line[line_len] = 0;
            a_line(line);
            line_len = 0;
            continue;
        }
        if (line_len < sizeof line - 1) line[line_len++] = c;
    }
    return false;
}

static void scr_done(web_t *w, bool ok)
{
    (void)ok;
    if (state == PC_OFF) return;
    if (state == PC_CONNECTING || testing)
    {
        char m[48];
        snprintf(m, sizeof m, "NO ANSWER FROM THE PC (%s)", web_error_text(w));
        set_msg(m);
        state = PC_FAILED;
        return;
    }
    // it went: again in a moment (meanwhile the clock runs on)
    t_retry = plat_millis() + 1500;
}

static char cmd_reply[64];
static size_t cmd_reply_len;
static bool cmd_body(web_t *w, const uint8_t *p, size_t n)
{
    (void)w;
    for (size_t i = 0; i < n && cmd_reply_len < sizeof cmd_reply - 1; i++) cmd_reply[cmd_reply_len++] = (char)p[i];
    cmd_reply[cmd_reply_len] = 0;
    return false;
}

static void cmd_done(web_t *w, bool ok)
{
    // the open command's answer says whether the PC knows PicoSing
    bool was_open = !strncmp(q[qh], "/sing/open", 10), was_file = !strncmp(q[qh], "/sing/open?file=", 16);
    qh = (qh + 1) % QN;
    if (!was_open) return;
    if (!ok)
    {
        if (was_file && web_http(w) == 404) set_msg("THE MP3 IS NOT ON THE PC");
        else if (web_http(w) == 404 || web_http(w) == 400) set_msg("UPDATE PICOAMP FOR WINDOWS");
        else { char m[48]; snprintf(m, sizeof m, "THE PC: %s", web_error_text(w)); set_msg(m); }
        state = PC_FAILED;
        web_abort(&w_scr);
        return;
    }
    if (state == PC_WAITING) set_msg("STARTING THE SONG ON THE PC...");
}

static void start_screen(void)
{
    char url[120];
    snprintf(url, sizeof url, "http://%s/screen", g_set.pc);
    line_len = 0;
    if (!web_start(&w_scr, url, NULL, 0, scr_body, scr_done, true))
    {
        char m[48];
        snprintf(m, sizeof m, "NO PC LINK (%s)", web_error_text(&w_scr));
        set_msg(m);
        state = PC_FAILED;
    }
}

static bool start(const song_t *s, const char *file);
bool pc_start(const song_t *s) { return start(s, NULL); }
bool pc_start_file(const song_t *s, const char *file) { return start(s, file); }

static bool start(const song_t *s, const char *file)
{
    init();
    testing = false;
    if (!g_set.pc[0]) { set_msg("SET THE PC IN SETTINGS"); state = PC_FAILED; return false; }
    if (web_busy(&w_scr)) web_abort(&w_scr);
    qh = qt = 0;
    title[0] = title_at_open[0] = 0;
    have_time = playing = local_pause = false;
    seen_stop_after_open = false;
    open_sent = false;
    ignore_until = 0;
    t_retry = 0;
    pstate = 0;
    gap = s->youtube[0] && !file ? s->videogap_ms : 0;
    // what to open: the song's MP3 on the PC, its YouTube video, else a search for it
    char path[440], e[400];
    if (file)
    {
        if (!web_escape(e, sizeof e, file)) { set_msg("THE PATH IS TOO LONG"); state = PC_FAILED; return false; }
        snprintf(path, sizeof path, "/sing/open?file=%s", e);
    }
    else if (s->youtube[0]) snprintf(path, sizeof path, "/sing/open?yt=%s", s->youtube);
    else
    {
        char what[120];
        snprintf(what, sizeof what, "%s %s", s->artist, s->title);
        web_escape(e, sizeof e, what);
        snprintf(path, sizeof path, "/sing/open?q=%s", e);
    }
    queue(path);
    state = PC_CONNECTING;
    set_msg("ASKING THE PC...");
    t_start = plat_millis();
    if (web_busy(&w_scr)) { t_retry = plat_millis() + 300; return true; }   // (after the abort)
    start_screen();
    return state != PC_FAILED;
}

bool pc_test(void)
{
    init();
    if (!g_set.pc[0]) { set_msg("NO ADDRESS"); state = PC_FAILED; return false; }
    if (web_busy(&w_scr)) web_abort(&w_scr);
    testing = true;
    title[0] = 0;
    have_time = false;
    state = PC_CONNECTING;
    set_msg("LOOKING FOR THE PC...");
    t_start = plat_millis();
    start_screen();
    return state != PC_FAILED;
}

void pc_go_now(void)
{
    if (state != PC_WAITING) return;
    state = PC_PLAYING;
    have_time = false;
    set_msg("");
}

void pc_stop(void)
{
    if (state == PC_PLAYING || state == PC_WAITING) queue("/cmd?k=pause");
    state = PC_OFF;
    testing = false;
    if (web_busy(&w_scr)) web_abort(&w_scr);
}

int32_t pc_ms(void)
{
    if (state != PC_PLAYING) return -1;
    int32_t c = clock_now();
    if (c < 0) return -1;
    return c - gap + g_set.pc_sync;
}

void pc_pause(bool on)
{
    if (state != PC_PLAYING) return;
    if (on)
    {
        base_el = clock_now() < 0 ? 0 : clock_now();
        base_at = plat_millis();
        local_pause = true;
        queue("/cmd?k=pause");
    }
    else
    {
        local_pause = false;
        playing = false;                 // until the PC says it plays
        queue("/cmd?k=play");
    }
}

void pc_seek(int32_t ms)
{
    if (state != PC_PLAYING) return;
    int32_t player = ms + gap - g_set.pc_sync;    // the player's own time
    if (player < 0) player = 0;
    char p[48];
    snprintf(p, sizeof p, "/cmd?k=seek&to=%d", (int)(player / 1000));
    queue(p);
    base_el = (player / 1000) * 1000;
    base_at = plat_millis();
    have_time = true;
    ignore_until = plat_millis() + 1500;
}

void pc_set_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    vol = v;
    char p[40];
    snprintf(p, sizeof p, "/cmd?k=vol&v=%d", v);
    queue(p);
}

void pc_poll(void)
{
    if (!ready) return;
    web_poll(&w_scr);
    web_poll(&w_cmd);
    uint32_t now = plat_millis();
    if (testing && state == PC_CONNECTING && (have_time || title[0] || pstate))
    {
        char m[48];
        snprintf(m, sizeof m, "PC FOUND%s", title[0] ? ": IT PLAYS" : "");
        set_msg(m);
        state = PC_OFF;
        testing = false;
        web_abort(&w_scr);
    }
    if (state == PC_CONNECTING && !testing)
    {
        // the screen's first lines tell what played before: then the song opens
        bool heard = pstate != 0;
        if (heard || now - t_start > 2500)
        {
            snprintf(title_at_open, sizeof title_at_open, "%s", title);
            seen_stop_after_open = pstate != 'p';
            state = PC_WAITING;
            set_msg("STARTING THE SONG ON THE PC...");
        }
    }
    if (state == PC_WAITING && now - t_start > 8000 && !strcmp(msg, "STARTING THE SONG ON THE PC..."))
        set_msg("PLAY IT ON THE PC (SPACE: IT PLAYS)");
    if (t_retry && (int32_t)(now - t_retry) >= 0 && !web_busy(&w_scr) && (state == PC_PLAYING || state == PC_WAITING || state == PC_CONNECTING))
    {
        t_retry = 0;
        start_screen();
    }
    // the next command (the open waits until the PC's state is known)
    if (!web_busy(&w_cmd) && qh != qt && g_set.pc[0])
    {
        bool is_open = !strncmp(q[qh], "/sing/open", 10);
        if (is_open && state == PC_CONNECTING) return;
        char url[520];
        snprintf(url, sizeof url, "http://%s%s", g_set.pc, q[qh]);
        cmd_reply_len = 0;
        if (is_open) open_sent = true;
        if (!web_start(&w_cmd, url, NULL, 0, cmd_body, cmd_done, false)) qh = (qh + 1) % QN;
    }
}
