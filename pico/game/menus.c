// PicoSing - karaoke for the MSX PicoVerse 2350
// menus.c - main menu, song list, song details (cover, tags, best scores,
// preview), who sings, singers and the session's scores

#include "game_int.h"
#include "platform.h"
#include "library.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <strings.h>
#include "id3.h"
#include "scratch.h"
#include "usdb.h"
#include "pclink.h"
#include "music.h"
#include "settings.h"

#define KEY_BS   0x08
#define KEY_DEL  0x7F

// ---------------------------------------------------------------- state
static int main_sel;
static int list_top;
static int who_row;
static int singer_sel;

// details
static cover_t cover;
static song_tags_t tags;
static top_t top5[TOP_MAX];
static int n_top;
static int loaded_sel = -1;         // the song the details are for
static bool load_due;
static int slide_dir;               // the cover slides in from this side
static uint32_t slide_ms;
static bool preview_on;

// the details screen keeps these palette entries for itself, the cover gets the rest
#define COVER_FREE ((1u << C_GRID) | (1u << C_NOTE) | (1u << C_GOLD) | (1u << C_P1_HIT) | \
                    (1u << C_P2_HIT) | (1u << C_DARK) | (1u << C_GREEN) | (1u << C_RAP))

static const char *main_items[] = { "SING", "SINGERS", "SCORES", "SONG FOLDER", "FIND SONGS (USDB)", "SETTINGS" };
#define MAIN_N 6

static char fpath[200];
static void folders_load(void);
static int set_row, u_row;

static void stop_preview(void)
{
    if (preview_on) plat_audio_stop();
    preview_on = false;
}

void menus_enter(game_state_t s)
{
    if (s != GS_DETAILS && s != GS_WHO) stop_preview();
    if (s == GS_DETAILS && loaded_sel != g_sel) load_due = true;
    if (s == GS_WHO) who_row = 0;
    if (s == GS_SINGERS) singer_sel = 0;
    if (s == GS_SETTINGS) set_row = 0;
    if (s == GS_USDB && usdb_state() != U_BUSY && !usdb_count()) u_row = 0;
    if (s == GS_FOLDERS)
    {
        snprintf(fpath, sizeof fpath, "%s", plat_song_root());
        folders_load();
    }
}

// ---------------------------------------------------------------- details
static bool try_cover(const char *path, uint32_t off, uint32_t len)
{
    if (!plat_exists(path)) return false;
    return cover_load(&cover, path, off, len, fb_palette, COVER_FREE);
}

static void load_details(void)
{
    stop_preview();
    char err[48];
    loaded_sel = g_sel;
    cover.ok = false;
    memset(&tags, 0, sizeof tags);
    n_top = 0;
    if (!plat_song_load(g_sel, &g_song, err, sizeof err)) { gi_set_msg(err); return; }
    plat_song_tags(g_sel, &g_song, &tags);
    n_top = singers_top(g_song.artist, g_song.title, top5, TOP_MAX);

    // the cover: #COVER, the usual names, then the picture in the MP3
    char dir[160], p[224];
    if (plat_song_dir(g_sel, dir, sizeof dir))
    {
        if (g_song.cover[0])
        {
            snprintf(p, sizeof p, "%s/%s", dir, g_song.cover);
            try_cover(p, 0, 0);
        }
        if (!cover.ok)
        {
            snprintf(p, sizeof p, "%s/%s - %s [CO].jpg", dir, g_song.artist, g_song.title);
            try_cover(p, 0, 0);
        }
        if (!cover.ok)
        {
            // any "... [CO].jpg" (USDB Syncer's name, PicoSing's for USDB covers)
            // (the list is in the scratch buffer, which the cover's decoding takes: the name first)
            plat_dirent_t *e = scratch(SCR_DIR);
            int n = plat_list_dir(dir, e, 32);
            p[0] = 0;
            for (int i = 0; i < n; i++)
            {
                size_t l = strlen(e[i].name);
                if (!e[i].dir && l > 8 && !strcasecmp(e[i].name + l - 8, "[CO].jpg")) { snprintf(p, sizeof p, "%s/%s", dir, e[i].name); break; }
            }
            if (p[0]) try_cover(p, 0, 0);
        }
        static const char *names[] = { "cover.jpg", "folder.jpg", "Cover.jpg", "Folder.jpg" };
        for (int i = 0; !cover.ok && i < 4; i++)
        {
            snprintf(p, sizeof p, "%s/%s", dir, names[i]);
            try_cover(p, 0, 0);
        }
        if (!cover.ok && tags.pic_len && g_song.audio[0])
        {
            snprintf(p, sizeof p, "%s/%s", dir, g_song.audio);
            try_cover(p, tags.pic_pos, tags.pic_len);
        }
        if (!cover.ok && g_song.background[0])
        {
            // no cover: the song's background picture, when it is a baseline JPEG
            snprintf(p, sizeof p, "%s/%s", dir, g_song.background);
            try_cover(p, 0, 0);
        }
    }

    // a taste of the song: from #PREVIEWSTART, or a third in
    int32_t from = g_song.preview_ms;
    if (from <= 0) from = song_length_ms(&g_song) / 3;
    preview_on = plat_audio_preview(g_sel, &g_song, from);
}

static void details_move(int d)
{
    int n = lib_count();
    if (n < 2) return;
    g_sel = (g_sel + d + n) % n;
    slide_dir = d;
    load_due = true;
}

static void header(fb_t *f, const char *title);
static void footer(fb_t *f, const char *help);

// ---------------------------------------------------------------- delete
static bool asking;
static bool ask_whole;          // the whole folder goes, not only the .txt
static bool delete_due;

bool menus_asking(void) { return asking || delete_due; }

static int count_txt(const char *dir)
{
    plat_dirent_t *e = scratch(SCR_DIR);
    int n = plat_list_dir(dir, e, 48), t = 0;
    for (int i = 0; i < n; i++)
    {
        size_t l = strlen(e[i].name);
        if (!e[i].dir && l > 4 && !strcasecmp(e[i].name + l - 4, ".txt")) t++;
    }
    return t;
}

void menus_ask_delete(void)
{
    char dir[200];
    if (!lib_count() || !plat_song_dir(g_sel, dir, sizeof dir)) return;
    // the whole folder when this is its only song and it is not the song folder itself
    ask_whole = count_txt(dir) <= 1 && strcmp(dir, plat_song_root()) != 0;
    asking = true;
}

static bool delete_tree(const char *dir, int depth)
{
    plat_dirent_t (*e)[48] = scratch(SCR_DIR);
    int n = plat_list_dir(dir, e[depth], 48);
    if (n < 0) return false;
    for (int i = 0; i < n; i++)
    {
        char p[300];
        snprintf(p, sizeof p, "%s/%s", dir, e[depth][i].name);
        if (e[depth][i].dir)
        {
            if (depth >= 2 || !delete_tree(p, depth + 1)) return false;
        }
        else if (!plat_remove(p)) return false;
    }
    return plat_remove(dir);
}

static void do_delete(void)
{
    stop_preview();
    plat_audio_stop();
    char dir[200];
    const char *txt = lib_path(g_sel);
    bool ok = false;
    if (txt && plat_song_dir(g_sel, dir, sizeof dir))
        ok = ask_whole ? delete_tree(dir, 0) : plat_remove(txt);
    if (ok)
    {
        lib_remove(g_sel);
        if (g_sel >= lib_count()) g_sel = lib_count() - 1;
        if (g_sel < 0) g_sel = 0;
        loaded_sel = -1;
        gi_set_msg("DELETED");
    }
    else gi_set_msg("COULD NOT DELETE IT ALL");
    gi_go(GS_SONGS);
}

void menus_overlay(fb_t *f)
{
    if (!asking && !delete_due) return;
    fb_rect(f, 12, 60, FB_W - 24, 84, C_BLACK);
    fb_frame(f, 12, 60, FB_W - 24, 84, C_P2);
    fb_frame(f, 13, 61, FB_W - 26, 82, C_P2);
    if (delete_due) { gi_text_center(f, 98, "DELETING...", 11, C_YELLOW); return; }
    const lib_entry_t *e = lib_get(g_sel);
    gi_text_center(f, 68, "DELETE FROM THE CARD?", 21, C_YELLOW);
    if (e)
    {
        gi_text_center(f, 84, e->title, (int)strlen(e->title), C_WHITE);
        gi_text_center(f, 96, e->artist, (int)strlen(e->artist), C_GREY);
    }
    const char *what = ask_whole ? "THE WHOLE FOLDER GOES" : "ONLY THIS .TXT GOES";
    gi_text_center(f, 112, what, (int)strlen(what), C_CYAN);
    gi_text_center(f, 128, "Y: DELETE   OTHER KEY: KEEP", 27, C_WHITE);
}

// ---------------------------------------------------------------- tag editor
static id3_text_t edit;
static int edit_row;
static bool save_due;
static char mp3_path[300];

static void edit_open(void)
{
    char dir[200];
    if (!g_song.audio[0] || !plat_song_dir(g_sel, dir, sizeof dir)) { gi_set_msg("THIS SONG HAS NO MP3"); return; }
    snprintf(mp3_path, sizeof mp3_path, "%s/%s", dir, g_song.audio);
    stop_preview();
    if (!id3_read(mp3_path, &edit)) { gi_set_msg("CANNOT READ THE MP3"); return; }
    // empty tags: start from the song's own details
    if (!edit.title[0]) snprintf(edit.title, sizeof edit.title, "%s", g_song.title);
    if (!edit.artist[0]) snprintf(edit.artist, sizeof edit.artist, "%s", g_song.artist);
    if (!edit.year[0] && g_song.year > 0) snprintf(edit.year, sizeof edit.year, "%d", g_song.year);
    edit_row = 0;
    gi_go(GS_EDIT);
}

static char *edit_field(int r, size_t *max)
{
    switch (r)
    {
        case 0: *max = sizeof edit.title - 1; return edit.title;
        case 1: *max = sizeof edit.artist - 1; return edit.artist;
        case 2: *max = sizeof edit.album - 1; return edit.album;
        case 3: *max = 4; return edit.year;
        default: return NULL;
    }
}

static void edit_key(uint8_t k)
{
    if (k == KEY_ESC) { gi_go(GS_DETAILS); return; }
    if (k == KEY_UP) { edit_row = (edit_row + 4) % 5; return; }
    if (k == KEY_DOWN || (k == KEY_RETURN && edit_row < 4)) { edit_row = (edit_row + 1) % 5; return; }
    if (edit_row == 4)
    {
        if (k == KEY_RETURN || k == KEY_SPACE) save_due = true;
        return;
    }
    size_t max;
    char *s = edit_field(edit_row, &max);
    size_t len = strlen(s);
    if (k == KEY_BS) { if (len) s[len - 1] = 0; }
    else if (k == 0x7F) s[0] = 0;               // DEL clears the field
    else if (k >= 0x20 && k < 0x7F && len < max)
    {
        if (edit_row == 3 && (k < '0' || k > '9')) return;
        s[len] = (char)k;
        s[len + 1] = 0;
    }
}

static void draw_edit(fb_t *f)
{
    fb_clear(f, C_BLACK);
    fb_rect(f, 0, 0, FB_W, 11, C_LANE);
    fb_textz(f, 4, 2, "PICOSING", C_YELLOW);
    gi_text_center(f, 2, "MP3 TAGS", 8, C_WHITE);
    const char *fn = strrchr(mp3_path, '/');
    fn = fn ? fn + 1 : mp3_path;
    int n = (int)strlen(fn);
    fb_text(f, 4, 16, fn, n > 31 ? 31 : n, C_GREY);
    static const char *labels[4] = { "TITLE", "ARTIST", "ALBUM", "YEAR" };
    for (int r = 0; r < 4; r++)
    {
        int y = 32 + r * 32;
        bool on = r == edit_row;
        fb_textz(f, 8, y, labels[r], on ? C_YELLOW : C_GREY);
        fb_rect(f, 6, y + 10, FB_W - 12, 13, on ? C_LANE : C_BLACK);
        fb_frame(f, 6, y + 10, FB_W - 12, 13, on ? C_WHITE : C_GREY);
        size_t max;
        const char *s = edit_field(r, &max);
        int len = (int)strlen(s);
        int first = len > 29 ? len - 29 : 0;            // the end shows while typing
        fb_text(f, 10, y + 13, s + first, len - first, C_WHITE);
        if (on && ((plat_millis() / 400) & 1)) fb_rect(f, 10 + (len - first) * 8, y + 13, 7, 8, C_YELLOW);
    }
    bool on = edit_row == 4;
    fb_rect(f, 88, 162, 80, 15, on ? C_P1 : C_BLACK);
    fb_frame(f, 88, 162, 80, 15, on ? C_WHITE : C_GREY);
    gi_text_center(f, 166, save_due ? "SAVING..." : "SAVE", save_due ? 9 : 4, on ? C_WHITE : C_GREY);
    gi_draw_msg(f, 184);
    fb_rect(f, 0, 200, FB_W, 12, C_LANE);
    gi_text_center(f, 202, "TYPE  BS  DEL:CLEAR  ESC:CANCEL", 31, C_GREY);
}

// ---------------------------------------------------------------- song folder
#define FOLDERS_MAX 64
static plat_dirent_t *fent;                // (the scratch buffer: the folders screen)
static int fcount, fsel, ftop;
static bool scan_due;

static int fcmp(const void *a, const void *b)
{
    return strcasecmp(((const plat_dirent_t *)a)->name, ((const plat_dirent_t *)b)->name);
}

static bool has_parent(void) { return strchr(fpath, '/') != NULL; }

static void folders_load(void)
{
    plat_dirent_t *all = scratch(SCR_DIR);
    fent = all + FOLDERS_MAX;
    int n = plat_list_dir(fpath, all, FOLDERS_MAX);
    fcount = 0;
    for (int i = 0; i < n; i++)
        if (all[i].dir && strcasecmp(all[i].name, "PICOSING")) fent[fcount++] = all[i];
    qsort(fent, (size_t)fcount, sizeof fent[0], fcmp);
    fsel = ftop = 0;
}

// rows: 0 = sing from here, 1 = up (when there is one), then the folders
static int frows(void) { return 1 + (has_parent() ? 1 : 0) + fcount; }

static void folders_key(uint8_t k)
{
    int rows = frows();
    int up_row = has_parent() ? 1 : -1;
    int first = has_parent() ? 2 : 1;
    if (k == KEY_ESC) gi_go(GS_MAIN);
    else if (k == KEY_UP) fsel = (fsel + rows - 1) % rows;
    else if (k == KEY_DOWN) fsel = (fsel + 1) % rows;
    else if (k == KEY_BS || ((k == KEY_RETURN || k == KEY_SPACE) && fsel == up_row))
    {
        char *sl = strrchr(fpath, '/');
        if (sl) { *sl = 0; folders_load(); }
    }
    else if (k == KEY_RETURN || k == KEY_SPACE)
    {
        if (fsel == 0) scan_due = true;
        else if (fsel >= first)
        {
            size_t l = strlen(fpath);
            snprintf(fpath + l, sizeof fpath - l, "/%s", fent[fsel - first].name);
            folders_load();
        }
    }
}

static void draw_folders(fb_t *f)
{
    header(f, "SONG FOLDER");
    const char *shown = strchr(fpath, ':') ? strchr(fpath, ':') + 1 : fpath;
    if (!*shown) shown = "/";
    int n = (int)strlen(shown);
    int off = n > 31 ? n - 31 : 0;
    fb_text(f, 4, 15, shown + off, n - off, C_CYAN);
    int rows = frows();
    int first = has_parent() ? 2 : 1;
    const int vis = 12;
    if (fsel < ftop) ftop = fsel;
    if (fsel >= ftop + vis) ftop = fsel - vis + 1;
    for (int i = 0; i < vis && ftop + i < rows; i++)
    {
        int r = ftop + i, y = 30 + i * 12;
        bool on = r == fsel;
        if (on) fb_rect(f, 0, y - 2, FB_W, 12, C_P1);
        char t[80];
        uint8_t c = on ? C_WHITE : C_GREY;
        if (r == 0) { snprintf(t, sizeof t, "[ SING FROM THIS FOLDER ]"); c = on ? C_WHITE : C_GREEN; }
        else if (r < first) snprintf(t, sizeof t, ".. (UP)");
        else snprintf(t, sizeof t, "%s/", fent[r - first].name);
        int len = (int)strlen(t);
        fb_text(f, 8, y, t, len > 30 ? 30 : len, c);
    }
    const char *cur = plat_song_root();
    const char *cs = strchr(cur, ':') ? strchr(cur, ':') + 1 : cur;
    char t[64];
    n = snprintf(t, sizeof t, "NOW: %s", *cs ? cs : "/");
    fb_text(f, 4, 178, t, n > 31 ? 31 : n, C_YELLOW);
    fb_textz(f, 4, 188, "PC FOLDERS: IN A NEXT VERSION", C_DARK);
    if (scan_due) gi_text_center(f, 100, " LOOKING FOR SONGS... ", 22, C_YELLOW);
    footer(f, "RETURN: OPEN  BS: UP  ESC");
}

// ---------------------------------------------------------------- find songs (USDB)
static char u_artist[40], u_title[40];
static int u_top;
static bool u_saved[USDB_MAX];
#define U_FORM 3                      // artist, title, search; then the results

static int u_rows(void) { return U_FORM + usdb_count() + (usdb_has_more() ? 1 : 0); }

static void u_text_key(char *s, size_t max, uint8_t k)
{
    size_t len = strlen(s);
    if (k == KEY_BS) { if (len) s[len - 1] = 0; }
    else if (k == KEY_DEL) s[0] = 0;
    else if (k >= 0x20 && k < 0x7F && len < max) { s[len] = (char)k; s[len + 1] = 0; }
}

static void open_saved(void)
{
    // the song just saved: its details
    const char *p = usdb_saved();
    for (int i = 0; p[0] && i < lib_count(); i++)
        if (!strcmp(lib_path(i), p)) { g_sel = i; loaded_sel = -1; gi_go(GS_DETAILS); return; }
}

static void usdb_key(uint8_t k)
{
    bool busy = usdb_state() == U_BUSY;
    int rows = u_rows();
    if (k == KEY_ESC) { if (busy) usdb_cancel(); else gi_go(GS_MAIN); return; }
    if (busy) return;
    if (k == KEY_UP) { u_row = (u_row + rows - 1) % rows; return; }
    if (k == KEY_DOWN) { u_row = (u_row + 1) % rows; return; }
    if (u_row < 2)
    {
        if (k == KEY_RETURN) { u_row++; return; }
        u_text_key(u_row ? u_title : u_artist, 38, k);
        return;
    }
    if (k != KEY_RETURN && k != KEY_SPACE) return;
    if (u_row == 2)
    {
        if (!u_artist[0] && !u_title[0]) { gi_set_msg("TYPE AN ARTIST OR A TITLE"); return; }
        memset(u_saved, 0, sizeof u_saved);
        u_top = 0;
        usdb_search(u_artist, u_title);
        return;
    }
    int i = u_row - U_FORM;
    if (i >= usdb_count()) { usdb_more(); return; }
    if (u_saved[i]) { open_saved(); return; }
    usdb_download(i);
}

static void draw_usdb(fb_t *f)
{
    header(f, "FIND SONGS");
    static const char *lab[2] = { "ARTIST", "TITLE" };
    for (int r = 0; r < 2; r++)
    {
        int y = 16 + r * 14;
        bool on = u_row == r;
        fb_textz(f, 4, y + 2, lab[r], on ? C_YELLOW : C_GREY);
        fb_frame(f, 56, y, FB_W - 60, 12, on ? C_WHITE : C_GREY);
        const char *s = r ? u_title : u_artist;
        int len = (int)strlen(s), first = len > 23 ? len - 23 : 0;
        fb_text(f, 59, y + 2, s + first, len - first, C_WHITE);
        if (on && ((plat_millis() / 400) & 1)) fb_rect(f, 59 + (len - first) * 8, y + 2, 7, 8, C_YELLOW);
    }
    bool on = u_row == 2;
    fb_rect(f, 88, 45, 80, 13, on ? C_P1 : C_BLACK);
    fb_frame(f, 88, 45, 80, 13, on ? C_WHITE : C_GREY);
    gi_text_center(f, 48, "SEARCH", 6, on ? C_WHITE : C_GREY);

    // the results
    const int vis = 10;
    int n = usdb_count(), rows = n + (usdb_has_more() ? 1 : 0);
    int sel = u_row - U_FORM;
    if (sel >= 0)
    {
        if (sel < u_top) u_top = sel;
        if (sel >= u_top + vis) u_top = sel - vis + 1;
    }
    for (int i = 0; i < vis && u_top + i < rows; i++)
    {
        int r = u_top + i, y = 64 + i * 12;
        bool o = r == sel;
        if (o) fb_rect(f, 0, y - 2, FB_W, 12, C_P1);
        char t[80];
        if (r == n) { gi_text_center(f, y, "MORE...", 7, o ? C_WHITE : C_GREEN); continue; }
        const usdb_hit_t *h = usdb_hit(r);
        int len = snprintf(t, sizeof t, "%s - %s", h->artist, h->title);
        if (len > 27) len = 27;
        fb_text(f, 4, y, t, len, o ? C_WHITE : C_GREY);
        char lg[4] = "";
        snprintf(lg, sizeof lg, "%.2s", h->lang);
        for (char *c = lg; *c; c++) *c = (char)toupper((unsigned char)*c);
        if (u_saved[r]) fb_textz(f, FB_W - 20, y, "OK", C_GREEN);
        else fb_textz(f, FB_W - 20, y, lg, o ? C_YELLOW : C_CYAN);
    }
    const char *m = usdb_message();
    if (m[0])
    {
        uint8_t c = usdb_state() == U_ERROR ? C_P2_HIT : usdb_state() == U_BUSY ? C_YELLOW : C_GREEN;
        gi_text_center(f, 186, m, (int)strlen(m), c);
    }
    if (usdb_via_pc()) fb_textz(f, 4, 186, "PC", C_CYAN);
    gi_draw_msg(f, 176);
    if (usdb_state() == U_BUSY) footer(f, "ESC: STOP");
    else footer(f, sel >= 0 && sel < n && u_saved[sel] ? "RETURN: SEE IT  ESC: MENU" : "RETURN: SEARCH / SAVE  ESC");
}

static void usdb_update(void)
{
    static int last;
    int st = usdb_state();
    if (st == U_SAVED && last != U_SAVED)
    {
        // into the list at once
        plat_add_song(usdb_saved());
        int i = u_row - U_FORM;
        if (i >= 0 && i < USDB_MAX) u_saved[i] = true;
    }
    if (st == U_RESULTS && last == U_BUSY && usdb_count() && u_row < U_FORM) u_row = U_FORM;
    last = st;
}

// ---------------------------------------------------------------- settings
enum { SET_MUSIC, SET_PC, SET_TEST, SET_USER, SET_PASS, SET_VOLUME, SET_DELAY, SET_N };

static void set_key(uint8_t k)
{
    if (k == KEY_ESC)
    {
        // "192.168.1.20" means the MSX link's own port
        settings_fix_pc();
        settings_save();
        gi_go(GS_MAIN);
        return;
    }
    if (k == KEY_UP) { set_row = (set_row + SET_N - 1) % SET_N; return; }
    if (k == KEY_DOWN || (k == KEY_RETURN && (set_row == SET_PC || set_row == SET_USER || set_row == SET_PASS)))
    {
        set_row = (set_row + 1) % SET_N;
        return;
    }
    bool left = k == KEY_LEFT || k == '-', right = k == KEY_RIGHT || k == '+';
    switch (set_row)
    {
        case SET_MUSIC:
            if (left || right || k == KEY_RETURN || k == KEY_SPACE) g_set.music_pc = !g_set.music_pc;
            break;
        case SET_PC:   u_text_key(g_set.pc, sizeof g_set.pc - 1, k); break;
        case SET_USER: u_text_key(g_set.usdb_user, sizeof g_set.usdb_user - 1, k); break;
        case SET_PASS: u_text_key(g_set.usdb_pass, sizeof g_set.usdb_pass - 1, k); break;
        case SET_TEST:
            if (k == KEY_RETURN || k == KEY_SPACE)
            {
                settings_fix_pc();
                pc_test();
            }
            break;
        case SET_VOLUME:
            if (left || right) music_volume(g_set.volume + (right ? 5 : -5));
            break;
        case SET_DELAY:
            if (left || right) game_set_mic_delay(g_mic_delay + (right ? 10 : -10));
            break;
    }
}

static void draw_settings(fb_t *f)
{
    header(f, "SETTINGS");
    static const char *lab[SET_N] = { "MUSIC", "PC (PICOAMP)", "", "USDB NAME", "USDB PASSWORD", "VOLUME", "MIC DELAY" };
    for (int r = 0; r < SET_N; r++)
    {
        int y = 18 + r * 22;
        bool on = r == set_row;
        if (on) fb_rect(f, 0, y - 4, FB_W, 20, C_LANE);
        char v[64] = "";
        bool text = false;
        switch (r)
        {
            case SET_MUSIC:  snprintf(v, sizeof v, "< %s >", g_set.music_pc ? "PC PLAYER" : "CARD MP3"); break;
            case SET_PC:     snprintf(v, sizeof v, "%s", g_set.pc); text = true; break;
            case SET_TEST:   snprintf(v, sizeof v, "[ TEST THE PC ]"); break;
            case SET_USER:   snprintf(v, sizeof v, "%s", g_set.usdb_user); text = true; break;
            case SET_PASS:
            {
                size_t n = strlen(g_set.usdb_pass);
                for (size_t i = 0; i < n && i < 20; i++) v[i] = '*';
                v[n < 20 ? n : 20] = 0;
                text = true;
                break;
            }
            case SET_VOLUME: snprintf(v, sizeof v, "< %d >", g_set.volume); break;
            case SET_DELAY:  snprintf(v, sizeof v, "< %dMS >", g_mic_delay); break;
        }
        if (lab[r][0]) fb_textz(f, 8, y, lab[r], on ? C_YELLOW : C_GREY);
        int x = r == SET_TEST ? 8 : 120;
        int len = (int)strlen(v), first = len > 16 && x == 120 ? len - 16 : 0;
        fb_text(f, x, y, v + first, len - first, on ? C_WHITE : C_GREY);
        if (on && text && ((plat_millis() / 400) & 1)) fb_rect(f, x + (len - first) * 8, y, 7, 8, C_YELLOW);
    }
    const char *m = set_row == SET_TEST || pc_state() == PC_CONNECTING ? pc_message() : "";
    if (m[0]) gi_text_center(f, 175, m, (int)strlen(m), pc_state() == PC_FAILED ? C_P2_HIT : C_GREEN);
    fb_textz(f, 4, 188, set_row == SET_PC ? "E.G. 192.168.2.73 (PICOAMP'S K)" :
                        set_row == SET_MUSIC ? "NO MP3: THE PC PLAYS IT ANYWAY" :
                        set_row <= SET_PASS && set_row >= SET_USER ? "A FREE ACCOUNT: USDB.ANIMUX.DE" : "", C_DARK);
    footer(f, "CURSOR  TYPE  ESC: SAVE");
}

// ---------------------------------------------------------------- keys
static void list_jump(char ch)
{
    ch = (char)toupper((unsigned char)ch);
    int n = lib_count();
    for (int k = 1; k <= n; k++)
    {
        int i = (g_sel + k) % n;
        if (toupper((unsigned char)lib_get(i)->artist[0]) == ch) { g_sel = i; return; }
    }
}

static void edit_name(singer_t *s, uint8_t k)
{
    int len = (int)strlen(s->name);
    if (k == KEY_BS) { if (len) s->name[len - 1] = 0; return; }
    if (k >= 0x20 && k < 0x7F && len < NAME_MAX)
    {
        s->name[len] = (char)toupper(k);
        s->name[len + 1] = 0;
    }
}

void menus_key(uint8_t k)
{
    int n = lib_count();
    if (delete_due) return;
    if (asking)
    {
        asking = false;
        if (k == 'Y' || k == 'y') delete_due = true;
        return;
    }
    switch (g_state)
    {
        case GS_TITLE:
            if (k == KEY_SPACE || k == KEY_RETURN) gi_go(GS_MAIN);
            break;

        case GS_MAIN:
            if (k == KEY_UP) main_sel = (main_sel + MAIN_N - 1) % MAIN_N;
            else if (k == KEY_DOWN) main_sel = (main_sel + 1) % MAIN_N;
            else if (k == KEY_ESC) gi_go(GS_TITLE);
            else if (k == KEY_RETURN || k == KEY_SPACE)
            {
                static const game_state_t to[] = { GS_SONGS, GS_SINGERS, GS_SCORES, GS_FOLDERS, GS_USDB, GS_SETTINGS };
                gi_go(to[main_sel]);
            }
            break;

        case GS_SONGS:
            if (k == KEY_ESC) { gi_go(GS_MAIN); break; }
            if (!n) break;
            if (k == KEY_UP && g_sel > 0) g_sel--;
            else if (k == KEY_DOWN && g_sel < n - 1) g_sel++;
            else if (k == KEY_LEFT) g_sel = g_sel > 13 ? g_sel - 13 : 0;
            else if (k == KEY_RIGHT) g_sel = g_sel + 13 < n ? g_sel + 13 : n - 1;
            else if (k == KEY_RETURN || k == KEY_SPACE) gi_go(GS_DETAILS);
            else if (k == KEY_DEL) menus_ask_delete();
            else if (isalnum(k)) list_jump((char)k);
            break;

        case GS_DETAILS:
            if (k == KEY_ESC) gi_go(GS_SONGS);
            else if (k == KEY_LEFT || k == KEY_UP) details_move(-1);
            else if (k == KEY_RIGHT || k == KEY_DOWN) details_move(1);
            else if ((k == KEY_RETURN || k == KEY_SPACE) && !load_due) gi_go(GS_WHO);
            else if ((k == 'E' || k == 'e') && !load_due) edit_open();
            else if (k == KEY_DEL && !load_due) menus_ask_delete();
            break;

        case GS_EDIT:
            if (!save_due) edit_key(k);
            break;

        case GS_FOLDERS:
            if (!scan_due) folders_key(k);
            break;

        case GS_USDB:
            usdb_key(k);
            break;

        case GS_SETTINGS:
            set_key(k);
            break;

        case GS_WHO:
        {
            int c = singers_count();
            if (k == KEY_ESC) gi_go(GS_DETAILS);
            else if (k == KEY_UP || k == KEY_DOWN) who_row ^= 1;
            else if (k == KEY_LEFT || k == KEY_RIGHT)
            {
                // -1 (nobody), 0 .. c-1
                int v = g_who[who_row] + 1 + (k == KEY_RIGHT ? 1 : -1);
                v = (v + c + 1) % (c + 1);
                g_who[who_row] = v - 1;
            }
            else if (k == KEY_RETURN || k == KEY_SPACE)
            {
                preview_on = false;     // the song takes over the audio
                if (!gi_start_song()) gi_go(GS_SONGS);
            }
            break;
        }

        case GS_SINGERS:
        {
            int c = singers_count();
            int rows = c + (c < SINGERS_MAX);
            if (k == KEY_ESC) { singers_save(); gi_go(GS_MAIN); }
            else if (k == KEY_UP) singer_sel = (singer_sel + rows - 1) % rows;
            else if (k == KEY_DOWN) singer_sel = (singer_sel + 1) % rows;
            else if (singer_sel == c)
            {
                if (k == KEY_RETURN || k == KEY_SPACE) singer_add();
            }
            else if (k == KEY_LEFT || k == KEY_RIGHT)
            {
                singer_t *s = singer_get(singer_sel);
                s->avatar = (uint8_t)((s->avatar + (k == KEY_RIGHT ? 1 : AVATARS - 1)) % AVATARS);
            }
            else if (k == KEY_DEL && c > 1)
            {
                singer_remove(singer_sel);
                for (int p = 0; p < 2; p++)
                {
                    if (g_who[p] == singer_sel) g_who[p] = -1;
                    else if (g_who[p] > singer_sel) g_who[p]--;
                }
                if (singer_sel >= singers_count()) singer_sel = singers_count() - 1;
            }
            else if (k == KEY_RETURN) singer_sel = (singer_sel + 1) % rows;
            else edit_name(singer_get(singer_sel), k);
            break;
        }

        case GS_SCORES:
            if (k == KEY_ESC || k == KEY_RETURN || k == KEY_SPACE) gi_go(GS_MAIN);
            break;

        default:
            break;
    }
}

void menus_update(void)
{
    if (g_state == GS_USDB) usdb_update();
    // the slow jobs wait a frame so the screen says what is going on
    static uint32_t due_since;
    bool due = delete_due || save_due || scan_due;
    if (!due) due_since = 0;
    else if (!due_since) due_since = plat_millis() | 1;
    else if (plat_millis() - due_since > 80)
    {
        due_since = 0;
        if (delete_due) { delete_due = false; do_delete(); }
        else if (save_due)
        {
            const char *err = id3_write(mp3_path, &edit);
            save_due = false;
            if (err) gi_set_msg(err);
            else { gi_set_msg("TAGS SAVED"); loaded_sel = -1; gi_go(GS_DETAILS); }
        }
        else if (scan_due)
        {
            bool ok = plat_set_song_root(fpath);
            settings_save();
            scan_due = false;
            char m[40];
            if (!ok) snprintf(m, sizeof m, "CANNOT READ THAT FOLDER");
            else if (lib_count() == 1) snprintf(m, sizeof m, "1 SONG");
            else snprintf(m, sizeof m, "%d SONGS", lib_count());
            gi_set_msg(m);
            g_sel = 0;
            loaded_sel = -1;
            if (ok) gi_go(GS_SONGS);
        }
    }
    // the details screen shows "LOADING" for a frame before the card work
    if (g_state == GS_DETAILS && load_due && plat_millis() - g_state_ms > 60)
    {
        load_due = false;
        load_details();
        slide_ms = plat_millis();
    }
}

// ---------------------------------------------------------------- drawing
static void header(fb_t *f, const char *title)
{
    fb_clear(f, C_BLACK);
    fb_rect(f, 0, 0, FB_W, 11, C_LANE);
    fb_textz(f, 4, 2, "PICOSING", C_YELLOW);
    gi_text_center(f, 2, title, (int)strlen(title), C_WHITE);
}

static void footer(fb_t *f, const char *help)
{
    fb_rect(f, 0, 200, FB_W, 12, C_LANE);
    gi_text_center(f, 202, help, (int)strlen(help), C_GREY);
}

static void draw_main(fb_t *f)
{
    header(f, "");
    // a small logo
    static const char *big = "PicoSing";
    for (int i = 0; i < 8; i++)
    {
        int x = 64 + i * 16;
        uint8_t c = (uint8_t)(i < 4 ? C_YELLOW : C_CYAN);
        // double-size letters made of 2x2 blocks
        extern char font8x8_basic[128][8];
        for (int y = 0; y < 8; y++)
            for (int b = 0; b < 8; b++)
                if (font8x8_basic[(int)big[i]][y] >> b & 1) fb_rect(f, x + b * 2, 22 + y * 2, 2, 2, c);
    }
    for (int i = 0; i < MAIN_N; i++)
    {
        int y = 50 + i * 16;
        bool on = (i == main_sel);
        if (on) fb_rect(f, 48, y - 4, 160, 15, C_P1);
        gi_text_center(f, y, main_items[i], (int)strlen(main_items[i]), on ? C_WHITE : C_GREY);
    }
    // who is at the mics
    for (int p = 0; p < 2; p++)
    {
        const singer_t *s = singer_get(g_who[p]);
        int x = p ? FB_W - 52 : 12;
        fb_frame(f, x - 2, 148, 44, 48, p ? C_P2 : C_P1);
        if (s)
        {
            avatar_draw(f, s->avatar, x + 4, 151, 2);
            int nl = (int)strlen(s->name);
            if (nl > 5) nl = 5;
            fb_text(f, x + 20 - nl * 4, 185, s->name, nl, p ? C_P2_HIT : C_P1_HIT);
        }
        else fb_textz(f, x + 4, 168, "----", C_GREY);
    }
    gi_text_center(f, 168, "HAPPY MSX-SING!", 15, C_CYAN);
    gi_draw_msg(f, 186);
    footer(f, "CURSOR + RETURN, ESC: TITLE");
}

static void draw_songs(fb_t *f)
{
    header(f, "SONGS");
    char t[40];
    int n;
    int count = lib_count();
    const char *problem = plat_card_problem();
    if (count == 0)
    {
        const char *m = problem ? problem : "NO SONGS FOUND";
        gi_text_center(f, 60, m, (int)strlen(m), C_WHITE);
        fb_textz(f, 16, 84, "PUT ULTRASTAR SONGS ON THE", C_GREY);
        fb_textz(f, 16, 94, "SD CARD, ONE FOLDER EACH:", C_GREY);
        fb_textz(f, 16, 110, "/KARAOKE/<SONG>/SONG.TXT", C_CYAN);
        fb_textz(f, 16, 120, "/KARAOKE/<SONG>/SONG.MP3", C_CYAN);
    }
    else
    {
        const int rows = 13;
        if (g_sel < list_top) list_top = g_sel;
        if (g_sel >= list_top + rows) list_top = g_sel - rows + 1;
        for (int i = 0; i < rows && list_top + i < count; i++)
        {
            const lib_entry_t *e = lib_get(list_top + i);
            int y = 16 + i * 12;
            bool on = (list_top + i == g_sel);
            if (on) fb_rect(f, 0, y - 2, FB_W, 12, C_P1);
            char line[80];
            int len = snprintf(line, sizeof line, "%s - %s", e->artist, e->title);
            if (len > 30) len = 30;
            fb_text(f, 4, y, line, len, on ? C_WHITE : C_GREY);
            if (e->duet) fb_textz(f, FB_W - 20, y, "DU", on ? C_YELLOW : C_RAP);
        }
        n = snprintf(t, sizeof t, "%d/%d", g_sel + 1, count);
        fb_text(f, FB_W - 4 - n * 8, 176, t, n, C_GREY);
        fb_textz(f, 4, 176, "A-Z: JUMP", C_GREEN);
    }
    gi_draw_msg(f, 186);
    footer(f, "RETURN: DETAILS  ESC: MENU");
}

// a stand-in when the song has no cover: a record
static void no_cover(fb_t *f, int x, int y)
{
    fb_rect(f, x, y, COVER_PX, COVER_PX, C_LANE);
    for (int r = 44; r > 0; r -= 2)
    {
        uint8_t c = r > 40 ? C_BLACK : r > 16 ? (r & 2 ? C_BLACK : C_LANE) : r > 6 ? C_P2 : C_BLACK;
        for (int dy = -r; dy <= r; dy++)
        {
            int w = 0;
            while ((w + 1) * (w + 1) + dy * dy <= r * r) w++;
            fb_rect(f, x + 48 - w, y + 48 + dy, 2 * w + 1, 1, c);
        }
    }
}

static int line_fit(fb_t *f, int x, int y, const char *label, const char *val, uint8_t c)
{
    if (!val || !val[0]) return y;
    fb_textz(f, x, y, label, C_GREY);
    int lx = x + (int)strlen(label) * 8;
    int max = (FB_W - lx - 2) / 8;
    int n = (int)strlen(val);
    if (n > max) n = max;
    fb_text(f, lx, y, val, n, c);
    return y + 10;
}

static void draw_details(fb_t *f)
{
    header(f, "SONG");
    if (load_due || loaded_sel != g_sel)
    {
        gi_text_center(f, 90, "LOADING...", 10, C_YELLOW);
        return;
    }
    // the cover slides in (an iTunes-style flick through the songs)
    int cx = 8, cy = 18;
    uint32_t dt = plat_millis() - slide_ms;
    if (slide_dir && dt < 240)
    {
        int t = (int)(240 - dt);
        cx += slide_dir * t * t * 120 / (240 * 240);
    }
    else slide_dir = 0;
    if (cover.ok) cover_draw(f, &cover, cx, cy);
    else no_cover(f, cx, cy);
    fb_frame(f, cx - 1, cy - 1, COVER_PX + 2, COVER_PX + 2, C_GREY);

    // title and artist, wrapped next to the cover
    int x = 112, y = 20;
    const char *ttl = g_song.title;
    int n = (int)strlen(ttl);
    for (int row = 0; row < 3 && n > 0; row++)
    {
        int w = n > 17 ? 17 : n;
        if (n > 17) { int c = w; while (c > 6 && ttl[c] != ' ') c--; if (ttl[c] == ' ') w = c; }
        fb_text(f, x, y, ttl, w, C_WHITE);
        ttl += w; n -= w;
        while (*ttl == ' ') { ttl++; n--; }
        y += 10;
    }
    y += 2;
    n = (int)strlen(g_song.artist);
    fb_text(f, x, y, g_song.artist, n > 17 ? 17 : n, C_YELLOW);
    y += 14;
    char t[48];
    if (tags.album[0]) { n = (int)strlen(tags.album); fb_text(f, x, y, tags.album, n > 17 ? 17 : n, C_CYAN); y += 10; }
    if (g_song.year > 0 || g_song.genre[0])
    {
        char yr[8] = "";
        if (g_song.year > 0) snprintf(yr, sizeof yr, "%d", g_song.year);
        n = snprintf(t, sizeof t, "%s%s%s", yr, yr[0] && g_song.genre[0] ? " " : "", g_song.genre);
        fb_text(f, x, y, t, n > 17 ? 17 : n, C_GREY); y += 10;
    }
    int32_t len = song_length_ms(&g_song);
    if (tags.seconds) len = (int32_t)tags.seconds * 1000;
    n = snprintf(t, sizeof t, "%d:%02d %s", (int)(len / 60000), (int)(len / 1000 % 60), g_song.duet ? "DUET" : "");
    fb_text(f, x, y, t, n, C_GREY); y += 10;
    if (tags.kbps) { n = snprintf(t, sizeof t, "%dKBPS %dKHZ", (int)tags.kbps, (int)(tags.rate / 1000)); fb_text(f, x, y, t, n, C_GREY); y += 10; }
    else if (g_song.youtube[0]) { fb_textz(f, x, y, "NO MP3: YOUTUBE", C_CYAN); y += 10; }
    else if (!tags.ok) { fb_textz(f, x, y, "NO MP3", C_CYAN); y += 10; }

    // more facts, below the cover
    y = 120;
    y = line_fit(f, 8, y, "LANGUAGE ", g_song.language, C_WHITE);
    n = snprintf(t, sizeof t, "%d LINES, %d NOTES", g_song.n_lines, g_song.n_notes);
    fb_text(f, 8, y, t, n, C_GREY);
    y += 14;

    // best scores for this song
    fb_textz(f, 8, y, "BEST SCORES", C_YELLOW);
    y += 10;
    if (n_top == 0) fb_textz(f, 8, y, "NOBODY YET - BE THE FIRST!", C_GREY);
    for (int i = 0; i < n_top && i < 3; i++)
    {
        n = snprintf(t, sizeof t, "%d. %-10s %5d", i + 1, top5[i].name, top5[i].score);
        fb_text(f, 8, y + i * 10, t, n, i == 0 ? C_WHITE : C_GREY);
    }
    gi_draw_msg(f, 186);
    footer(f, "<> SONGS RET:SING E:TAGS DEL");
}

static void draw_who(fb_t *f)
{
    header(f, "WHO SINGS?");
    gi_text_center(f, 16, g_song.title, (int)strlen(g_song.title), C_WHITE);
    for (int p = 0; p < 2; p++)
    {
        int y = 34 + p * 78;
        bool on = (p == who_row);
        uint8_t c = p ? C_P2 : C_P1;
        fb_frame(f, 16, y, FB_W - 32, 70, on ? C_WHITE : c);
        if (on) fb_frame(f, 17, y + 1, FB_W - 34, 68, C_WHITE);
        fb_textz(f, 28, y + 8, p ? "RED MIC" : "BLUE MIC", p ? C_P2_HIT : C_P1_HIT);
        const singer_t *s = singer_get(g_who[p]);
        if (s)
        {
            avatar_draw(f, s->avatar, 152, y + 11, 3);
            fb_textz(f, 32, y + 30, s->name, C_WHITE);
            char t[32];
            int n = snprintf(t, sizeof t, "BEST %d", s->best);
            if (s->songs) fb_text(f, 32, y + 44, t, n, C_GREY);
        }
        else fb_textz(f, 32, y + 30, "NOBODY", C_GREY);
        if (on) { fb_textz(f, 21, y + 30, "<", C_YELLOW); fb_textz(f, FB_W - 28, y + 30, ">", C_YELLOW); }
    }
    footer(f, "<> SINGER  RETURN: GO!  ESC");
}

static void draw_singers(fb_t *f)
{
    header(f, "SINGERS");
    int c = singers_count();
    int rows = c + (c < SINGERS_MAX);
    for (int i = 0; i < rows; i++)
    {
        int y = 16 + i * 22;
        bool on = (i == singer_sel);
        if (on) fb_rect(f, 0, y - 2, FB_W, 21, C_LANE);
        if (i == c) { fb_textz(f, 32, y + 4, "+ ADD A SINGER", on ? C_GREEN : C_GREY); continue; }
        const singer_t *s = singer_get(i);
        avatar_draw(f, s->avatar, 8, y, 1);
        int x = 32 + fb_textz(f, 32, y + 4, s->name, on ? C_WHITE : C_GREY);
        if (on && ((plat_millis() / 400) & 1)) fb_rect(f, x, y + 4, 7, 8, C_YELLOW);   // cursor
        for (int p = 0; p < 2; p++)
            if (g_who[p] == i) fb_rect(f, 128 + p * 6, y + 5, 4, 6, p ? C_P2 : C_P1);
    }
    footer(f, "TYPE NAME <> FACE DEL ESC:OK");
}

static void draw_scores(fb_t *f)
{
    header(f, "TONIGHT");
    int c = singers_count();
    int order[SINGERS_MAX];
    for (int i = 0; i < c; i++) order[i] = i;
    for (int i = 1; i < c; i++)      // by total, high first
        for (int j = i; j > 0 && singer_get(order[j])->total > singer_get(order[j - 1])->total; j--)
        {
            int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
    fb_textz(f, 40, 16, "SINGER      #  BEST  TOTAL", C_GREY);
    bool any = false;
    for (int r = 0; r < c; r++)
    {
        const singer_t *s = singer_get(order[r]);
        int y = 30 + r * 20;
        avatar_draw(f, s->avatar, 16, y - 4, 1);
        char t[48];
        int n = snprintf(t, sizeof t, "%-10s %2d %5d %6d", s->name, s->songs, s->best, s->total);
        fb_text(f, 40, y, t, n, r == 0 && s->total ? C_YELLOW : C_WHITE);
        if (s->songs) any = true;
    }
    if (!any) gi_text_center(f, 190, "NO SONGS SUNG YET TONIGHT", 25, C_GREY);
    footer(f, "RETURN: MENU");
}

void menus_draw(fb_t *f)
{
    switch (g_state)
    {
        case GS_MAIN:    draw_main(f); break;
        case GS_SONGS:   draw_songs(f); break;
        case GS_DETAILS: draw_details(f); break;
        case GS_WHO:     draw_who(f); break;
        case GS_SINGERS: draw_singers(f); break;
        case GS_SCORES:  draw_scores(f); break;
        case GS_EDIT:    draw_edit(f); break;
        case GS_FOLDERS: draw_folders(f); break;
        case GS_USDB:    draw_usdb(f); break;
        case GS_SETTINGS: draw_settings(f); break;
        default:         fb_clear(f, C_BLACK); break;
    }
}

void menus_palette(uint8_t pal[32])
{
    if ((g_state == GS_DETAILS || g_state == GS_WHO) && cover.ok && loaded_sel == g_sel && !load_due)
    {
        // the cover's colours, but only on the details screen
        if (g_state == GS_DETAILS) memcpy(pal, cover.pal, 32);
    }
}
