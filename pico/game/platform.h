// PicoSing - karaoke for the MSX PicoVerse 2350
// platform.h - what the game needs from the hardware (plat_pico.c on the
// cartridge, the PC simulator in tests/)

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include "song.h"

uint32_t plat_millis(void);

// Read and parse the .txt of library entry i. err: a short message on failure.
bool plat_song_load(int i, song_t *s, char *err, int err_size);

// Start the song's audio (song->audio, in the .txt's folder) from 0 ms
// (or from #START), or from 'from_ms' for a preview (from_ms >= 0).
bool plat_audio_start(int i, const song_t *s, char *err, int err_size);
bool plat_audio_preview(int i, const song_t *s, int32_t from_ms);
void plat_audio_stop(void);
// Pause (true) or go on (false); the clock stands still while paused
void plat_audio_pause(bool on);
// Go to ms in the song (to the whole second at or before it on the Pico)
void plat_audio_seek(int32_t ms);
// Playback position in ms (the music the singer hears), -1 when it ended.
int32_t plat_audio_ms(void);

// Card state for the menu: 0 ok, else a short message ("NO CARD", ...)
const char *plat_card_problem(void);

// ---- files (PicoSing's own: singers, scores) ----
// The data folder: "<songs root>/PICOSING", made when missing.
const char *plat_data_dir(void);
bool plat_read_file(const char *path, char *buf, size_t max, size_t *len);   // 0-terminates
bool plat_write_file(const char *path, const char *data, size_t len);        // replaces
bool plat_append_file(const char *path, const char *data, size_t len);

// ---- a song's folder and pictures ----
// The folder of library entry i (no trailing '/').
bool plat_song_dir(int i, char *dir, size_t size);
bool plat_exists(const char *path);
// One file open for streaming (a JPEG for the cover): from offset, at most
// len bytes (0 = to the end of the file).
bool   plat_stream_open(const char *path, uint32_t offset, uint32_t len);
size_t plat_stream_read(uint8_t *buf, size_t n);
void   plat_stream_close(void);

// ---- the song's audio file: tags ----
typedef struct {
    bool     ok;
    char     title[48], artist[48], album[48];
    uint32_t seconds, kbps, rate;
    uint32_t pic_pos, pic_len;     // an embedded picture (ID3 APIC), 0 = none
} song_tags_t;
bool plat_song_tags(int i, const song_t *s, song_tags_t *t);

// ---- title music (the Pico's own synthesizer while no song plays) ----
void plat_music(bool on);
// The sound's volume, 0..100 (the songs and the title tune)
void plat_set_volume(int v);
// a folder (and the ones above it)
bool plat_mkdir(const char *path);
// a song saved by PicoSing (USDB): into the list now
bool plat_add_song(const char *txt_path);

// ---------------------------------------------------------------- folders, files
// The song folder (a FatFs path on the Pico, "0:/KARAOKE"); choosing one
// scans it again (songs up to 4 folders deep) and remembers it
const char *plat_song_root(void);
bool plat_set_song_root(const char *path);

typedef struct { char name[64]; bool dir; uint32_t size; } plat_dirent_t;
int  plat_list_dir(const char *path, plat_dirent_t *out, int max);   // -1: no folder
bool plat_remove(const char *path);          // a file or an empty folder
bool plat_rename(const char *from, const char *to);

// Up to two open files (handles 0, 1; -1 = failed). write: read and write,
// created when missing, at offset 0.
int      plat_fopen(const char *path, bool write);
size_t   plat_fread(int h, void *buf, size_t n);
size_t   plat_fwrite(int h, const void *buf, size_t n);
bool     plat_fseek(int h, uint32_t off);
uint32_t plat_fsize(int h);
bool     plat_ftruncate(int h);              // cut the file at the current offset
void     plat_fclose(int h);

#endif
