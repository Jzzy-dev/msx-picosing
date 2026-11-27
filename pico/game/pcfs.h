// PicoSing - karaoke for the MSX PicoVerse 2350
// pcfs.h - the song folder on a PC: PicoAmp for Windows' karaoke folder
// (its /sing/list, /sing/songs, /sing/file), as "PC:" or "PC:/a/b" paths.
//
// The requests are short and waited for, as the card's reads are (the
// screen stands still for a moment). The songs' music plays on the PC
// (/sing/open?file=: pclink.c), so only the .txt files, the folders and the
// covers come over.

#ifndef PICOSING_PCFS_H
#define PICOSING_PCFS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "platform.h"

#define PCFS_ROOT "PC:"

bool pcfs_is(const char *path);                 // a "PC:" path
const char *pcfs_rel(const char *path);         // its part under the PC's folder ("" at its top)

int  pcfs_scan(const char *root);               // the songs under it into the library: how many, -1: no answer
int  pcfs_list(const char *path, plat_dirent_t *out, int max);
bool pcfs_exists(const char *path);             // (the folder's list, kept)
bool pcfs_read(const char *path, char *buf, size_t max, size_t *len);   // a small file (a .txt): 0-terminated

// a file streamed (a cover): from off, at most len bytes (0: to its end)
bool   pcfs_stream_open(const char *path, uint32_t off, uint32_t len);
size_t pcfs_stream_read(uint8_t *buf, size_t n);
void   pcfs_stream_close(void);

const char *pcfs_error(void);                   // why the last one failed (for a message)

// the platform's: while a request is waited for (the simulator's clock goes on)
void plat_idle(void);

#endif
