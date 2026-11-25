// PicoSing - plat_pico.h (see plat_pico.c)
#ifndef PLAT_PICO_H
#define PLAT_PICO_H
void plat_init(void);        // mount the card, read the song list, start the audio output
void plat_audio_pump(void);  // decode more music when there is room (call often)
#endif
