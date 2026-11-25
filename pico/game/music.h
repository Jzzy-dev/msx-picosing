// PicoSing - karaoke for the MSX PicoVerse 2350
// music.h - the song's music: the card's MP3, or the PC's player

#ifndef PICOSING_MUSIC_H
#define PICOSING_MUSIC_H

#include <stdint.h>
#include <stdbool.h>
#include "song.h"

#define MUSIC_WAITING (-2)              // the PC's player has not started the song yet

bool    music_start(int i, const song_t *s, char *err, int n);
int32_t music_ms(void);                 // the song's time; -1 ended / failed, MUSIC_WAITING
bool    music_waiting(void);
void    music_go_now(void);             // the PC's player plays the song now (SPACE while waiting)
void    music_stop(void);
void    music_pause(bool on);
void    music_seek(int32_t ms);
void    music_volume(int v);            // 0..100 (kept in the settings)
bool    music_on_pc(void);
const char *music_message(void);

#endif
