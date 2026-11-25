// PicoSing - karaoke for the MSX PicoVerse 2350
// scratch.h - one big buffer for the jobs that never run at the same time
// (the title's plasma, a cover being decoded, the scores file, the tag
// editor's copying, a folder's list): the RP2350's RAM is not endless.
#ifndef PICOSING_SCRATCH_H
#define PICOSING_SCRATCH_H
#include <stdint.h>
#include <stdbool.h>

#define SCRATCH_SIZE 27648u             // 96 x 96 RGB (a cover); the plasma is 27136
enum { SCR_NONE, SCR_TITLE, SCR_COVER, SCR_SCORES, SCR_ID3, SCR_DIR };

extern uint8_t g_scratch[SCRATCH_SIZE];
void *scratch(int owner);               // the buffer, now this owner's
bool  scratch_mine(int owner);          // still as this owner left it
#endif
