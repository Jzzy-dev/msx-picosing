// PicoSing - audio_hal.h: song clock support (see audio_hal.c)
#ifndef AUDIO_HAL_H
#define AUDIO_HAL_H
#include <stdint.h>
// Call before the first buffer of a song: the next commit records its time.
void audio_hal_mark_start(void);
// Time of that first commit (0: not yet). The music starts playing then.
uint32_t audio_hal_first_commit_ms(void);
#endif
