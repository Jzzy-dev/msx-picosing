// PicoSing - karaoke for the MSX PicoVerse 2350
// synth.h - the title tune, synthesized by the Pico (an original 8-bar loop:
// pulse lead with vibrato, triangle bass, soft square chords, drums)

#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

void synth_reset(void);
// Write 'frames' stereo frames (L, R) at 'rate' Hz; returns the beat (0..)
// so a screen can pulse with it.
void synth_render(int16_t *stereo, uint32_t frames, uint32_t rate);
uint32_t synth_step(void);      // 1/16 notes played since synth_reset()

#endif
