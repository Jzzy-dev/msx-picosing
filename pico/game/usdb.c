// PicoSing - karaoke for the MSX PicoVerse 2350
// usdb.c - find songs on USDB and save them on the card (see usdb.h)
//
// What is asked (as USDB Syncer does):
//   log in    POST index.php?link=login        user, pass, login=Login
//   search    POST index.php?link=list         interpret, title, order, ud, limit, start, details=1
//   the text  POST index.php?link=gettxt&id=N  wd=1: the .txt is in the page's <textarea>
//   cover     GET  data/cover/N.jpg
// Through PicoAmp for Windows: GET /usdb/login?u=&p=, /usdb/list?interpret=&title=&start=&limit=,
// /usdb/txt?id=, /usdb/cover?id= (the same pages back, the cookies kept on the PC).
//
// The pages are read as they come in (an HTML page can be bigger than the
// memory there is for it): a small tag reader picks out the rows and cells.

#include "usdb.h"
#include "platform.h"
#include "settings.h"
#include "song.h"
#include "../net/web.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const char *usdb_base = "https://usdb.animux.de/";

#define PAGE 20

static web_t w;
static bool ready, logged, via_pc, tried_pc, relogged;
static int state = U_IDLE;
static char msg[48];
static usdb_hit_t hits[USDB_MAX];
static int n_hits, page_rows;
static char q_artist[48], q_title[48];
static int q_start;

// the job
enum { J_NONE, J_SEARCH, J_TXT, J_COVER };
static int job;
enum { S_LOGIN, S_WORK };
static int step;
static int dl_i;
static char form[400];
static char folder[200], txt_path[260], cover_path[260], saved[260];
static int fh = -1;
static bool got_textarea, cover_jpeg;
static uint32_t file_bytes;

// markers in a page
static char tail[48];
static bool not_logged, login_bad;

static void set_msg(const char *m) { snprintf(msg, sizeof msg, "%s", m); }
int usdb_state(void) { return state; }
const char *usdb_message(void) { return msg; }
int usdb_count(void) { return n_hits; }
const usdb_hit_t *usdb_hit(int i) { return i >= 0 && i < n_hits ? &hits[i] : NULL; }
const char *usdb_saved(void) { return saved; }
bool usdb_via_pc(void) { return via_pc; }
bool usdb_has_more(void) { return page_rows >= PAGE && n_hits < USDB_MAX; }

static void markers(const uint8_t *p, size_t n)
{
    static const char *nl = "You are not logged in", *bad = "Password invalid";
    char buf[sizeof tail + 1100];
    size_t tl = strlen(tail);
    while (n)
    {
        size_t k = n > 1024 ? 1024 : n;
        memcpy(buf, tail, tl);
        memcpy(buf + tl, p, k);
        buf[tl + k] = 0;
        for (size_t i = 0; i < tl + k; i++) if (!buf[i]) buf[i] = ' ';
        if (strstr(buf, nl)) not_logged = true;
        if (strstr(buf, bad)) login_bad = true;
        size_t keep = tl + k > 40 ? 40 : tl + k;
        memcpy(tail, buf + tl + k - keep, keep);
        tail[keep] = 0;
        tl = keep;
        p += k;
        n -= k;
    }
}

// ---------------------------------------------------------------- HTML
typedef struct {
    bool in_tag, in_ent;
    char tag[300];
    size_t tag_len;
    char ent[12];
    size_t ent_len;
} html_t;
static html_t H;

static void html_reset(void) { memset(&H, 0, sizeof H); }

// an entity's text into out (UTF-8): bytes written
static int entity(const char *e, char *out)
{
    if (!strcmp(e, "amp")) { out[0] = '&'; return 1; }
    if (!strcmp(e, "lt")) { out[0] = '<'; return 1; }
    if (!strcmp(e, "gt")) { out[0] = '>'; return 1; }
    if (!strcmp(e, "quot")) { out[0] = '"'; return 1; }
    if (!strcmp(e, "apos")) { out[0] = '\''; return 1; }
    if (!strcmp(e, "nbsp")) { out[0] = ' '; return 1; }
    unsigned long c = 0;
    if (e[0] == '#') c = (e[1] == 'x' || e[1] == 'X') ? strtoul(e + 2, NULL, 16) : strtoul(e + 1, NULL, 10);
    if (!c) return -1;
    if (c < 0x80) { out[0] = (char)c; return 1; }
    if (c < 0x800) { out[0] = (char)(0xC0 | c >> 6); out[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) { out[0] = (char)(0xE0 | c >> 12); out[1] = (char)(0x80 | ((c >> 6) & 63)); out[2] = (char)(0x80 | (c & 63)); return 3; }
    return -1;
}

// the page's text and tags: text(c) for each character (entities made into text), tag(t) for each tag
static void html_feed(const uint8_t *p, size_t n, void (*text)(const char *s, size_t n), void (*tag)(const char *t))
{
    for (size_t i = 0; i < n; i++)
    {
        char c = (char)p[i];
        if (H.in_tag)
        {
            if (c == '>')
            {
                H.tag[H.tag_len] = 0;
                H.in_tag = false;
                tag(H.tag);
            }
            else if (H.tag_len < sizeof H.tag - 1) H.tag[H.tag_len++] = c;
            continue;
        }
        if (H.in_ent)
        {
            if (c == ';' || H.ent_len >= sizeof H.ent - 1 || c == '<' || c == ' ')
            {
                H.ent[H.ent_len] = 0;
                H.in_ent = false;
                char o[4];
                int k = c == ';' ? entity(H.ent, o) : -1;
                if (k > 0) text(o, (size_t)k);
                else { text("&", 1); text(H.ent, H.ent_len); if (c != ';' && c != '<') text(&c, 1); else if (c == ';') text(";", 1); }
                if (c == '<') { H.in_tag = true; H.tag_len = 0; }
                continue;
            }
            H.ent[H.ent_len++] = c;
            continue;
        }
        if (c == '<') { H.in_tag = true; H.tag_len = 0; continue; }
        if (c == '&') { H.in_ent = true; H.ent_len = 0; continue; }
        text(&c, 1);
    }
}

// ---------------------------------------------------------------- the song list
// Rows: <tr class="list_trN" data-songid="ID" ...> (details=1), cells: sample, cover,
// artist, title (a link), genre, year, edition, golden, language, ...
// or (the plain list) <td onclick="show_detail(ID)">artist</td> title, edition, golden, language
static struct {
    bool in_row, in_cell, detail_rows;
    uint32_t id;
    int cell;
    char cells[9][48];
    size_t len;
} R;

static void row_end(void)
{
    if (!R.id || n_hits >= USDB_MAX) return;
    int a, t, l, e;
    if (R.detail_rows) { a = 2; t = 3; e = 6; l = 8; }
    else { a = 0; t = 1; e = 2; l = 4; }
    if (R.cell <= t) return;
    usdb_hit_t *h = &hits[n_hits];
    memset(h, 0, sizeof *h);
    h->id = R.id;
    song_to_latin1(h->artist, sizeof h->artist, R.cells[a], strlen(R.cells[a]));
    song_to_latin1(h->title, sizeof h->title, R.cells[t], strlen(R.cells[t]));
    if (R.cell > l) song_to_latin1(h->lang, sizeof h->lang, R.cells[l], strlen(R.cells[l]));
    if (R.cell > e) song_to_latin1(h->edition, sizeof h->edition, R.cells[e], strlen(R.cells[e]));
    if (!h->artist[0] && !h->title[0]) return;
    n_hits++;
    page_rows++;
}

static void list_text(const char *s, size_t n)
{
    if (!R.in_cell || R.cell >= 9) return;
    for (size_t i = 0; i < n; i++)
    {
        char c = s[i] == '\n' || s[i] == '\t' || s[i] == '\r' ? ' ' : s[i];
        if (c == ' ' && (R.len == 0 || R.cells[R.cell][R.len - 1] == ' ')) continue;
        if (R.len < sizeof R.cells[0] - 1) R.cells[R.cell][R.len++] = c;
    }
}

static uint32_t attr_num(const char *t, const char *name)
{
    const char *p = strstr(t, name);
    if (!p) return 0;
    p += strlen(name);
    while (*p && !isdigit((unsigned char)*p)) p++;
    return (uint32_t)strtoul(p, NULL, 10);
}

static void list_tag(const char *t)
{
    bool close = t[0] == '/';
    const char *name = close ? t + 1 : t;
    if (!strncasecmp(name, "tr", 2) && (name[2] == 0 || isspace((unsigned char)name[2])))
    {
        if (close) { if (R.in_row) row_end(); R.in_row = false; R.id = 0; return; }
        if (R.in_row) row_end();
        memset(&R.cells, 0, sizeof R.cells);
        R.in_row = true;
        R.cell = -1;
        R.in_cell = false;
        R.id = attr_num(t, "data-songid=");
        R.detail_rows = R.id != 0;
        return;
    }
    if (!R.in_row) return;
    if (!strncasecmp(name, "td", 2) && (name[2] == 0 || isspace((unsigned char)name[2])))
    {
        if (close)
        {
            if (R.in_cell && R.cell >= 0 && R.cell < 9)
            {
                while (R.len && R.cells[R.cell][R.len - 1] == ' ') R.len--;
                R.cells[R.cell][R.len] = 0;
            }
            R.in_cell = false;
            return;
        }
        R.cell++;
        R.len = 0;
        R.in_cell = true;
        if (!R.id && strstr(t, "show_detail(")) { R.id = attr_num(t, "show_detail("); R.detail_rows = false; }
    }
}

static bool list_body(web_t *ww, const uint8_t *p, size_t n)
{
    (void)ww;
    markers(p, n);
    html_feed(p, n, list_text, list_tag);
    return false;
}

// ---------------------------------------------------------------- the .txt
static bool in_textarea;
static void txt_text(const char *s, size_t n)
{
    if (!in_textarea || fh < 0) return;
    if (file_bytes == 0)
    {
        // the text starts after the tag's line end
        while (n && (s[0] == '\n' || s[0] == '\r')) { s++; n--; }
    }
    if (!n) return;
    plat_fwrite(fh, s, n);
    file_bytes += (uint32_t)n;
}
static void txt_tag(const char *t)
{
    if (!strncasecmp(t, "textarea", 8) && !got_textarea) { in_textarea = true; got_textarea = true; file_bytes = 0; }
    else if (!strncasecmp(t, "/textarea", 9)) in_textarea = false;
}
static bool txt_body(web_t *ww, const uint8_t *p, size_t n)
{
    (void)ww;
    markers(p, n);
    html_feed(p, n, txt_text, txt_tag);
    return false;
}

static bool cover_body(web_t *ww, const uint8_t *p, size_t n)
{
    (void)ww;
    if (fh < 0) return true;
    if (file_bytes == 0 && n >= 2) cover_jpeg = p[0] == 0xFF && p[1] == 0xD8;
    if (!cover_jpeg) return true;
    plat_fwrite(fh, p, n);
    file_bytes += (uint32_t)n;
    return false;
}

static bool login_body(web_t *ww, const uint8_t *p, size_t n)
{
    (void)ww;
    markers(p, n);
    if (via_pc)
    {
        // "ok", "bad", or why not
        size_t k = strlen(form);
        for (size_t i = 0; i < n && k < 60; i++) form[k++] = (char)p[i];
        form[k] = 0;
    }
    return false;
}

// ---------------------------------------------------------------- the steps
static void step_done(web_t *ww, bool ok);

static bool go(const char *url, const char *body, web_body_fn fn)
{
    not_logged = login_bad = false;
    tail[0] = 0;
    html_reset();
    if (!web_start(&w, url, body, body ? strlen(body) : 0, fn, step_done, false))
    {
        char m[48];
        snprintf(m, sizeof m, "NO WIFI LINK (%s)", web_error_text(&w));
        set_msg(m);
        state = U_ERROR;
        return false;
    }
    return true;
}

static void esc(char *out, size_t size, const char *s) { if (!web_escape(out, size, s)) out[0] = 0; }

static bool start_login(void)
{
    step = S_LOGIN;
    set_msg("LOGGING IN...");
    char u[100], p[100], url[300];
    esc(u, sizeof u, g_set.usdb_user);
    esc(p, sizeof p, g_set.usdb_pass);
    if (via_pc)
    {
        form[0] = 0;
        snprintf(url, sizeof url, "http://%s/usdb/login?u=%s&p=%s", g_set.pc, u, p);
        return go(url, NULL, login_body);
    }
    if (!g_set.usdb_user[0]) { set_msg("SET YOUR USDB LOGIN IN SETTINGS"); state = U_ERROR; return false; }
    snprintf(form, sizeof form, "user=%s&pass=%s&login=Login", u, p);
    snprintf(url, sizeof url, "%sindex.php?link=login", usdb_base);
    return go(url, form, login_body);
}

static bool start_work(void)
{
    step = S_WORK;
    char url[400], a[100], t[100];
    if (job == J_SEARCH)
    {
        set_msg("SEARCHING...");
        memset(&R, 0, sizeof R);
        page_rows = 0;
        esc(a, sizeof a, q_artist);
        esc(t, sizeof t, q_title);
        if (via_pc)
        {
            snprintf(url, sizeof url, "http://%s/usdb/list?interpret=%s&title=%s&start=%d&limit=%d", g_set.pc, a, t, q_start, PAGE);
            return go(url, NULL, list_body);
        }
        snprintf(form, sizeof form, "interpret=%s&title=%s&edition=&language=&genre=&order=rating&ud=desc&limit=%d&start=%d&details=1",
                 a, t, PAGE, q_start);
        snprintf(url, sizeof url, "%sindex.php?link=list", usdb_base);
        return go(url, form, list_body);
    }
    if (job == J_TXT)
    {
        set_msg("GETTING THE SONG...");
        got_textarea = in_textarea = false;
        file_bytes = 0;
        fh = plat_fopen(txt_path, true);
        if (fh < 0) { set_msg("CANNOT WRITE ON THE CARD"); state = U_ERROR; return false; }
        plat_ftruncate(fh);
        unsigned id = (unsigned)hits[dl_i].id;
        if (via_pc)
        {
            snprintf(url, sizeof url, "http://%s/usdb/txt?id=%u", g_set.pc, id);
            return go(url, NULL, txt_body);
        }
        snprintf(form, sizeof form, "wd=1");
        snprintf(url, sizeof url, "%sindex.php?link=gettxt&id=%u", usdb_base, id);
        return go(url, form, txt_body);
    }
    // the cover
    set_msg("GETTING THE COVER...");
    file_bytes = 0;
    cover_jpeg = false;
    fh = plat_fopen(cover_path, true);
    if (fh < 0) { state = U_SAVED; set_msg("SAVED (NO COVER)"); return true; }
    plat_ftruncate(fh);
    unsigned id = (unsigned)hits[dl_i].id;
    if (via_pc) snprintf(url, sizeof url, "http://%s/usdb/cover?id=%u", g_set.pc, id);
    else snprintf(url, sizeof url, "%sdata/cover/%u.jpg", usdb_base, id);
    return go(url, NULL, cover_body);
}

// the ESP could not reach USDB: PicoAmp for Windows fetches for us (once tried, it stays so)
static bool to_pc(web_t *ww)
{
    uint8_t e = web_error(ww);
    if (via_pc || tried_pc || !g_set.pc[0]) return false;
    if (e != WEB_E_DNS && e != WEB_E_CONNECT && e != WEB_E_TIMEOUT && e != WEB_E_CLOSED) return false;
    tried_pc = via_pc = true;
    logged = false;
    return true;
}

static void close_file(void) { if (fh >= 0) { plat_fclose(fh); fh = -1; } }

static void fail(web_t *ww, const char *what)
{
    char m[48];
    snprintf(m, sizeof m, "%s: %s", what, web_error_text(ww));
    set_msg(m);
    state = U_ERROR;
}

static void step_done(web_t *ww, bool ok)
{
    if (step == S_LOGIN)
    {
        if (!ok)
        {
            if (to_pc(ww)) { start_login(); return; }
            fail(ww, "USDB");
            return;
        }
        if (via_pc)
        {
            if (strncmp(form, "ok", 2)) { set_msg(!strncmp(form, "bad", 3) ? "USDB: WRONG NAME OR PASSWORD" : "THE PC COULD NOT LOG IN"); state = U_ERROR; return; }
        }
        else if (login_bad) { set_msg("USDB: WRONG NAME OR PASSWORD"); state = U_ERROR; return; }
        logged = true;
        start_work();
        return;
    }
    // a page of work
    if (job == J_TXT || job == J_COVER) close_file();
    bool need_login = not_logged || (via_pc && web_http(ww) == 401);
    if (need_login && !relogged)
    {
        relogged = true;
        logged = false;
        if (job == J_TXT) plat_remove(txt_path);
        start_login();
        return;
    }
    if (job == J_SEARCH)
    {
        if (!ok)
        {
            if (to_pc(ww)) { start_login(); return; }
            fail(ww, "USDB");
            return;
        }
        if (need_login) { set_msg("USDB DID NOT LET US IN"); state = U_ERROR; return; }
        state = U_RESULTS;
        if (n_hits == 0) set_msg("NOTHING FOUND");
        else set_msg("");
        return;
    }
    if (job == J_TXT)
    {
        if (!ok || !got_textarea || file_bytes < 20)
        {
            plat_remove(txt_path);
            if (!ok && to_pc(ww)) { start_login(); return; }
            if (!ok) fail(ww, "USDB");
            else { set_msg(need_login ? "USDB DID NOT LET US IN" : "USDB SENT NO SONG TEXT"); state = U_ERROR; }
            return;
        }
        job = J_COVER;
        start_work();
        return;
    }
    // the cover: nice to have
    if (!ok || !cover_jpeg || file_bytes < 100) plat_remove(cover_path);
    snprintf(saved, sizeof saved, "%s", txt_path);
    state = U_SAVED;
    set_msg(file_bytes >= 100 && cover_jpeg ? "SAVED WITH ITS COVER" : "SAVED");
}

static bool begin(int j)
{
    if (!ready) { web_init(&w); ready = true; }
    if (web_busy(&w)) return false;
    job = j;
    relogged = false;
    state = U_BUSY;
    return logged ? start_work() : start_login();
}

bool usdb_search(const char *artist, const char *title)
{
    snprintf(q_artist, sizeof q_artist, "%s", artist);
    snprintf(q_title, sizeof q_title, "%s", title);
    q_start = 0;
    n_hits = 0;
    page_rows = 0;
    return begin(J_SEARCH);
}

bool usdb_more(void)
{
    if (!usdb_has_more()) return false;
    q_start += PAGE;
    return begin(J_SEARCH);
}

// a name for the card: no \ / : * ? " < > |, no letters FatFs may not take
static void clean(char *out, size_t size, const char *s)
{
    size_t o = 0;
    for (; *s && o + 1 < size; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126 || strchr("\\/:*?\"<>|", c)) c = c > 126 ? '_' : ' ';
        if (c == ' ' && (o == 0 || out[o - 1] == ' ')) continue;
        out[o++] = (char)c;
    }
    while (o && (out[o - 1] == ' ' || out[o - 1] == '.')) o--;
    out[o] = 0;
}

bool usdb_download(int i)
{
    if (i < 0 || i >= n_hits || (ready && web_busy(&w))) return false;
    dl_i = i;
    char a[40], t[40], name[90];
    clean(a, sizeof a, hits[i].artist);
    clean(t, sizeof t, hits[i].title);
    snprintf(name, sizeof name, "%s - %s", a[0] ? a : "Unknown", t[0] ? t : "Song");
    snprintf(folder, sizeof folder, "%s/USDB/%s", plat_song_root(), name);
    if (!plat_mkdir(folder)) { set_msg("CANNOT MAKE THE FOLDER"); state = U_ERROR; return false; }
    snprintf(txt_path, sizeof txt_path, "%s/%s.txt", folder, name);
    snprintf(cover_path, sizeof cover_path, "%s/%s [CO].jpg", folder, name);
    saved[0] = 0;
    return begin(J_TXT);
}

void usdb_cancel(void)
{
    if (ready && web_busy(&w)) web_abort(&w);
}

void usdb_poll(void)
{
    if (ready) web_poll(&w);
    if (!ready || web_busy(&w)) return;
    // a write left open by an abort
    if (state != U_BUSY) close_file();
}
