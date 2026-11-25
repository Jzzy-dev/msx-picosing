// PicoSing - karaoke for the MSX PicoVerse 2350
// game_int.h - what game.c (singing, results), menus.c (the other screens)
// and title.c share. Not for the platform.

#ifndef GAME_INT_H
#define GAME_INT_H

#include "game.h"
#include "song.h"
#include "fb.h"
#include "singers.h"
#include "cover.h"

extern game_state_t g_state;
extern song_t g_song;
extern int g_sel;              // the song list's cursor
extern int g_who[2];           // the singer at each mic (blue, red), -1 = nobody
extern int g_mic_delay;
extern uint32_t g_state_ms;    // when the screen came up

void gi_go(game_state_t s);
void gi_set_msg(const char *m);
void gi_draw_msg(fb_t *f, int y);
void gi_text_center(fb_t *f, int y, const char *s, int len, uint8_t c);
void gi_meters(fb_t *f, int y);
bool gi_start_song(void);      // song g_sel with g_who; false (and a message) when it cannot

// menus.c
void menus_enter(game_state_t s);
void menus_key(uint8_t k);
void menus_update(void);
void menus_draw(fb_t *f);
void menus_palette(uint8_t pal[32]);
void menus_ask_delete(void);   // "delete song g_sel?" over the current screen
bool menus_asking(void);       // that question is up: keys go to menus_key
void menus_overlay(fb_t *f);

// title.c
void title_draw(fb_t *f, uint32_t t_ms);
void title_palette(uint8_t pal[32], uint32_t t_ms);

#endif
