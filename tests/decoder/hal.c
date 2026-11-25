#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "pva_internal.h"
static uint8_t arena[72 * 1024] __attribute__((aligned(4))); static size_t used;
void *pva_alloc(size_t n) { n = (n + 3) & ~3u; if (used + n > sizeof arena) return NULL; void *p = arena + used; used += n; memset(p, 0, n); return p; }
void pva_alloc_reset(void) { used = 0; }
void pva_log(const char *fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); printf("\n"); }
static int16_t buf[1152 * 2];
long frames_out;
int16_t *pva_hal_audio_acquire(uint32_t *m) { *m = 1152; return buf; }
void pva_hal_audio_commit(uint32_t f, uint32_t r) { frames_out += f; }
void pva_hal_audio_mute(bool m) {}
bool pva_hal_audio_init(void) { return true; }
uint32_t pva_hal_millis(void) { return 0; }
size_t pva_net_read(uint8_t *d, size_t n) { return 0; }
bool pva_net_ended(void) { return true; }
void pva_eq_reset(void) {}
void pva_eq_process(int16_t *s, uint32_t f, uint32_t r) {}
void pva_viz_feed(const int16_t *s, uint32_t f, uint32_t r) {}
void *(*pv_mp3_alloc_fn)(size_t size);
void *pv_mp3_alloc(size_t n) { return pv_mp3_alloc_fn ? pv_mp3_alloc_fn(n) : NULL; }
