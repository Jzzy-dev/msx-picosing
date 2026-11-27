// PicoSing - test_online: finding songs on USDB and singing with the PC's
// player, against a simulated ESP-01, a fake USDB and a fake PicoAmp for
// Windows (its /screen, /cmd, /sing/open and /usdb/ bridge).
// usage: test_online <songs> <out>

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include "simplat.h"
#include "fake_esp.h"
#include "../pico/game/game.h"
#include "../pico/game/fb.h"
#include "../pico/game/library.h"
#include "../pico/game/settings.h"
#include "../pico/game/usdb.h"
#include "../pico/game/pclink.h"
#include "../pico/game/music.h"
#include "../pico/game/platform.h"
#include "../pico/game/pcfs.h"
#include "../pico/game/game_int.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static const char *outdir;
static char song_txt[20000];
static uint8_t *cover;
static size_t cover_len;

// ---------------------------------------------------------------- fake USDB
static int u_logins, u_lists, u_txts, u_covers, u_bad_logins;
static bool u_expire_once;        // the next page says "not logged in" (a session that ran out)
static char u_last_form[400];

static const char *form_val(const char *form, const char *key, char *out, size_t size)
{
    char k[32];
    snprintf(k, sizeof k, "%s=", key);
    const char *p = form;
    out[0] = 0;
    while ((p = strstr(p, k)))
    {
        if (p == form || p[-1] == '&' || p[-1] == '?')
        {
            p += strlen(k);
            size_t n = 0;
            while (*p && *p != '&' && *p != ' ' && n + 1 < size)
            {
                if (*p == '+') out[n++] = ' ', p++;
                else if (*p == '%' && p[1] && p[2]) { char h[3] = { p[1], p[2], 0 }; out[n++] = (char)strtol(h, NULL, 16); p += 3; }
                else out[n++] = *p++;
            }
            out[n] = 0;
            return out;
        }
        p++;
    }
    return NULL;
}

static void list_page(fesp_conn_t *c, const char *form)
{
    char a[64], t[64], st[16], lim[16];
    form_val(form, "interpret", a, sizeof a);
    form_val(form, "title", t, sizeof t);
    form_val(form, "start", st, sizeof st);
    form_val(form, "limit", lim, sizeof lim);
    int start = atoi(st), limit = atoi(lim) ? atoi(lim) : 20;
    fesp_outs(c, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html><body><div class=\"row1\"><table>\n"
                 "<tr class=\"list_head\"><td>Sample</td><td>Cover</td><td>Artist</td><td>Title</td></tr>\n");
    // "abba": Dancing Queen (1234) and Waterloo; "a": 25 songs (pages); "simon": an &amp;
    struct { int id; const char *ar, *ti, *lang; } all[30];
    int n = 0;
    if (strcasestr("ABBA", a) && a[0] && strcasecmp(a, "a"))
    {
        all[n++] = (typeof(all[0])){ 1234, "ABBA", "Dancing Queen", "English" };
        all[n++] = (typeof(all[0])){ 1235, "ABBA", "Waterloo", "English" };
    }
    else if (!strcasecmp(a, "a")) for (int i = 0; i < 25; i++) all[n++] = (typeof(all[0])){ 2000 + i, "A Band", "Song", "German" };
    else if (strcasestr(a, "simon")) all[n++] = (typeof(all[0])){ 3000, "Simon &amp; Garfunkel", "The Boxer &#039;69", "English" };
    char row[900];
    for (int i = start; i < n && i < start + limit; i++)
    {
        snprintf(row, sizeof row,
                 "<tr class=\"list_tr%d\" data-songid=\"%d\" data-lastchange=\"1700000000\">"
                 "<td><audio><source src=\"data/sample/%d.mp3\" type=\"audio/mpeg\"></audio></td><td><img src=\"data/cover/%d.jpg\" width=\"20\"></td><td>%s</td>\n"
                 "<td><a href=\"?link=detail&id=%d\">%s%s</a></td>\n<td>Pop</td>\n<td>1976</td>\n<td>SingStar</td>\n<td>Ja</td>\n<td>%s</td>\n<td>IC</td>\n<td>5</td>\n<td>%d</td></tr>\n",
                 1 + i % 2, all[i].id, all[i].id, all[i].id, all[i].ar, all[i].id, all[i].ti, n > 2 ? "" : "", all[i].lang, 100 + i);
        fesp_outs(c, row);
    }
    fesp_outs(c, "</table></div></body></html>\n");
    fesp_close(c);
}

static void txt_page(fesp_conn_t *c)
{
    fesp_outs(c, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html><body><form><textarea cols=\"80\" rows=\"30\" name=\"txt\">\n");
    // the song's text, as HTML (& < > quoted)
    for (const char *p = song_txt; *p; p++)
    {
        if (*p == '&') fesp_outs(c, "&amp;");
        else if (*p == '<') fesp_outs(c, "&lt;");
        else if (*p == '>') fesp_outs(c, "&gt;");
        else fesp_out(c, p, 1);
    }
    fesp_outs(c, "</textarea></form></body></html>\n");
    fesp_close(c);
}

static void cover_reply(fesp_conn_t *c, int id)
{
    if (id != 1234) { fesp_outs(c, "HTTP/1.1 404 Not Found\r\n\r\nno"); fesp_close(c); return; }
    char h[100];
    snprintf(h, sizeof h, "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", cover_len);
    fesp_outs(c, h);
    fesp_out(c, cover, cover_len);
    fesp_close(c);
}

static void usdb_serve(fesp_conn_t *c, const char *req, size_t len)
{
    const char *body = strstr(req, "\r\n\r\n");
    body = body ? body + 4 : "";
    bool cookie = strstr(req, "Cookie: PHPSESSID=s1") != NULL;
    snprintf(u_last_form, sizeof u_last_form, "%s", body);
    if (!strncmp(req, "POST /index.php?link=login ", 27))
    {
        char u[40], p[40];
        form_val(body, "user", u, sizeof u);
        form_val(body, "pass", p, sizeof p);
        u_logins++;
        if (!strcmp(u, "jzzy") && !strcmp(p, "s3cret&x"))
            fesp_outs(c, "HTTP/1.1 200 OK\r\nSet-Cookie: PHPSESSID=s1; path=/\r\n\r\n<html>Welcome jzzy</html>");
        else { u_bad_logins++; fesp_outs(c, "HTTP/1.1 200 OK\r\n\r\n<html>Login or Password invalid, please try again.</html>"); }
        fesp_close(c);
        return;
    }
    bool expired = u_expire_once && !strncmp(req, "POST /index.php?link=gettxt", 27);
    if ((!cookie || expired) && strncmp(req, "GET /data/cover/", 16))
    {
        if (expired) u_expire_once = false;
        fesp_outs(c, "HTTP/1.1 200 OK\r\n\r\n<html>ERROR: You are not logged in. Login to use this function.</html>");
        fesp_close(c);
        return;
    }
    if (!strncmp(req, "POST /index.php?link=list ", 26)) { u_lists++; list_page(c, body); return; }
    if (!strncmp(req, "POST /index.php?link=gettxt&id=1234 ", 36) && !strcmp(body, "wd=1")) { u_txts++; txt_page(c); return; }
    if (!strncmp(req, "GET /data/cover/", 16)) { u_covers++; cover_reply(c, atoi(req + 16)); return; }
    fesp_outs(c, "HTTP/1.1 404 Not Found\r\n\r\n");
    fesp_close(c);
    (void)len;
}

// ---------------------------------------------------------------- fake PicoAmp for Windows
static struct {
    bool playing;
    int32_t base, since;          // its time: base + (now - since) while it plays
    int volume;
    char title[80];
    int opens, pauses, plays, seeks, vols, last_seek_s;
    char last_open[200];
    uint32_t starts_at;           // the opened video plays from then on
    bool usdb_logged;
    int sing_reqs;
} pc;

static int32_t pc_time(void) { return pc.playing ? pc.base + (int32_t)(now - pc.since) : pc.base; }
static void pc_set_playing(bool on) { pc.base = pc_time(); pc.since = (int32_t)now; pc.playing = on; }

typedef struct { uint32_t last_t, last_v; char sent_title[80]; } scr_t;

static void url_dec(char *o, size_t n, const char *s)
{
    size_t k = 0;
    for (; *s && *s != '&' && k + 1 < n; s++)
    {
        if (*s == '%' && s[1] && s[2]) { char h[3] = { s[1], s[2], 0 }; o[k++] = (char)strtol(h, NULL, 16); s += 2; }
        else o[k++] = *s == '+' ? ' ' : *s;
    }
    o[k] = 0;
}

static void pc_serve(fesp_conn_t *c, const char *req, size_t len)
{
    (void)len;
    char path[300] = "";
    sscanf(req, "GET %299s", path);
    char v[200];
    if (!strncmp(path, "/screen", 7))
    {
        fesp_outs(c, "HTTP/1.0 200 OK\r\nContent-Type: text/x-picoamp-screen\r\nX-PicoAmp: 2\r\n\r\n");
        scr_t *s = calloc(1, sizeof *s);
        s->last_t = now - 1000;
        *fesp_user(c) = s;
        return;
    }
    // the PC's song folder (PicoAmp for Windows' karaoke folder)
    if (!strncmp(path, "/sing/songs", 11) || !strncmp(path, "/sing/list", 10) || !strncmp(path, "/sing/file", 10))
    {
        char rel[300] = "";
        const char *q = strstr(path, "path=");
        if (q) url_dec(rel, sizeof rel, q + 5);
        pc.sing_reqs++;
        if (!strncmp(path, "/sing/songs", 11))
        {
            fesp_outs(c, "HTTP/1.0 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n"
                         "Karaoke/Twinkle Twinkle Little Star/Twinkle Twinkle Little Star.txt\tTwinkle\tCaf\xc3\xa9 Star\t0\n"
                         "Karaoke/Duo/duet.txt\tA Pair\tDuo\t1\n");
        }
        else if (!strncmp(path, "/sing/list", 10))
        {
            const char *b = !rel[0] ? "d Karaoke\nf notes.txt\n" : !strcmp(rel, "Karaoke") ? "d Twinkle Twinkle Little Star\nd Duo\nd Caf\xc3\xa9\n" :
                            !strcmp(rel, "Karaoke/Twinkle Twinkle Little Star") ? "f Twinkle Twinkle Little Star.txt\nf Twinkle Twinkle Little Star.mp3\nf cover.jpg\n" : NULL;
            if (!b) fesp_outs(c, "HTTP/1.0 404 Not Found\r\n\r\nno such folder\n");
            else { fesp_outs(c, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n"); fesp_outs(c, b); }
        }
        else if (!strcmp(rel, "Karaoke/Twinkle Twinkle Little Star/Twinkle Twinkle Little Star.txt"))
        {
            fesp_outs(c, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n");
            fesp_outs(c, song_txt);
        }
        else if (!strcmp(rel, "Karaoke/Twinkle Twinkle Little Star/cover.jpg"))
        {
            char h[120];
            snprintf(h, sizeof h, "HTTP/1.0 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)cover_len);
            fesp_outs(c, h);
            fesp_out(c, cover, cover_len);
        }
        else fesp_outs(c, "HTTP/1.0 404 Not Found\r\n\r\nnot here\n");
        fesp_close(c);
        return;
    }
    if (!strncmp(path, "/sing/open", 10))
    {
        pc.opens++;
        snprintf(pc.last_open, sizeof pc.last_open, "%s", path);
        if (form_val(path, "yt", v, sizeof v)) snprintf(pc.title, sizeof pc.title, "Twinkle (video %s)", v);
        else { form_val(path, "q", v, sizeof v); snprintf(pc.title, sizeof pc.title, "Spotify: %s", v); }
        pc.playing = false;
        pc.base = 0;
        pc.starts_at = now + 1500;            // the browser opens, the video starts
        fesp_outs(c, "HTTP/1.0 200 OK\r\n\r\nok\n");
        fesp_close(c);
        return;
    }
    if (!strncmp(path, "/cmd", 4))
    {
        form_val(path, "k", v, sizeof v);
        if (!strcmp(v, "pause")) { pc.pauses++; if (pc.playing) pc_set_playing(false); }
        else if (!strcmp(v, "play")) { pc.plays++; if (!pc.playing) pc_set_playing(true); }
        else if (!strcmp(v, "seek")) { form_val(path, "to", v, sizeof v); pc.seeks++; pc.last_seek_s = atoi(v); pc.base = atoi(v) * 1000; pc.since = (int32_t)now; }
        else if (!strcmp(v, "vol")) { form_val(path, "v", v, sizeof v); pc.vols++; pc.volume = atoi(v); }
        fesp_outs(c, "HTTP/1.0 200 OK\r\n\r\nok\n");
        fesp_close(c);
        return;
    }
    if (!strncmp(path, "/usdb/", 6))
    {
        // the bridge: the same pages, the PC logged in
        if (!strncmp(path, "/usdb/login", 11))
        {
            char u[40], p[40];
            form_val(path, "u", u, sizeof u);
            form_val(path, "p", p, sizeof p);
            pc.usdb_logged = !strcmp(u, "jzzy") && !strcmp(p, "s3cret&x");
            fesp_outs(c, pc.usdb_logged ? "HTTP/1.0 200 OK\r\n\r\nok\n" : "HTTP/1.0 200 OK\r\n\r\nbad\n");
            fesp_close(c);
            return;
        }
        if (!pc.usdb_logged) { fesp_outs(c, "HTTP/1.0 200 OK\r\n\r\nYou are not logged in. Login to use this function."); fesp_close(c); return; }
        if (!strncmp(path, "/usdb/list", 10)) { u_lists++; list_page(c, strchr(path, '?') + 1); return; }
        if (!strncmp(path, "/usdb/txt?id=1234", 17)) { u_txts++; txt_page(c); return; }
        if (!strncmp(path, "/usdb/cover?id=", 15)) { u_covers++; cover_reply(c, atoi(path + 15)); return; }
    }
    fesp_outs(c, "HTTP/1.0 404 Not Found\r\n\r\n");
    fesp_close(c);
}

static void pc_tick(fesp_conn_t *c, uint32_t t)
{
    scr_t *s = *fesp_user(c);
    if (!s) return;
    if (pc.starts_at && t >= pc.starts_at) { pc.starts_at = 0; pc.base = 0; pc.since = (int32_t)t; pc.playing = true; }
    if (strcmp(s->sent_title, pc.title))
    {
        char l[120];
        snprintf(l, sizeof l, "S%s\t%s\t\n", "", pc.title);
        fesp_outs(c, l);
        snprintf(s->sent_title, sizeof s->sent_title, "%s", pc.title);
    }
    if (t - s->last_t >= 500)
    {
        char l[80];
        snprintf(l, sizeof l, "T %d 200 %c %d 0\n", (int)pc_time(), pc.playing ? 'p' : 'a', pc.volume);
        fesp_outs(c, l);
        s->last_t = t;
    }
    if (t - s->last_v >= 40)
    {
        fesp_outs(c, "V00000000000000000000000000AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
        s->last_v = t;
    }
}
static void pc_gone(fesp_conn_t *c) { free(*fesp_user(c)); *fesp_user(c) = NULL; }

// what the singers hear: the PC's video, the song starting #VIDEOGAP into it
static int32_t video_gap;
static int32_t heard(void) { int32_t t = pc_time(); return pc.title[0] && (pc.playing || pc.base) ? t - video_gap : -1; }

// ---------------------------------------------------------------- driving the game
static fb_t scr;
static void run(uint32_t ms, int skill1)
{
    uint32_t end = now + ms;
    while (now < end)
    {
        now++;
        fesp_tick(now);
        if (now % 23 == 0) { sim_sing(0, now, 0); sim_sing(1, now, skill1); }
        if (now % 10 == 0) game_update();
        if (now % 40 == 0) game_draw(&scr);
    }
}
static void keys(const char *s) { for (; *s; s++) { game_key((uint8_t)*s); run(20, 0); } }

static void shot(const char *name)
{
    uint8_t pal[32];
    game_draw(&scr);
    game_palette(pal);
    char p[600];
    snprintf(p, sizeof p, "%s/%s.ppm", outdir, name);
    FILE *f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "P6\n256 212\n255\n");
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++)
        {
            uint8_t b = scr.px[y][x >> 1], c = (x & 1) ? (b & 15) : (b >> 4);
            uint8_t rgb[3] = { (uint8_t)(((pal[c * 2] >> 4) & 7) * 255 / 7), (uint8_t)((pal[c * 2 + 1] & 7) * 255 / 7), (uint8_t)((pal[c * 2] & 7) * 255 / 7) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static char *load(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(*n + 1);
    if (fread(b, 1, *n, f) != *n) { free(b); fclose(f); return NULL; }
    b[*n] = 0;
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: test_online <songs> <out>\n"); return 2; }
    outdir = argv[2];
    char cmd[1600], card[600], p[900];
    snprintf(cmd, sizeof cmd, "rm -rf '%s/card' '%s/data' && mkdir -p '%s/card/KARAOKE' '%s/data'", outdir, outdir, outdir, outdir);
    if (system(cmd)) return 2;
    snprintf(card, sizeof card, "%s/card", outdir);
    static char data[600];
    snprintf(data, sizeof data, "%s/data", outdir);
    sim_data = data;

    // the song USDB has: the test song, its music on YouTube only
    size_t n;
    snprintf(p, sizeof p, "%s/KARAOKE/Twinkle Twinkle Little Star/Twinkle Twinkle Little Star.txt", argv[1]);
    char *t = load(p, &n);
    snprintf(p, sizeof p, "%s/KARAOKE/Twinkle Twinkle Little Star/cover.jpg", argv[1]);
    cover = (uint8_t *)load(p, &cover_len);
    if (!t || !cover) { printf("no test song\n"); return 2; }
    char *mp3 = strstr(t, "#MP3:");
    char *eol = mp3 ? strchr(mp3, '\n') : NULL;
    snprintf(song_txt, sizeof song_txt, "%.*s#MP3:Twinkle Twinkle Little Star.mp3\n#VIDEO:v=dQw4w9WgXcQ,co=twinkle.jpg\n#VIDEOGAP:1.5\n%s",
             (int)(mp3 - t), t, eol + 1);
    video_gap = 1500;

    fesp_host_t hu = { "usdb.animux.de", 30, 443, true, false, usdb_serve, NULL, NULL };
    fesp_add_host(&hu);
    fesp_host_t hp = { "pc.test", 14, 8765, false, false, pc_serve, pc_tick, pc_gone };
    fesp_add_host(&hp);

    sim_scan(card);
    now = 1000;
    game_init();
    CHECK(lib_count() == 0, "an empty card");
    keys(" ");                                   // title -> main
    CHECK(game_state() == GS_MAIN, "main");

    // ---- settings: the PC (":8765" is added), the USDB login (a wrong password first)
    for (int i = 0; i < 5; i++) game_key(KEY_DOWN);
    keys("\r");
    CHECK(game_state() == GS_SETTINGS, "settings");
    game_key(KEY_DOWN);
    keys("http://pc.test:8765/");                  // (as the PC shows it: made into pc.test:8765)
    game_key(KEY_DOWN);                          // test
    keys("\r");
    run(2500, 0);
    printf("PC test: %s\n", pc_message());
    CHECK(strstr(pc_message(), "PC FOUND"), "the PC answers: %s", pc_message());
    game_key(KEY_DOWN);
    keys("jzzy\rwrong");
    shot("o1_settings");
    keys("\x1b");
    CHECK(!strcmp(g_set.pc, "pc.test:8765") && !strcmp(g_set.usdb_user, "jzzy"), "settings: '%s' '%s'", g_set.pc, g_set.usdb_user);
    snprintf(p, sizeof p, "%s/SETTINGS.TXT", data);
    char *st = load(p, &n);
    CHECK(st && strstr(st, "PC=pc.test:8765") && strstr(st, "USDB_USER=jzzy"), "settings saved");
    free(st);

    // ---- find songs: the wrong password
    game_key(KEY_UP);                            // (main: FIND SONGS is above SETTINGS)
    keys("\r");
    CHECK(game_state() == GS_USDB, "find songs (%d)", game_state());
    keys("abba\r\r\r");
    run(4000, 0);
    printf("wrong password: %s\n", usdb_message());
    CHECK(usdb_state() == U_ERROR && strstr(usdb_message(), "WRONG"), "a wrong password is told: %s", usdb_message());

    // the right one (with an & in it: escaped)
    snprintf(g_set.usdb_pass, sizeof g_set.usdb_pass, "s3cret&x");
    keys("\r");                                  // SEARCH again
    run(5000, 0);
    printf("search: %s, %d found, logins %d\n", usdb_message(), usdb_count(), u_logins);
    CHECK(usdb_state() == U_RESULTS && usdb_count() == 2, "two songs: %d", usdb_count());
    if (usdb_count() == 2)
        CHECK(usdb_hit(0)->id == 1234 && !strcmp(usdb_hit(0)->artist, "ABBA") && !strcmp(usdb_hit(0)->title, "Dancing Queen") &&
              !strcmp(usdb_hit(0)->lang, "English"), "row: %u '%s' '%s' '%s'", (unsigned)usdb_hit(0)->id, usdb_hit(0)->artist, usdb_hit(0)->title, usdb_hit(0)->lang);
    CHECK(strstr(u_last_form, "interpret=abba") && strstr(u_last_form, "details=1"), "the search form: %s", u_last_form);
    shot("o2_results");

    // ---- save the first: the session runs out on the way (logs in again)
    u_expire_once = true;
    int logins = u_logins;
    keys("\r");                                  // (the cursor is on the first result)
    run(6000, 0);
    printf("download: %s\n", usdb_message());
    CHECK(usdb_state() == U_SAVED && u_logins == logins + 1, "saved after logging in again (%d)", u_logins - logins);
    snprintf(p, sizeof p, "%s/KARAOKE/USDB/ABBA - Dancing Queen/ABBA - Dancing Queen.txt", card);
    char *saved = load(p, &n);
    CHECK(saved && !strcmp(saved, song_txt), "the .txt as USDB has it (%zu bytes)", saved ? n : 0);
    free(saved);
    snprintf(p, sizeof p, "%s/KARAOKE/USDB/ABBA - Dancing Queen/ABBA - Dancing Queen [CO].jpg", card);
    char *cv = load(p, &n);
    CHECK(cv && n == cover_len, "the cover saved");
    free(cv);
    CHECK(lib_count() == 1, "in the song list at once (%d)", lib_count());
    shot("o3_saved");

    // ---- sing it: no MP3, so the PC's player plays the YouTube video
    keys("\r");                                  // RETURN on a saved song: its details
    run(800, 0);
    CHECK(game_state() == GS_DETAILS, "details of the saved song (%d)", game_state());
    shot("o4_details");
    sim_heard_ms = heard;
    keys("\r\r");                                // who sings, go
    CHECK(game_state() == GS_PLAY && music_on_pc(), "playing with the PC");
    run(300, 0);
    printf("waiting: %d '%s' state %d\n", music_waiting(), music_message(), pc_state());
    shot("o5_waiting");
    run(4000, 0);
    CHECK(pc.opens == 1 && strstr(pc.last_open, "/sing/open?yt=dQw4w9WgXcQ"), "the video opened: %s", pc.last_open);
    CHECK(pc_state() == PC_PLAYING, "following the PC's player (%d, %s)", pc_state(), pc_message());
    int32_t diff = music_ms() - (pc_time() - video_gap);
    printf("PicoSing's time vs the video's: %d ms\n", (int)diff);
    CHECK(abs(diff) < 60, "in time with the video (%d ms)", (int)diff);
    // volume, pause, go on
    game_key(KEY_UP);
    run(500, 0);
    CHECK(pc.vols >= 1 && pc.volume == g_set.volume, "volume to the PC: %d %d", pc.volume, g_set.volume);
    game_key(KEY_SPACE);
    run(1500, 0);
    int32_t a = music_ms();
    run(2000, 0);
    CHECK(pc.pauses == 1 && !pc.playing && music_ms() == a, "paused on the PC too");
    game_key(KEY_SPACE);
    run(2000, 0);
    CHECK(pc.playing && pc.plays == 1 && music_ms() > a, "plays on");
    diff = music_ms() - (pc_time() - video_gap);
    CHECK(abs(diff) < 100, "in time again (%d ms)", (int)diff);
    run(60000, 0);
    CHECK(game_state() == GS_RESULT, "the song ended (4 s after its last note) (%d)", game_state());
    printf("score with the PC's player: %d\n", game_score(0));
    CHECK(game_score(0) >= 9000, "sung in time with the video: %d", game_score(0));
    CHECK(pc.pauses == 2, "the PC's player paused at the end (%d)", pc.pauses);
    shot("o6_results");

    // ---- the ESP can't reach USDB: PicoAmp for Windows fetches for us
    fesp_host_down("usdb.animux.de", true);
    keys("\r\x1b");                              // results -> songs -> main
    CHECK(game_state() == GS_MAIN, "main again (%d)", game_state());
    keys("\r");                                  // (still on FIND SONGS)
    CHECK(game_state() == GS_USDB, "find songs again");
    game_key(KEY_UP); game_key(KEY_UP); game_key(KEY_UP);    // the artist field
    for (int i = 0; i < 5; i++) game_key(0x08);
    keys("simon\r\r\r");
    run(8000, 0);
    printf("via the PC: %s, %d found\n", usdb_message(), usdb_count());
    CHECK(usdb_via_pc() && usdb_state() == U_RESULTS && usdb_count() == 1, "found through the PC (%d)", usdb_count());
    if (usdb_count() == 1) CHECK(!strcmp(usdb_hit(0)->artist, "Simon & Garfunkel") && !strcmp(usdb_hit(0)->title, "The Boxer '69"),
                                 "entities: '%s' '%s'", usdb_hit(0)->artist, usdb_hit(0)->title);
    shot("o7_via_pc");

    // ---- paging: 25 songs, 20 a page
    keys("\x1b");
    keys("\r");
    game_key(KEY_UP); game_key(KEY_UP); game_key(KEY_UP);
    for (int i = 0; i < 6; i++) game_key(0x08);
    keys("a\r\r\r");
    run(8000, 0);
    CHECK(usdb_count() == 20 && usdb_has_more(), "a page: %d", usdb_count());
    usdb_more();
    run(8000, 0);
    CHECK(usdb_count() == 25 && !usdb_has_more(), "two pages: %d", usdb_count());

    // ---- a song with no video: a search for it on the PC (Spotify)
    static song_t s2;
    s2 = *game_song();
    s2.youtube[0] = 0;
    snprintf(s2.artist, sizeof s2.artist, "ABBA");
    snprintf(s2.title, sizeof s2.title, "Dancing Queen");
    pc_start(&s2);
    run(3000, 0);
    CHECK(strstr(pc.last_open, "/sing/open?q=ABBA+Dancing+Queen"), "a search: %s", pc.last_open);
    pc_stop();
    run(500, 0);

    // ---- the song folder on the PC: its songs, a song's text, its folders and cover; its music on the PC
    {
        extern int sim_previews;
        int r0 = pc.sing_reqs;
        CHECK(plat_set_song_root("PC:/Karaoke") && !strcmp(plat_song_root(), "PC:/Karaoke"), "the PC's folder chosen");
        CHECK(lib_count() == 2 && pc.sing_reqs == r0 + 1, "its 2 songs in one request (%d)", lib_count());
        int tw = -1;
        for (int i = 0; i < lib_count(); i++) if (!strcmp(lib_get(i)->artist, "Twinkle")) tw = i;
        CHECK(tw >= 0 && !strcmp(lib_get(tw)->title, "Caf\xE9 Star") && !lib_get(tw)->duet, "artist and title, its accent (Latin-1)");
        CHECK(tw >= 0 && !strcmp(lib_path(tw), "PC:/Karaoke/Twinkle Twinkle Little Star/Twinkle Twinkle Little Star.txt"), "its path: %s", tw >= 0 ? lib_path(tw) : "");
        int du = tw == 0 ? 1 : 0;
        CHECK(lib_get(du)->duet && !strcmp(lib_get(du)->title, "Duo"), "the duet");
        static song_t ps;
        char err[48] = "";
        CHECK(tw >= 0 && plat_song_load(tw, &ps, err, sizeof err) && ps.n_notes > 0 && !strcmp(ps.audio, "Twinkle Twinkle Little Star.mp3"),
              "its .txt from the PC: %d notes (%s)", ps.n_notes, err);
        CHECK(!plat_song_load(du, &ps, err, sizeof err) && !strcmp(err, "NOT ON THE PC"), "a .txt that is not there: %s", err);
        r0 = pc.sing_reqs;
        CHECK(plat_exists("PC:/Karaoke/Twinkle Twinkle Little Star/cover.jpg") && plat_exists("PC:/Karaoke/Twinkle Twinkle Little Star/COVER.JPG") &&
              !plat_exists("PC:/Karaoke/Twinkle Twinkle Little Star/folder.jpg") && pc.sing_reqs == r0 + 1, "exists: the folder asked once (%d)", pc.sing_reqs - r0);
        CHECK(plat_stream_open("PC:/Karaoke/Twinkle Twinkle Little Star/cover.jpg", 0, 0), "the cover streamed");
        static uint8_t got[1 << 20];
        size_t gl = 0, k;
        while ((k = plat_stream_read(got + gl, 700)) > 0 && gl < sizeof got - 700) gl += k;
        plat_stream_close();
        CHECK(gl == cover_len && !memcmp(got, cover, cover_len), "the cover's %u bytes, through the ring (%u)", (unsigned)cover_len, (unsigned)gl);
        CHECK(!plat_stream_open("PC:/Karaoke/x.mp3", 1000, 100), "a picture inside an MP3: not from the PC");
        plat_dirent_t de[8];
        int dn = plat_list_dir("PC:/Karaoke", de, 8);
        CHECK(dn == 3 && de[0].dir && !strcmp(de[0].name, "Twinkle Twinkle Little Star") && !strcmp(de[2].name, "Caf\xc3\xa9"), "a folder's list (%d)", dn);
        CHECK(plat_list_dir("PC:/Nope", de, 8) < 0, "a folder that is not there");
        int pv = sim_previews;
        CHECK(!plat_audio_preview(tw, &ps, 5000) && sim_previews == pv, "no preview (its music is on the PC)");
        CHECK(plat_fopen("PC:/Karaoke/x.txt", true) < 0 && !plat_remove("PC:/Karaoke/x.txt"), "nothing written there");
        // its music: the PC plays the MP3
        plat_song_load(tw, &ps, err, sizeof err);
        CHECK(music_start(tw, &ps, err, sizeof err) && music_on_pc(), "the music starts on the PC (%s)", err);
        run(3000, 0);
        CHECK(!strcmp(pc.last_open, "/sing/open?file=Karaoke%2FTwinkle+Twinkle+Little+Star%2FTwinkle+Twinkle+Little+Star.mp3"), "it asks for the MP3: %s", pc.last_open);
        run(2000, 0);
        CHECK(pc_state() == PC_PLAYING && music_ms() > 2800 && music_ms() < 4000, "and follows its time (%d: no #VIDEOGAP for the MP3)", (int)music_ms());
        music_stop();
        run(500, 0);
        // the PC does not answer: the card's folder stays
        fesp_host_down("pc.test", true);
        CHECK(!plat_set_song_root("PC:/Karaoke"), "no answer from the PC: not chosen");
        CHECK(!strcmp(plat_song_root(), "PC:/Karaoke") && lib_count() == 2, "the PC's folder stays chosen, its list as it was (%d)", lib_count());
        fesp_host_down("pc.test", false);
        char cardk[600];
        snprintf(cardk, sizeof cardk, "%s/KARAOKE", card);
        CHECK(plat_set_song_root(cardk) && !strcmp(plat_card_root(), cardk), "back on the card");

        // the song folder screen: from the card's top to the PC's folders, sing from one
        gi_go(GS_FOLDERS);
        run(50, 0);
        for (int i = 0; i < 5; i++) { game_key(0x08); run(50, 0); }     // BS: up to the card's top (the simulator's card is a PC path)
        shot("o8_card_top");
        game_key(KEY_DOWN);
        keys("\r");                             // [ THE PC'S FOLDERS ]
        run(50, 0);
        shot("o9_pc_folders");
        game_key(KEY_DOWN); game_key(KEY_DOWN);
        keys("\r");                             // Karaoke/
        run(50, 0);
        shot("o10_pc_karaoke");
        keys("\r");                             // [ SING FROM THIS FOLDER ]
        run(400, 0);
        CHECK(game_state() == GS_SONGS && lib_count() == 2 && !strcmp(plat_song_root(), "PC:/Karaoke"), "chosen on the folder screen (%d, %d, %s)",
              game_state(), lib_count(), plat_song_root());
        shot("o11_pc_songs");
        for (g_sel = 0; g_sel < lib_count() && strcmp(lib_get(g_sel)->artist, "Twinkle"); g_sel++) { }
        gi_go(GS_DETAILS);
        run(400, 0);
        shot("o12_pc_details");
        CHECK(game_state() == GS_DETAILS && !strcmp(game_song()->audio, "Twinkle Twinkle Little Star.mp3"), "its details, from the PC");
        snprintf(p, sizeof p, "%s/SETTINGS.TXT", data);
        st = load(p, &n);
        CHECK(st && strstr(st, "ROOT=PC:/Karaoke"), "the PC's folder is remembered");
        free(st);
        plat_set_song_root(cardk);
        settings_save();
    }
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails != 0;
}
