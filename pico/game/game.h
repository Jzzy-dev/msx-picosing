// PicoSing - karaoke for the MSX PicoVerse 2350
// game.h - menu, singing, scoring and results
//
// Plain C over platform.h; runs the same on the cartridge and on a PC.

#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>
#include "fb.h"
#include "song.h"
#include "../pitch.h"

// Keys (the MSX CHGET codes; the ROM maps the joystick to them)
#define KEY_RIGHT  0x1C
#define KEY_LEFT   0x1D
#define KEY_UP     0x1E
#define KEY_DOWN   0x1F
#define KEY_RETURN 0x0D
#define KEY_ESC    0x1B
#define KEY_SPACE  0x20

typedef enum {
    GS_TITLE = 0,   // title screen (music)
    GS_MAIN,        // main menu: sing, singers, scores, mic delay
    GS_SONGS,       // the song list
    GS_DETAILS,     // one song: cover, tags, best scores, preview
    GS_WHO,         // who sings at which mic
    GS_PLAY,
    GS_RESULT,
    GS_SINGERS,     // names and avatars
    GS_SCORES,      // this session's scores
    GS_EDIT,        // the MP3's tags
    GS_FOLDERS,     // which folder the songs come from
    GS_USDB,        // find songs on USDB
    GS_SETTINGS,
} game_state_t;

void game_init(void);
void game_key(uint8_t key);
// A pitch analysis of mic p (0 = blue/left, 1 = red/right), made at now_ms
// over the last ~93 ms of sound.
void game_mic(int p, const pitch_result_t *r, uint32_t now_ms);
// Call often: follows the music, ends the song.
void game_update(void);
// Draw the current screen.
void game_draw(fb_t *f);
// The current screen's palette (V9938 format): it can change every frame.
void game_palette(uint8_t pal[32]);

game_state_t game_state(void);
int  game_score(int p);
int  game_mic_delay(void);
void game_set_mic_delay(int ms);
int  game_best_delay(void);    // measured over the last song, -1 = not enough singing
song_t *game_song(void);      // the song buffer (also used for library scans)

#endif
