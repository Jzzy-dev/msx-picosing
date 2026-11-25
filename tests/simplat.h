// PicoSing PC simulator: platform and singers (see simplat.c)
#ifndef SIMPLAT_H
#define SIMPLAT_H
#include <stdint.h>
extern uint32_t now;
extern int sim_late_ms;          // the singers come in this much later than the music
extern const char *sim_data;     // the data folder (singers, scores)
extern int sim_music_on;
extern int sim_volume;
extern int32_t (*sim_heard_ms)(void);
void sim_scan(const char *root);
// skill 0: in tune (an octave low), 1: a whole tone flat in the second half of every beat
void sim_sing(int p, uint32_t t, int skill);
#endif
