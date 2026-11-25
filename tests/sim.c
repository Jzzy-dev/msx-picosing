// PicoSing PC simulator: runs the real game code (song parser, scoring,
// drawing, display lists) with a simulated clock, simulated singers and the
// songs in a folder, and saves screenshots (PPM) of what the MSX would show.
//
// usage: sim <songs folder> <out folder> [list]   (list: dump display lists too)

#include "../pico/game/game.h"
#include "../pico/game/platform.h"
#include "../pico/game/library.h"
#include "../pico/game/dlist.h"
#include "../pico/game/singers.h"
#include "../pico/game/id3.h"
#include "fake_esp.h"
#include "../pico/game/library.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "simplat.h"

static const char *outdir;
static char datadir[512];
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---------------------------------------------------------------- MSX side
static fb_t want, shown, msx;
static uint8_t msx_pal[32];
static uint8_t ops[16384];
static size_t list_bytes_max;
static int lists;

static uint8_t pal_sent[32];
static int pal_lists;
static void frame(void)
{
    game_draw(&want);
    uint8_t pal[32];
    game_palette(pal);
    bool send = lists == 0 || memcmp(pal, pal_sent, 32);
    if (send) { memcpy(pal_sent, pal, 32); pal_lists++; }
    bool more;
    size_t n = dlist_build(&want, &shown, ops, 6000, send ? pal : NULL, &more);
    if (n > list_bytes_max) list_bytes_max = n;
    dlist_apply(ops, n, &msx, msx_pal);
    lists++;
}

static void shot(const char *name)
{
    // let the lists catch up, then compare and save
    for (int i = 0; i < 20 && memcmp(&msx, &want, sizeof msx); i++) frame();
    CHECK(!memcmp(&msx, &want, sizeof msx), "%s: MSX screen differs from the Pico's", name);
    char path[512]; snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++)
        {
            uint8_t b = msx.px[y][x >> 1], c = (x & 1) ? (b & 15) : (b >> 4);
            int r = (msx_pal[c * 2] >> 4) & 7, bl = msx_pal[c * 2] & 7, g = msx_pal[c * 2 + 1] & 7;
            uint8_t rgb[3] = { (uint8_t)(r * 255 / 7), (uint8_t)(g * 255 / 7), (uint8_t)(bl * 255 / 7) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static void run_until(uint32_t t_end, int skill1)
{
    while (now < t_end)
    {
        now += 1;
        fesp_tick(now);
        if (now % 23 == 0) { sim_sing(0, now, 0); sim_sing(1, now, skill1); }
        if (now % 10 == 0) game_update();
        if (now % 40 == 0) frame();
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: sim <songs> <out>\n"); return 2; }
    outdir = argv[2];
    // a copy of the songs: the test deletes and changes files
    char cmd[1600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s/card' && mkdir -p '%s/card' && cp -r '%s/KARAOKE' '%s/card/'", outdir, outdir, argv[1], outdir);
    if (system(cmd)) { printf("cannot copy the songs\n"); return 2; }
    char card[600];
    snprintf(card, sizeof card, "%s/card", outdir);
    sim_scan(card);
    printf("%d songs\n", lib_count());
    CHECK(lib_count() >= 1, "songs found");

    // a fresh data folder: two default singers, no scores
    snprintf(datadir, sizeof datadir, "%s/data", outdir);
    sim_data = datadir;
    char p[600];
    snprintf(p, sizeof p, "%s/SINGERS.TXT", datadir); remove(p);
    snprintf(p, sizeof p, "%s/SCORES.TXT", datadir); remove(p);

    game_init();
    CHECK(game_state() == GS_TITLE, "title first");
    now = 1000;
    run_until(1600, 0);
    CHECK(sim_music_on, "title tune plays");
    shot("00_title");
    run_until(2100, 0);
    shot("00_title_b");
    game_key(KEY_SPACE);
    CHECK(game_state() == GS_MAIN, "main menu");
    run_until(now + 200, 0);
    shot("01_main");

    // name the singers: ANNA (avatar 3) and BOB
    game_key(KEY_DOWN); game_key(KEY_RETURN);
    CHECK(game_state() == GS_SINGERS, "singers screen");
    for (int i = 0; i < 4; i++) game_key(0x08);
    for (const char *c = "anna"; *c; c++) game_key((uint8_t)*c);
    game_key(KEY_RIGHT); game_key(KEY_RIGHT); game_key(KEY_RIGHT);
    game_key(KEY_DOWN);
    for (int i = 0; i < 3; i++) game_key(0x08);
    for (const char *c = "bob"; *c; c++) game_key((uint8_t)*c);
    game_key(KEY_DOWN); game_key(KEY_RETURN);   // + add a singer
    run_until(now + 200, 0);
    shot("02_singers");
    game_key(KEY_ESC);
    CHECK(game_state() == GS_MAIN, "back to main");
    CHECK(!strcmp(singer_get(0)->name, "ANNA") && singer_get(0)->avatar == 3, "singer 0 edited (%s)", singer_get(0)->name);

    game_key(KEY_UP);               // SING
    game_key(KEY_RETURN);
    CHECK(game_state() == GS_SONGS, "song list");
    run_until(now + 200, 0);
    shot("03_songs");
    game_key(KEY_RETURN);
    CHECK(game_state() == GS_DETAILS, "details");
    run_until(now + 600, 0);
    shot("04_details");
    game_key(KEY_RIGHT);            // flick to the duet
    run_until(now + 150, 0);
    run_until(now + 600, 0);
    shot("04_details_duet");
    game_key(KEY_LEFT);
    run_until(now + 600, 0);
    game_key(KEY_RETURN);
    CHECK(game_state() == GS_WHO, "who sings");
    run_until(now + 200, 0);
    shot("05_who");
    game_key(KEY_RETURN);
    CHECK(game_state() == GS_PLAY, "playing");
    const song_t *s = game_song();
    printf("song: %s - %s, %d lines, %d notes, beat %.0f ms\n", s->artist, s->title, s->n_lines, s->n_notes, song_beat_ms(s));
    run_until(now + 1500, 1);  shot("10_countin");
    run_until(now + 2700, 1);  shot("11_line1");
    run_until(now + 2600, 1);  shot("12_line1_end");
    run_until(now + 6000, 1);  shot("13_line3");
    int s0 = game_score(0), s1 = game_score(1);
    printf("scores halfway: blue %d red %d\n", s0, s1);
    run_until(now + 30000, 1);
    CHECK(game_state() == GS_RESULT, "song ended (state %d)", game_state());
    shot("14_results");
    printf("final scores: blue %d (in tune, an octave low) red %d (flat half the time)\n", game_score(0), game_score(1));
    printf("measured mic delay: %d ms (the singers come in 46 ms late)\n", game_best_delay());
    CHECK(game_best_delay() >= 30 && game_best_delay() <= 70, "delay measured (%d)", game_best_delay());
    CHECK(game_score(0) >= 9000, "perfect singer scores high (%d)", game_score(0));
    CHECK(game_score(1) > 3000 && game_score(1) < 8000, "half-flat singer in the middle (%d)", game_score(1));
    printf("display lists: %d, largest %zu bytes\n", lists, list_bytes_max);

    game_key(KEY_RETURN);
    CHECK(game_state() == GS_SONGS, "back to the songs");

    // the duet: blue sings lines 1, 3, 5 and red 2, 4, 6
    game_key(KEY_DOWN);
    game_key(KEY_RETURN);
    run_until(now + 600, 0);
    game_key(KEY_RETURN);
    game_key(KEY_RETURN);
    CHECK(game_state() == GS_PLAY && game_song()->duet, "duet playing");
    sim_late_ms = 120;
    run_until(now + 7600, 0);
    shot("15_duet");
    run_until(now + 40000, 0);
    CHECK(game_state() == GS_RESULT, "duet ended");
    printf("duet scores: blue %d red %d\n", game_score(0), game_score(1));
    printf("measured mic delay with singers 120 ms later: %d ms\n", game_best_delay());
    CHECK(game_best_delay() >= 140 && game_best_delay() <= 190, "late singers measured (%d)", game_best_delay());
    int before_d = game_mic_delay();
    game_key('d');
    CHECK(game_mic_delay() == game_best_delay() && game_mic_delay() != before_d, "D applies the measured delay");
    CHECK(game_score(0) >= 7500 && game_score(1) >= 7500, "both in tune in the duet, though late (%d %d)", game_score(0), game_score(1));
    shot("16_duet_results");
    game_key(KEY_RETURN); game_key(KEY_ESC);
    game_key(KEY_DOWN); game_key(KEY_DOWN); game_key(KEY_RETURN);
    CHECK(game_state() == GS_SCORES, "scores screen");
    CHECK(singer_get(0)->songs == 2 && singer_get(1)->songs == 2, "results recorded (%d %d)", singer_get(0)->songs, singer_get(1)->songs);
    run_until(now + 200, 0);
    shot("17_scores");
    // and the best scores show on the details screen
    game_key(KEY_ESC); game_key(KEY_UP); game_key(KEY_UP); game_key(KEY_RETURN);
    game_key(KEY_UP); game_key(KEY_RETURN);
    run_until(now + 600, 0);
    shot("18_details_best");

    // pause, skip the intro, sing, go back a line and sing it again
    game_key(KEY_RETURN); game_key(KEY_RETURN);
    CHECK(game_state() == GS_PLAY && !game_song()->duet, "third song playing");
    run_until(now + 300, 0);
    int32_t p0 = plat_audio_ms();
    game_key('n');                       // skip the intro
    int32_t p1 = plat_audio_ms();
    int32_t next_ms = -1;
    for (int l = 0; l < game_song()->n_lines && next_ms < 0; l++)
        if (song_ms_at(game_song(), (float)game_song()->lines[l].start) > p1) next_ms = song_ms_at(game_song(), (float)game_song()->lines[l].start);
    printf("N at %d ms: now %d ms (the next line at %d ms)\n", p0, p1, next_ms);
    CHECK(p1 > p0 + 500 && next_ms - p1 >= 2000 && next_ms - p1 < 3000, "N goes to 2-3 s before the next line");
    run_until(now + 9000, 0);
    int a = game_score(0);
    game_key(KEY_SPACE);
    int32_t q0 = plat_audio_ms();
    run_until(now + 3000, 0);
    shot("19_paused");
    CHECK(plat_audio_ms() == q0 && game_score(0) == a, "paused: the clock and the score stand still");
    game_key(KEY_SPACE);
    run_until(now + 500, 0);
    CHECK(plat_audio_ms() > q0, "SPACE goes on");
    int b = game_score(0);
    game_key('p');
    run_until(now + 100, 0);
    shot("20_again");
    int c = game_score(0);
    printf("score %d, after P (sing the line again) %d\n", b, c);
    CHECK(c < b, "P takes back the points of the line sung again");
    run_until(now + 60000, 0);
    CHECK(game_state() == GS_RESULT, "third song ended");
    printf("after skipping the intro and singing a line twice: %d\n", game_score(0));
    CHECK(game_score(0) >= 9000, "a line sung twice counts once (%d)", game_score(0));

    // ---- a folder of its own: /KARAOKE/80s holds one song
    char tw[1500];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/KARAOKE/80s/Twinkle Again' && cp '%s/KARAOKE/Twinkle Twinkle Little Star/Twinkle Twinkle Little Star.txt' '%s/KARAOKE/Twinkle Twinkle Little Star/Twinkle Twinkle Little Star.mp3' '%s/KARAOKE/80s/Twinkle Again/'",
             card, card, card, card);
    CHECK(system(cmd) == 0, "made a folder");
    game_key(KEY_RETURN);               // results -> songs
    game_key(KEY_ESC);                  // main menu
    CHECK(game_state() == GS_MAIN, "main menu again");
    for (int i = 0; i < 3; i++) game_key(KEY_DOWN);
    game_key(KEY_RETURN);
    CHECK(game_state() == GS_FOLDERS, "song folder screen");
    game_key(KEY_DOWN); game_key(KEY_DOWN);      // 80s
    run_until(now + 200, 0);
    shot("21_folders");
    game_key(KEY_RETURN);                        // into 80s
    game_key(KEY_RETURN);                        // sing from here
    run_until(now + 300, 0);
    CHECK(game_state() == GS_SONGS && lib_count() == 1, "songs from /KARAOKE/80s only (%d)", lib_count());

    // ---- delete it: the folder holds only this song, so the folder goes
    game_key(0x7F);
    run_until(now + 100, 0);
    shot("22_delete");
    game_key('n');
    CHECK(lib_count() == 1, "N keeps it");
    game_key(0x7F); game_key('y');
    run_until(now + 300, 0);
    snprintf(tw, sizeof tw, "%s/KARAOKE/80s/Twinkle Again", card);
    struct stat st;
    CHECK(lib_count() == 0 && stat(tw, &st) != 0, "deleted with its folder");

    // ---- back to /KARAOKE; the duet shares its folder, so only its .txt goes
    game_key(KEY_ESC);                           // the menu still points at SONG FOLDER
    game_key(KEY_RETURN);
    CHECK(game_state() == GS_FOLDERS, "song folder screen again");
    game_key(0x08);                              // BS: up to /KARAOKE
    game_key(KEY_RETURN);
    run_until(now + 300, 0);
    CHECK(lib_count() == 2, "both songs back (%d)", lib_count());
    game_key(KEY_DOWN);                          // the duet
    game_key(0x7F); game_key('y');
    run_until(now + 300, 0);
    snprintf(tw, sizeof tw, "%s/KARAOKE/Twinkle Twinkle Little Star/Twinkle Twinkle Little Star.mp3", card);
    CHECK(lib_count() == 1 && stat(tw, &st) == 0, "only the duet's .txt went");

    // ---- the tags: the album
    game_key(KEY_RETURN);                        // details
    run_until(now + 600, 0);
    game_key('e');
    CHECK(game_state() == GS_EDIT, "tag editor");
    game_key(KEY_DOWN); game_key(KEY_DOWN);      // album
    for (const char *c = "Nursery Rhymes"; *c; c++) game_key((uint8_t)*c);
    run_until(now + 100, 0);
    shot("23_tags");
    game_key(KEY_DOWN); game_key(KEY_DOWN);      // save
    game_key(KEY_RETURN);
    run_until(now + 300, 0);
    id3_text_t tg;
    CHECK(game_state() == GS_DETAILS && id3_read(tw, &tg) && !strcmp(tg.album, "Nursery Rhymes"), "album saved ('%s')", tg.album);
    run_until(now + 700, 0);
    shot("24_details_tags");

    printf("palette lists: %d of %d\n", pal_lists, lists);
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails != 0;
}
