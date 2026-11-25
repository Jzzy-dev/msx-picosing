// PicoSing - karaoke for the MSX PicoVerse 2350
// game.c - singing, scoring and results (the other screens: menus.c, title.c)
//
// Scoring (after UltraStar): 9000 points for the notes, shared out by
// length (golden notes count twice), and 1000 for the lines. A beat counts
// as sung when the mic's pitch is within a semitone of the note, in any
// octave. Rap notes only need a voice; freestyle notes give no points.

#include "game_int.h"
#include "platform.h"
#include "library.h"
#include "music.h"
#include "pclink.h"
#include "usdb.h"
#include "settings.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define LANE_X0     8
#define LANE_W      240
#define LANE_Y      26
#define LANE_H      112
#define TOLERANCE   1.0f      // semitones
#define LINE_FULL   0.9f      // this share of a line earns its whole bonus
#define SANG_MIN    40        // voiced analyses (~1 s) before a mic counts as singing
#define DELAY_STEPS 21        // mic delays tried: 0, 20, ... 400 ms
#define DELAY_STEP  20

song_t g_song;
game_state_t g_state;
int g_sel;
int g_who[2] = { 0, 1 };
int g_mic_delay = 60;
uint32_t g_state_ms;
static char msg[48];
static uint32_t msg_until;
static int cur_song = -1;
static int32_t song_len_ms;

// a comment after each line, per mic
static const char *comment[2];
static uint32_t comment_until[2];

static pitch_result_t last[2];
static uint32_t last_ms[2];

typedef struct {
    float    hit_w, total_w;
    float    line_bonus, line_share;
    int      line;              // line being scored, -1 = none
    float    line_hit, line_w;
    uint32_t prev_ms;
    int      voiced;            // analyses with a voice during the song
    int      trail_line;
    int      last_x;
    uint8_t  ty[LANE_W];        // trail: screen y, 0 = nothing
    uint8_t  th[LANE_W];        // 1 = on the note
    uint16_t dhit[DELAY_STEPS]; // on-note analyses for each mic delay tried
    int      line_voiced;       // analyses with a voice in the current line
} player_t;

static player_t pl[2];
static uint16_t credit[2][SONG_MAX_NOTES];   // sung part of each note, 1/16 beat
static float    line_got[2][SONG_MAX_LINES]; // line bonus given for each line
static bool     paused;
static bool     started;              // the music has begun (the PC's player can keep us waiting)
static uint32_t vol_shown_until;
static uint32_t jump_msg_until;
static char     jump_msg[24];

song_t *game_song(void) { return &g_song; }
game_state_t game_state(void) { return g_state; }
int game_mic_delay(void) { return g_mic_delay; }
void game_set_mic_delay(int ms) { g_mic_delay = ms < 0 ? 0 : ms > 400 ? 400 : ms; g_set.mic_delay = g_mic_delay; }

void gi_set_msg(const char *m)
{
    snprintf(msg, sizeof msg, "%s", m);
    msg_until = plat_millis() + 4000;
}
#define set_msg gi_set_msg

void gi_draw_msg(fb_t *f, int y)
{
    if (msg[0] && plat_millis() < msg_until)
    {
        fb_rect(f, 0, y, FB_W, 11, C_P2);
        gi_text_center(f, y + 2, msg, (int)strlen(msg), C_WHITE);
    }
}

void gi_go(game_state_t st)
{
    g_state = st;
    g_state_ms = plat_millis();
    menus_enter(st);
}

static int track_of(int p) { return g_song.duet ? p : 0; }
static bool scored(const note_t *n) { return n->type != NOTE_FREESTYLE; }
static float weight(const note_t *n) { return (n->type == NOTE_GOLDEN || n->type == NOTE_RAP_GOLDEN) ? 2.0f : 1.0f; }

// ---------------------------------------------------------------- layout
typedef struct {
    int   y0, h, yc;
    float b0, b1;       // beats at the left and right edge
    float pmid, step;   // pitch in the middle, pixels per semitone
    int   bar;          // note bar height
} layout_t;

static void lane_of(int track, int *y0, int *h)
{
    if (g_song.duet) { *y0 = LANE_Y + track * 58; *h = 54; }
    else             { *y0 = LANE_Y; *h = LANE_H; }
}

static void layout(int line, int track, layout_t *L)
{
    lane_of(track, &L->y0, &L->h);
    L->yc = L->y0 + L->h / 2;
    const line_t *ln = &g_song.lines[line];
    L->b0 = (float)ln->start;
    L->b1 = (float)ln->end;
    if (L->b1 - L->b0 < 1.0f) L->b1 = L->b0 + 1.0f;
    int pmin = 999, pmax = -999;
    for (int i = 0; i < ln->count; i++)
    {
        const note_t *n = &g_song.notes[ln->first + i];
        if (n->type == NOTE_FREESTYLE && ln->count > 1) continue;
        if (n->pitch < pmin) pmin = n->pitch;
        if (n->pitch > pmax) pmax = n->pitch;
    }
    if (pmin > pmax) pmin = pmax = 0;
    int range = pmax - pmin;
    if (range < 6) range = 6;              // keep small steps from looking huge
    L->pmid = (pmin + pmax) * 0.5f;
    L->step = (float)(L->h - 12) / (float)range;
    if (L->step > 6.0f) L->step = 6.0f;
    L->bar = L->step >= 5.0f ? 5 : L->step >= 3.0f ? (int)L->step : 3;
}

static int beat_x(const layout_t *L, float beat)
{
    return LANE_X0 + (int)((beat - L->b0) * (float)LANE_W / (L->b1 - L->b0));
}

static int pitch_y(const layout_t *L, float pitch)
{
    int y = L->yc - (int)lrintf((pitch - L->pmid) * L->step);
    if (y < L->y0 + 2) y = L->y0 + 2;
    if (y > L->y0 + L->h - 3) y = L->y0 + L->h - 3;
    return y;
}

// The note of a track sung at a beat (NULL in a pause)
static const note_t *note_at(int track, float beat, int *line_out)
{
    const track_t *t = &g_song.tracks[track];
    for (int li = 0; li < t->n_lines; li++)
    {
        const line_t *ln = &g_song.lines[t->first_line + li];
        if (beat < (float)ln->start) break;
        if (beat >= (float)ln->end) continue;
        if (line_out) *line_out = t->first_line + li;
        for (int i = 0; i < ln->count; i++)
        {
            const note_t *n = &g_song.notes[ln->first + i];
            if (beat >= (float)n->start && beat < (float)(n->start + n->len)) return n;
        }
        return NULL;
    }
    if (line_out) *line_out = -1;
    return NULL;
}

// ---------------------------------------------------------------- scoring
static float line_weight(int line)
{
    const line_t *ln = &g_song.lines[line];
    float w = 0;
    for (int i = 0; i < ln->count; i++)
    {
        const note_t *n = &g_song.notes[ln->first + i];
        if (scored(n)) w += (float)n->len * weight(n);
    }
    return w;
}

static void close_line(player_t *P)
{
    if (P->line >= 0 && P->line_w > 0)
    {
        float r = P->line_hit / (P->line_w * LINE_FULL);
        if (r > 1.0f) r = 1.0f;
        P->line_bonus += P->line_share * r;
        line_got[P == &pl[1]][P->line] += P->line_share * r;

        // a comment, when this mic sang in the line
        int p = (P == &pl[1]);
        if (P->line_voiced > 3)
        {
            static const char *great[] = { "PERFECT!", "SUPERSTAR!", "WOW!" };
            static const char *good[]  = { "GREAT!", "COOL!", "NICE ONE" };
            static const char *ok[]    = { "GOOD", "NOT BAD", "OK!" };
            static const char *poor[]  = { "KEEP GOING", "HMM...", "TRY HARDER" };
            int v = (int)(P->line_hit * 7.0f) % 3;
            comment[p] = r >= 0.95f ? great[v] : r >= 0.75f ? good[v] : r >= 0.45f ? ok[v] : poor[v];
            comment_until[p] = plat_millis() + 1500;
        }
    }
    P->line = -1;
    P->line_voiced = 0;
}

int game_score(int p)
{
    const player_t *P = &pl[p & 1];
    if (P->total_w <= 0) return 0;
    int s = (int)(9000.0f * P->hit_w / P->total_w + P->line_bonus + 0.5f);
    return s > 10000 ? 10000 : s;
}

static bool sang(int p) { return pl[p & 1].voiced >= SANG_MIN; }
static int best_delay(void);

// The mic delay that matched best over the song (-1: not enough singing)
int game_best_delay(void) { return best_delay(); }

static int best_delay(void)
{
    uint32_t sum[DELAY_STEPS] = { 0 };
    for (int p = 0; p < 2; p++)
        if (sang(p))
            for (int k = 0; k < DELAY_STEPS; k++) sum[k] += pl[p].dhit[k];
    int best = 0;
    for (int k = 1; k < DELAY_STEPS; k++) if (sum[k] > sum[best]) best = k;
    if (sum[best] < 60) return -1;
    // a flat top: take the middle of the delays within 2% of the best
    int lo = best, hi = best;
    while (lo > 0 && sum[lo - 1] * 50 >= sum[best] * 49) lo--;
    while (hi < DELAY_STEPS - 1 && sum[hi + 1] * 50 >= sum[best] * 49) hi++;
    return (lo + hi) / 2 * DELAY_STEP;
}

static void start_scoring(void)
{
    memset(pl, 0, sizeof pl);
    memset(credit, 0, sizeof credit);
    memset(line_got, 0, sizeof line_got);
    paused = false;
    for (int p = 0; p < 2; p++)
    {
        player_t *P = &pl[p];
        P->line = -1;
        P->trail_line = -1;
        P->last_x = -1;
        const track_t *t = &g_song.tracks[track_of(p)];
        int lines = 0;
        for (int li = 0; li < t->n_lines; li++)
        {
            float w = line_weight(t->first_line + li);
            P->total_w += w;
            if (w > 0) lines++;
        }
        P->line_share = lines ? 1000.0f / (float)lines : 0.0f;
    }
}

void game_mic(int p, const pitch_result_t *r, uint32_t now_ms)
{
    p &= 1;
    last[p] = *r;
    last_ms[p] = now_ms;
    if (g_state != GS_PLAY || paused) return;

    player_t *P = &pl[p];
    int32_t pos = music_ms();
    if (pos < 0) return;

    float dt_ms = P->prev_ms ? (float)(now_ms - P->prev_ms) : 0.0f;
    if (dt_ms > 100.0f) dt_ms = 100.0f;
    P->prev_ms = now_ms;
    if (r->voiced) P->voiced++;
    if (r->voiced && P->line >= 0) P->line_voiced++;

    int tr = track_of(p);
    float beat = song_beat_at(&g_song, pos - g_mic_delay);
    float dt_beats = dt_ms / song_beat_ms(&g_song);

    int line = -1;
    const note_t *n = note_at(tr, beat, &line);

    // line bonus bookkeeping
    if (P->line >= 0 && (line != P->line)) close_line(P);
    if (line >= 0 && P->line != line)
    {
        P->line = line;
        P->line_hit = 0;
        P->line_w = line_weight(line);
    }

    bool hit = false;
    float sung = (float)(r->note - 60) + (float)r->cents / 100.0f;   // UltraStar pitch
    if (n && scored(n))
    {
        if (n->type == NOTE_RAP || n->type == NOTE_RAP_GOLDEN)
            hit = r->voiced || r->dbfs > -40.0f;
        else if (r->voiced)
        {
            float d = sung - (float)n->pitch;
            d -= 12.0f * roundf(d / 12.0f);
            hit = fabsf(d) <= TOLERANCE;
        }
        if (hit)
        {
            int idx = (int)(n - g_song.notes);
            float left = (float)n->len - (float)credit[p][idx] / 16.0f;
            float c = dt_beats < left ? dt_beats : left;
            if (c > 0)
            {
                credit[p][idx] = (uint16_t)(credit[p][idx] + (uint16_t)lrintf(c * 16.0f));
                P->hit_w += c * weight(n);
                if (P->line == line) P->line_hit += c * weight(n);
            }
        }
    }

    // Which mic delay would have put this analysis on its note? Counted for
    // all delays tried; the best one is offered on the results screen.
    if (r->voiced)
    {
        for (int k = 0; k < DELAY_STEPS; k++)
        {
            const note_t *nk = note_at(tr, song_beat_at(&g_song, pos - k * DELAY_STEP), NULL);
            if (!nk || nk->type == NOTE_FREESTYLE || nk->type == NOTE_RAP || nk->type == NOTE_RAP_GOLDEN) continue;
            float d = sung - (float)nk->pitch;
            d -= 12.0f * roundf(d / 12.0f);
            if (fabsf(d) <= TOLERANCE && P->dhit[k] < 65535) P->dhit[k]++;
        }
    }

    // trail on the line that is on screen
    float beat_now = song_beat_at(&g_song, pos);
    int shown = song_line_at(&g_song, tr, beat_now);
    if (shown != P->trail_line)
    {
        P->trail_line = shown;
        memset(P->ty, 0, sizeof P->ty);
        P->last_x = -1;
    }
    if (shown < 0 || !r->voiced) { P->last_x = -1; return; }
    layout_t L;
    layout(shown, tr, &L);
    if (beat < L.b0 || beat >= L.b1) { P->last_x = -1; return; }

    // fold the sung pitch into the octave of the note (or of the line)
    float ref = n ? (float)n->pitch : L.pmid;
    float s = sung - 12.0f * roundf((sung - ref) / 12.0f);
    int x = beat_x(&L, beat) - LANE_X0;
    if (x < 0) x = 0;
    if (x >= LANE_W) x = LANE_W - 1;
    int y = pitch_y(&L, s);
    int from = (P->last_x >= 0 && x > P->last_x && x - P->last_x <= 12) ? P->last_x + 1 : x;
    for (int i = from; i <= x; i++)
    {
        P->ty[i] = (uint8_t)y;
        P->th[i] = hit ? 1 : 0;
    }
    P->last_x = x;
}

// ---------------------------------------------------------------- flow
// ---------------------------------------------------------------- jumping
// Take back what was scored from beat b on: practice a part again, and the
// points for it count once.
static void undo_from(float b)
{
    for (int p = 0; p < 2; p++)
    {
        player_t *P = &pl[p];
        for (int i = 0; i < g_song.n_notes; i++)
        {
            const note_t *n = &g_song.notes[i];
            if ((float)n->start >= b && credit[p][i])
            {
                P->hit_w -= (float)credit[p][i] / 16.0f * weight(n);
                credit[p][i] = 0;
            }
        }
        for (int l = 0; l < g_song.n_lines; l++)
            if ((float)g_song.lines[l].start >= b && line_got[p][l] > 0)
            {
                P->line_bonus -= line_got[p][l];
                line_got[p][l] = 0;
            }
        if (P->hit_w < 0) P->hit_w = 0;
        if (P->line_bonus < 0) P->line_bonus = 0;
        P->line = -1;                // the line being sung is dropped, not scored
        P->line_voiced = 0;
        P->trail_line = -1;
        P->prev_ms = 0;
        comment[p] = NULL;
    }
}

// dir +1: the next line (skips an intro or a solo), -1: the line again, or
// the one before when the current one has just begun
static void jump(int dir)
{
    int32_t pos = music_ms();
    if (pos < 0) return;
    float now_b = song_beat_at(&g_song, pos);
    float target = -1;
    float best_gap = 1e9f;
    for (int l = 0; l < g_song.n_lines; l++)
    {
        float st = (float)g_song.lines[l].start;
        if (dir > 0)
        {
            // the next line whose 2 s run-up is still ahead
            if (song_ms_at(&g_song, st) - 2000 > pos + 500 && st - now_b < best_gap) { best_gap = st - now_b; target = st; }
        }
        else
        {
            // the latest line that started over 2 s ago
            if (pos - song_ms_at(&g_song, st) > 2000 && now_b - st < best_gap) { best_gap = now_b - st; target = st; }
        }
    }
    int32_t to_ms;
    if (target < 0)
    {
        if (dir > 0) return;         // no line ahead
        to_ms = 0;                    // back to the start
        target = -1e9f;
    }
    else to_ms = song_ms_at(&g_song, target) - 2000;   // a 2 s run-up
    if (to_ms < 0) to_ms = 0;
    if (dir < 0) undo_from(target);
    else for (int p = 0; p < 2; p++) { close_line(&pl[p]); pl[p].trail_line = -1; pl[p].prev_ms = 0; }
    paused = false;
    music_seek(to_ms);
    snprintf(jump_msg, sizeof jump_msg, "%s", dir > 0 ? "SKIP >>" : "<< AGAIN");
    jump_msg_until = plat_millis() + 1200;
}

static void toggle_pause(void)
{
    paused = !paused;
    music_pause(paused);
}

void game_init(void)
{
    settings_load();
    g_mic_delay = g_set.mic_delay;
    plat_set_volume(g_set.volume);
    singers_load();
    if (g_who[1] >= singers_count()) g_who[1] = -1;
    g_sel = 0;
    msg[0] = 0;
    gi_go(GS_TITLE);
}

static void finish(void)
{
    close_line(&pl[0]);
    close_line(&pl[1]);
    music_stop();
    settings_save();                 // (volume, sync)
    for (int p = 0; p < 2; p++)
        if (g_who[p] >= 0 && sang(p))
            singers_record(g_who[p], game_score(p), g_song.artist, g_song.title);
    gi_go(GS_RESULT);
}

bool gi_start_song(void)
{
    char err[48];
    if (lib_count() == 0) return false;
    if (!plat_song_load(g_sel, &g_song, err, sizeof err)) { set_msg(err); return false; }
    if (g_song.n_notes == 0) { set_msg("NO NOTES IN THIS SONG"); return false; }
    start_scoring();
    comment[0] = comment[1] = NULL;
    song_len_ms = song_length_ms(&g_song);
    cur_song = g_sel;
    if (!music_start(g_sel, &g_song, err, sizeof err)) { set_msg(err); return false; }
    started = false;
    gi_go(GS_PLAY);
    return true;
}

void game_key(uint8_t k)
{
    if (menus_asking()) { menus_key(k); return; }
    switch (g_state)
    {
        case GS_PLAY:
            if (k == KEY_ESC) { if (paused) toggle_pause(); finish(); }
            else if (k == KEY_SPACE && music_waiting()) music_go_now();
            else if (k == KEY_SPACE) toggle_pause();
            else if (k == KEY_UP || k == KEY_DOWN)
            {
                music_volume(g_set.volume + (k == KEY_UP ? 5 : -5));
                vol_shown_until = plat_millis() + 1500;
            }
            else if ((k == ',' || k == '.') && music_on_pc())
            {
                // the PC's player a little earlier or later than the notes
                g_set.pc_sync += k == '.' ? 50 : -50;
                vol_shown_until = plat_millis() + 1500;
            }
            else if (k == 'N' || k == 'n' || k == KEY_RIGHT) jump(1);
            else if (k == 'P' || k == 'p' || k == KEY_LEFT) jump(-1);
            else if (k == '+' || k == '=') game_set_mic_delay(g_mic_delay + 10);
            else if (k == '-') game_set_mic_delay(g_mic_delay - 10);
            break;
        case GS_RESULT:
            if (k == KEY_RETURN || k == KEY_SPACE || k == KEY_ESC) gi_go(GS_SONGS);
            else if ((k == 'D' || k == 'd') && best_delay() >= 0) game_set_mic_delay(best_delay());
            else if (k == 0x7F) menus_ask_delete();      // DEL: a song that needs work goes
            break;
        default:
            menus_key(k);
            break;
    }
}

void game_update(void)
{
    pc_poll();
    usdb_poll();
    // the title tune on the title screen and in the menus without music of their own
    plat_music(g_state == GS_TITLE || g_state == GS_MAIN || g_state == GS_SINGERS || g_state == GS_SCORES);
    if (g_state != GS_PLAY) { menus_update(); return; }
    if (paused) return;
    int32_t pos = music_ms();
    if (pos == MUSIC_WAITING) return;
    if (pos < 0 && !started && music_on_pc())
    {
        // the PC's player never started: back to the songs, with why
        set_msg(music_message()[0] ? music_message() : "THE PC'S PLAYER DID NOT START");
        music_stop();
        gi_go(GS_SONGS);
        return;
    }
    if (pos >= 0) started = true;
    // (a video goes on after the song: it ends 4 s after the last note)
    bool past = music_on_pc() && pos > song_len_ms + 4000;
    if (pos < 0 || past || (g_song.end_ms > 0 && pos >= g_song.end_ms)) finish();
}

// ---------------------------------------------------------------- drawing
// Centred text; up to 36 characters with 7-pixel spacing, longer ends in ".."
void gi_text_center(fb_t *f, int y, const char *s, int len, uint8_t c)
{
    int adv = 8;
    bool cut = false;
    if (len > 32) adv = 7;
    if (len > 36) { len = 34; cut = true; }
    int x = (FB_W - (len + (cut ? 2 : 0)) * adv) / 2;
    for (int i = 0; i < len; i++) fb_text(f, x + i * adv, y, s + i, 1, c);
    if (cut) fb_text(f, x + len * adv, y, "..", 2, c), fb_text(f, x + (len + 1) * adv, y, ".", 1, c);
}

void gi_meters(fb_t *f, int y)
{
    static const char *names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int p = 0; p < 2; p++)
    {
        int x0 = p ? 136 : 8;
        const pitch_result_t *r = &last[p];
        bool fresh = last_ms[p] && plat_millis() - last_ms[p] < 500;
        int len = fresh ? (int)((r->dbfs + 60.0f) * 1.3f) : 0;
        if (len < 0) len = 0;
        if (len > 72) len = 72;
        fb_rect(f, x0, y + 2, 72, 4, C_DARK);
        fb_rect(f, x0, y + 2, len, 4, p ? C_P2 : C_P1);
        if (fresh && r->voiced && r->note >= 12)
        {
            char t[8];
            int n = snprintf(t, sizeof t, "%s%d", names[r->note % 12], r->note / 12 - 1);
            fb_text(f, x0 + 78, y, t, n, p ? C_P2_HIT : C_P1_HIT);
        }
    }
}

// Build a line's text with a colour per character; returns the length
static int line_text(int line, float beat, bool active, char *buf, uint8_t *col, int max)
{
    const line_t *ln = &g_song.lines[line];
    int n = 0;
    for (int i = 0; i < ln->count && n < max; i++)
    {
        const note_t *nt = &g_song.notes[ln->first + i];
        uint8_t c = C_GREY;
        if (active)
        {
            if (beat >= (float)(nt->start + nt->len)) c = C_YELLOW;
            else if (beat >= (float)nt->start) c = C_CYAN;
            else c = C_WHITE;
        }
        const char *s = song_note_text(&g_song, nt);
        for (int k = 0; k < nt->text_len && n < max; k++)
        {
            buf[n] = s[k];
            col[n] = c;
            n++;
        }
    }
    // no leading spaces
    int skip = 0;
    while (skip < n && buf[skip] == ' ') skip++;
    if (skip)
    {
        memmove(buf, buf + skip, (size_t)(n - skip));
        memmove(col, col + skip, (size_t)(n - skip));
        n -= skip;
    }
    return n;
}

// adv: 8 pixels per character, or 7 for long lines (the font's letters
// are 7 pixels wide, so 36 characters fit on a row)
static void draw_text_line(fb_t *f, int y, const char *buf, const uint8_t *col, int n, int adv)
{
    int x = (FB_W - n * adv) / 2;
    for (int i = 0; i < n; i++) fb_text(f, x + i * adv, y, buf + i, 1, col[i]);
}

// One or two rows of lyrics; returns rows used
static int draw_lyrics(fb_t *f, int line, float beat, bool active, int y, int max_rows)
{
    char buf[160];
    uint8_t col[160];
    int n = line_text(line, beat, active, buf, col, (int)sizeof buf);
    while (n > 0 && buf[n - 1] == ' ') n--;
    if (n <= 32) { draw_text_line(f, y, buf, col, n, 8); return 1; }
    if (n <= 36) { draw_text_line(f, y, buf, col, n, 7); return 1; }
    int cut = 36;
    while (cut > 8 && buf[cut] != ' ') cut--;
    if (buf[cut] != ' ') cut = 36;
    draw_text_line(f, y, buf, col, cut, 7);
    if (max_rows < 2) return 1;
    int rest = n - cut;
    const char *b2 = buf + cut;
    const uint8_t *c2 = col + cut;
    while (rest > 0 && *b2 == ' ') { b2++; c2++; rest--; }
    if (rest > 36) rest = 36;
    draw_text_line(f, y + 10, b2, c2, rest, rest <= 32 ? 8 : 7);
    return 2;
}

static void draw_lane(fb_t *f, int track, float beat)
{
    int y0, h;
    lane_of(track, &y0, &h);
    fb_rect(f, 0, y0, FB_W, h, C_LANE);
    for (int i = 1; i < 4; i++) fb_rect(f, 0, y0 + i * h / 4, FB_W, 1, C_GRID);

    int line = song_line_at(&g_song, track, beat);
    if (line < 0) return;
    layout_t L;
    layout(line, track, &L);
    const line_t *ln = &g_song.lines[line];
    for (int i = 0; i < ln->count; i++)
    {
        const note_t *n = &g_song.notes[ln->first + i];
        int x0 = beat_x(&L, (float)n->start);
        int x1 = beat_x(&L, (float)(n->start + n->len)) - 1;
        if (x1 <= x0) x1 = x0 + 1;
        int y = pitch_y(&L, (float)n->pitch) - L.bar / 2;
        uint8_t c = (n->type == NOTE_GOLDEN || n->type == NOTE_RAP_GOLDEN) ? C_GOLD :
                    (n->type == NOTE_NORMAL) ? C_NOTE : C_RAP;
        fb_rect(f, x0, y, x1 - x0, L.bar, c);
    }

    // the sung pitch
    for (int p = 0; p < 2; p++)
    {
        if (track_of(p) != track) continue;
        const player_t *P = &pl[p];
        if (P->trail_line != line) continue;
        // blue on the upper half of the note, red on the lower: both show
        // when the two mics sing the same note
        int dy = p ? 1 : -2;
        for (int x = 0; x < LANE_W; x++)
            if (P->ty[x])
                fb_rect(f, LANE_X0 + x, P->ty[x] + dy, 1, 2,
                        P->th[x] ? (p ? C_P2_HIT : C_P1_HIT) : (p ? C_P2 : C_P1));
    }

    // where the music is
    if (beat >= L.b0 && beat < L.b1) fb_rect(f, beat_x(&L, beat), y0, 1, h, C_WHITE);
}

static void draw_play(fb_t *f)
{
    fb_clear(f, C_BLACK);
    int32_t pos = music_ms();
    if (pos < 0) pos = 0;
    float beat = song_beat_at(&g_song, pos);

    for (int tr = 0; tr < g_song.n_tracks; tr++) draw_lane(f, tr, beat);

    char t[80];
    int n = (int)strlen(g_song.title);
    gi_text_center(f, 1, g_song.title, n, C_GREY);
    for (int p = 0; p < 2; p++)
    {
        if (p == 1 && !(g_song.duet || sang(1) || g_who[1] >= 0)) continue;
        const singer_t *sg = singer_get(g_who[p]);
        n = snprintf(t, sizeof t, "%05d", game_score(p));
        int x = p ? FB_W - 4 - n * 8 : 4;
        fb_text(f, x, 13, t, n, p ? C_P2_HIT : C_P1_HIT);
        if (sg)
        {
            // the singer's name next to the score, in the mic's colour
            int nl = (int)strlen(sg->name);
            fb_text(f, p ? x - 4 - nl * 8 : x + n * 8 + 4, 13, sg->name, nl, p ? C_P2 : C_P1);
        }
        // a comment on the line just sung
        if (comment[p] && plat_millis() < comment_until[p])
        {
            int cl = (int)strlen(comment[p]);
            int y0, h;
            lane_of(track_of(p), &y0, &h);
            int cx = p ? FB_W - 6 - cl * 8 : 6;
            fb_rect(f, cx - 2, y0 + h - 12, cl * 8 + 4, 11, p ? C_P2 : C_P1);
            fb_text(f, cx, y0 + h - 10, comment[p], cl, C_WHITE);
        }
    }
    // progress
    int bar_w = 96;
    fb_rect(f, 80, 22, bar_w, 2, C_DARK);
    if (song_len_ms > 0)
    {
        int done = (int)((int64_t)pos * bar_w / song_len_ms);
        if (done > bar_w) done = bar_w;
        fb_rect(f, 80, 22, done, 2, C_GREY);
    }


    if (g_song.duet)
    {
        int l0 = song_line_at(&g_song, 0, beat), l1 = song_line_at(&g_song, 1, beat);
        if (l0 >= 0) draw_lyrics(f, l0, beat, true, 148, 2);
        if (l1 >= 0) draw_lyrics(f, l1, beat, true, 176, 2);
    }
    else
    {
        int l0 = song_line_at(&g_song, 0, beat);
        if (l0 >= 0)
        {
            draw_lyrics(f, l0, beat, true, 148, 2);
            const track_t *t0 = &g_song.tracks[0];
            if (l0 + 1 < t0->first_line + t0->n_lines) draw_lyrics(f, l0 + 1, beat, false, 176, 2);
        }
        else
        {
            gi_text_center(f, 160, "THE END", 7, C_GREY);
        }
    }

    // a count-in before the first line of the song
    if (g_song.n_lines)
    {
        int32_t first = song_ms_at(&g_song, (float)g_song.lines[0].start);
        int32_t left = first - pos;
        if (left > 0 && left < 3000)
        {
            n = snprintf(t, sizeof t, "%d", (int)(left / 1000) + 1);
            fb_text(f, 124, 128, t, n, C_YELLOW);
        }
    }
    // paused, or just jumped
    char vb[40];
    const char *banner = paused ? "PAUSED - SPACE GOES ON" : plat_millis() < jump_msg_until ? jump_msg : NULL;
    if (!banner && music_waiting()) banner = music_message();
    if (!banner && plat_millis() < vol_shown_until)
    {
        if (music_on_pc()) snprintf(vb, sizeof vb, "VOLUME %d  SYNC %+dMS", g_set.volume, g_set.pc_sync);
        else snprintf(vb, sizeof vb, "VOLUME %d", g_set.volume);
        banner = vb;
    }
    if (banner && !banner[0]) banner = NULL;
    if (banner)
    {
        int bl = (int)strlen(banner);
        fb_rect(f, (FB_W - bl * 8) / 2 - 6, 70, bl * 8 + 12, 15, C_YELLOW);
        gi_text_center(f, 74, banner, bl, C_BLACK);
    }
    gi_meters(f, 202);
}

static const char *rating(int s)
{
    if (s < 2000) return "KEEP PRACTISING";
    if (s < 4000) return "NOT BAD";
    if (s < 6000) return "GOOD";
    if (s < 7500) return "GREAT";
    if (s < 9000) return "SUPERSTAR";
    return "PERFECT!";
}

static void draw_result(fb_t *f)
{
    fb_clear(f, C_BLACK);
    fb_rect(f, 0, 0, FB_W, 11, C_LANE);
    gi_text_center(f, 2, "RESULTS", 7, C_YELLOW);
    char t[80];
    int n;
    gi_text_center(f, 16, g_song.title, (int)strlen(g_song.title), C_WHITE);
    gi_text_center(f, 26, g_song.artist, (int)strlen(g_song.artist), C_GREY);

    for (int p = 0; p < 2; p++)
    {
        int y = 50 + p * 60;
        uint8_t c = p ? C_P2_HIT : C_P1_HIT;
        fb_frame(f, 16, y - 6, FB_W - 32, 50, p ? C_P2 : C_P1);
        const singer_t *sg = singer_get(g_who[p]);
        int tx = 28;
        if (sg)
        {
            avatar_draw(f, sg->avatar, 22, y - 2, 2);
            tx = 60;
            fb_textz(f, tx, y, sg->name, c);
        }
        else fb_textz(f, tx, y, p ? "RED MIC" : "BLUE MIC", c);
        if (!sang(p))
        {
            fb_textz(f, tx, y + 16, "DID NOT SING", C_GREY);
            continue;
        }
        n = snprintf(t, sizeof t, "%d", game_score(p));
        fb_text(f, FB_W - 28 - n * 8, y, t, n, C_WHITE);
        const char *r = rating(game_score(p));
        fb_textz(f, tx, y + 16, r, C_YELLOW);
        int bar = (int)((FB_W - 28 - tx) * game_score(p) / 10000);
        fb_rect(f, tx, y + 30, FB_W - 28 - tx, 4, C_DARK);
        fb_rect(f, tx, y + 30, bar, 4, c);
    }
    int bd = best_delay();
    if (bd >= 0)
    {
        n = snprintf(t, sizeof t, "BEST MIC DELAY %dMS (NOW %d)", bd, g_mic_delay);
        gi_text_center(f, 168, t, n, bd == g_mic_delay ? C_GREEN : C_WHITE);
        if (bd != g_mic_delay) gi_text_center(f, 178, "D: USE IT", 9, C_YELLOW);
    }
    gi_text_center(f, 192, "RETURN: BACK TO THE SONGS", 25, C_GREEN);
    gi_text_center(f, 202, "DEL: DELETE THIS SONG", 21, C_GREY);
}

void game_draw(fb_t *f)
{
    switch (g_state)
    {
        case GS_TITLE:  title_draw(f, plat_millis() - g_state_ms); break;
        case GS_PLAY:   draw_play(f); break;
        case GS_RESULT: draw_result(f); break;
        default:        menus_draw(f); break;
    }
    menus_overlay(f);
}

void game_palette(uint8_t pal[32])
{
    memcpy(pal, fb_palette, 32);
    switch (g_state)
    {
        case GS_TITLE:
            title_palette(pal, plat_millis() - g_state_ms);
            break;
        case GS_PLAY:
        {
            // the lanes light up on every beat of the music (a quarter note:
            // four UltraStar beats) and fade
            int32_t pos = music_ms();
            if (pos < 0) break;
            float b = song_beat_at(&g_song, pos) / 4.0f;
            if (b < 0) break;
            float ph = b - floorf(b);
            int lift = ph < 0.08f ? 2 : ph < 0.2f ? 1 : 0;
            pal[C_LANE * 2] = (uint8_t)((0 + (lift > 1 ? 1 : 0)) << 4 | (2 + lift));
            pal[C_LANE * 2 + 1] = (uint8_t)(lift > 1 ? 1 : 0);
            break;
        }
        default:
            menus_palette(pal);
            break;
    }
}
