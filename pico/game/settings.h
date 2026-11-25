// PicoSing - karaoke for the MSX PicoVerse 2350
// settings.h - what is kept between sessions in <data>/SETTINGS.TXT
// (the song folder is the platform's: ROOT=)

#ifndef PICOSING_SETTINGS_H
#define PICOSING_SETTINGS_H

#include <stdbool.h>

typedef struct {
    bool music_pc;          // the music from the PC's player (else the card's MP3s)
    char pc[64];            // PicoAmp for Windows' MSX link: "192.168.1.20:8765"
    char usdb_user[40];
    char usdb_pass[40];
    int  volume;            // 0..100
    int  mic_delay;         // ms
    int  pc_sync;           // ms added to the PC player's time
} settings_t;

extern settings_t g_set;

void settings_load(void);
void settings_save(void);
// The PC's address as it is typed ("http://192.168.2.73:8765/", "192.168.2.73") made into
// "192.168.2.73:8765": no http://, no path, the MSX link's port when none is given
void settings_fix_pc(void);

#endif
