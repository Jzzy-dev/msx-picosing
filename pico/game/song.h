// PicoSing - karaoke for the MSX PicoVerse 2350
// song.h - UltraStar TXT songs
//
// Plain C (no Pico SDK), unit-tested on a PC.

#ifndef SONG_H
#define SONG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SONG_MAX_NOTES   2400
#define SONG_MAX_LINES   400
#define SONG_TEXT_POOL   12000
#define SONG_STR         64

enum { NOTE_NORMAL = 0, NOTE_GOLDEN, NOTE_FREESTYLE, NOTE_RAP, NOTE_RAP_GOLDEN };

typedef struct {
    int32_t  start;      // beat
    int16_t  len;        // beats
    int8_t   pitch;      // UltraStar pitch: 0 = C4 (MIDI 60)
    uint8_t  type;       // NOTE_*
    uint16_t text;       // offset in the text pool
    uint8_t  text_len;   // bytes (the syllable, display charset)
} note_t;

typedef struct {
    int32_t  start;      // first beat of the line (first note)
    int32_t  end;        // beat after its last note
    uint16_t first;      // first note index
    uint16_t count;      // notes in the line
} line_t;

typedef struct {
    uint16_t first_line, n_lines;   // in song_t.lines
} track_t;

typedef struct {
    // header
    char     title[SONG_STR];
    char     artist[SONG_STR];
    char     audio[SONG_STR * 2];   // #AUDIO or #MP3 file name
    char     genre[32];
    char     language[24];
    char     cover[SONG_STR];       // #COVER file name
    char     background[SONG_STR];  // #BACKGROUND file name
    int32_t  preview_ms;            // #PREVIEWSTART (seconds in the file) -> ms, 0 = none
    char     youtube[16];           // the YouTube video of #VIDEO ("v=xFrGuyw1V8s,co=..." or a link), "" = none
    int32_t  videogap_ms;           // #VIDEOGAP: where the song starts in the video
    int      year;
    float    bpm;                   // as in the file (quarter beats)
    int32_t  gap_ms;                // #GAP
    int32_t  start_ms;              // #START (seconds in the file) -> ms
    int32_t  end_ms;                // #END, 0 = none
    bool     relative;
    bool     duet;

    // body
    uint8_t  n_tracks;              // 1, or 2 for duets
    track_t  tracks[2];
    uint16_t n_lines;
    line_t   lines[SONG_MAX_LINES];
    uint16_t n_notes;
    note_t   notes[SONG_MAX_NOTES];
    uint16_t pool_used;
    char     pool[SONG_TEXT_POOL];

    // errors / warnings while parsing
    uint16_t bad_lines;
} song_t;

// Parse a whole file held in memory. header_only: stop at the first note
// (for the song list). Returns false when it is not an UltraStar file.
bool song_parse(const char *text, size_t len, song_t *s, bool header_only);

// Timing
float   song_beat_ms(const song_t *s);                  // one beat in ms
float   song_beat_at(const song_t *s, int32_t ms);      // beat at a time
int32_t song_ms_at(const song_t *s, float beat);        // time of a beat
int32_t song_length_ms(const song_t *s);                // end of the last note

// The line of a track that is shown at a beat: the current line, or the
// next one during a pause. -1 after the last line.
int song_line_at(const song_t *s, int track, float beat);

// Text of a note (not 0-terminated, display charset: see song_text_char)
static inline const char *song_note_text(const song_t *s, const note_t *n) { return s->pool + n->text; }

// Convert UTF-8 or Latin-1 to the display charset (Latin-1, one byte per
// character). Unknown characters become '?'. Returns the length.
size_t song_to_latin1(char *dst, size_t size, const char *src, size_t len);

#endif
