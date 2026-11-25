// PicoSing - karaoke for the MSX PicoVerse 2350
// scratch.c - see scratch.h
#include "scratch.h"
uint8_t g_scratch[SCRATCH_SIZE] __attribute__((aligned(4)));
static int owner_now;
void *scratch(int owner) { owner_now = owner; return g_scratch; }
bool scratch_mine(int owner) { return owner_now == owner; }
