The MP3 decoder (pico/pva/pva_decode.c with picomp3lib) on a PC, with stdio
standing in for FatFs:

P=<picomp3lib>/src
gcc -O1 -fsanitize=address,undefined -include stdlib.h -include mp3_alloc_dispatch.h \
    -I. -I../../pico/pva -I../../pico/audio -I$P -DMPDEC_ALLOCATOR=pv_mp3_alloc \
    sweep.c hal.c ../../pico/pva/pva_decode.c $P/*.c -o sweep
./sweep song.mp3

It seeks to every second of the song and decodes a few frames there.
Before v0.6 some MP3s crashed it (a negative memcpy in the decoder).
