// PicoSing - karaoke for the MSX PicoVerse 2350
// pva_internal.h - what PicoAmp's decoder (pva_decode.c, PicoVerse Audio
// Engine by Jzzy, used unchanged) needs, without the rest of the engine.
// Implemented in audio/audio_hal.c.

#ifndef PVA_INTERNAL_H
#define PVA_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "pva_proto.h"

void *pva_alloc(size_t size);       // zeroed, 4-byte aligned; NULL when full
void  pva_log(const char *fmt, ...);

bool     pva_hal_audio_init(void);
int16_t *pva_hal_audio_acquire(uint32_t *max_frames);
void     pva_hal_audio_commit(uint32_t frames, uint32_t sample_rate);
void     pva_hal_audio_mute(bool mute);
uint32_t pva_hal_millis(void);

typedef struct
{
    char     title[PVA_ST_TITLE_LEN];
    char     artist[PVA_ST_ARTIST_LEN];
    char     album[PVA_ST_ALBUM_LEN];
    bool     tagged;
    uint8_t  format;
    uint32_t total_seconds;
    uint32_t bitrate_kbps;
    uint32_t sample_rate;
    uint8_t  channels;
    bool     seekable;
    uint32_t pic_pos, pic_len;
    bool     pic_front;
} pva_track_info_t;

bool pva_dec_init(void);
const char *pva_dec_path(void);
int  pva_dec_open(const char *fatfs_path, pva_track_info_t *info);
int  pva_dec_open_stream(pva_track_info_t *info);
bool pva_dec_is_stream(void);
void pva_dec_frame_info(pva_track_info_t *info);
void pva_dec_close(void);
bool pva_dec_active(void);
int  pva_dec_step(void);
int  pva_dec_seek(uint32_t seconds);
uint32_t pva_dec_elapsed(void);
uint32_t pva_dec_elapsed_ms(void);
void pva_dec_set_volume(uint8_t vol);
void pva_dec_set_balance(uint8_t bal);
void pva_dec_vu(uint8_t *l, uint8_t *r);
uint8_t pva_dec_last_fresult(void);

int  pva_kind_from_name(const char *name);
char pva_fold_ascii(uint32_t c);
void pva_copy_ascii(char *dst, size_t size, const char *src, size_t src_len);

// not used here (no internet radio, equalizer or spectrum in PicoSing)
size_t pva_net_read(uint8_t *dst, size_t n);
bool   pva_net_ended(void);
void   pva_eq_reset(void);
void   pva_eq_process(int16_t *stereo, uint32_t frames, uint32_t rate);
void   pva_viz_feed(const int16_t *stereo, uint32_t frames, uint32_t rate);

#endif
