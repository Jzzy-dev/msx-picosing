// PicoSing - karaoke for the MSX PicoVerse 2350
// pclink.h - the music from a player on the PC, through PicoAmp for Windows
//
// PicoAmp for Windows (its MSX link, on port 8765) follows what the PC
// plays: a YouTube video in the browser, Spotify, ... PicoSing asks it to
// open the song (/sing/open), follows the player's time on its screen
// stream (/screen: a "T <ms> <length> <p|a|s> <volume> <muted>" line every
// half second) and sends it pause, seek and volume (/cmd).

#ifndef PICOSING_PCLINK_H
#define PICOSING_PCLINK_H

#include <stdint.h>
#include <stdbool.h>
#include "song.h"

enum { PC_OFF, PC_CONNECTING, PC_WAITING, PC_PLAYING, PC_FAILED };

void pc_poll(void);
// open the song on the PC (its YouTube video, else a Spotify search)
bool pc_start(const song_t *s);
// a song of the PC's song folder: its MP3 (the karaoke folder's path) plays in PicoAmp for Windows
bool pc_start_file(const song_t *s, const char *file);
void pc_go_now(void);                 // waiting: the player plays the song now
void pc_stop(void);                   // pause the player, close the stream
int  pc_state(void);
int32_t pc_ms(void);                  // the song's time (-1: not playing yet)
bool pc_paused(void);
void pc_pause(bool on);
void pc_seek(int32_t ms);             // (to the second)
int  pc_volume(void);                 // the player's (-1: not known)
void pc_set_volume(int v);
const char *pc_message(void);         // what goes on / went wrong
const char *pc_title(void);           // what the PC plays
bool pc_test(void);                   // ask the PC (the settings' test): pc_state, pc_message
#endif
