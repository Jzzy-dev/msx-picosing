// PicoSing - decoder test: seek to every second of an MP3 and decode a little
// (the false-frame-sync crash of v0.5). Build: see tests/decoder/README.txt
#include <stdio.h>
#include "pva_internal.h"
int main(int argc, char **argv)
{
    pva_dec_init(); pva_track_info_t info; int bad = 0;
    for (int s = 0; s < 212; s++)
    {
        if (pva_dec_open(argv[1], &info) != 0) { printf("open fail\n"); return 1; }
        pva_dec_seek(s);
        int got = 0;
        for (int i = 0; i < 60; i++) { int r = pva_dec_step(); if (r < 0) break; if (r > 0) got++; }
        if (got < 20 && s < 205) { bad++; printf("seek %d: only %d frames\n", s, got); }
        pva_dec_close();
    }
    printf("sweep done, %d bad\n", bad);
}
