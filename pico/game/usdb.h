// PicoSing - karaoke for the MSX PicoVerse 2350
// usdb.h - find songs on USDB (usdb.animux.de) and save them on the card
//
// USDB has the notes and words (.txt) of UltraStar songs, no music; it asks
// for a (free) account. PicoSing logs in with the name and password in the
// settings, over the ESP-01 (HTTPS). When that can't be reached, PicoAmp for
// Windows does it for the MSX (its /usdb/ bridge), with the same name, or
// its own when the MSX has none.

#ifndef PICOSING_USDB_H
#define PICOSING_USDB_H

#include <stdint.h>
#include <stdbool.h>

#define USDB_MAX 40

typedef struct {
    uint32_t id;
    char artist[44];
    char title[44];
    char lang[16];
    char edition[24];
} usdb_hit_t;

enum { U_IDLE, U_BUSY, U_RESULTS, U_SAVED, U_ERROR };

void usdb_poll(void);
bool usdb_search(const char *artist, const char *title);   // the first page
bool usdb_more(void);                       // the next page (when usdb_has_more())
bool usdb_has_more(void);
bool usdb_download(int i);                  // into <song folder>/USDB/<artist> - <title>/
void usdb_cancel(void);
int  usdb_state(void);
const char *usdb_message(void);
int  usdb_count(void);
const usdb_hit_t *usdb_hit(int i);
const char *usdb_saved(void);               // the .txt just saved
bool usdb_via_pc(void);                     // the PC fetches for us

// for the tests: where USDB is
extern const char *usdb_base;               // "https://usdb.animux.de/"

#endif
