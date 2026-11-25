// mp3_alloc_dispatch.h - the Helix MP3 decoder's allocator hook (as in PicoAmp:
// picomp3lib's buffers.c is built with -DMPDEC_ALLOCATOR=pv_mp3_alloc and
// the decoder sets pv_mp3_alloc_fn to its arena allocator)
#ifndef MP3_ALLOC_DISPATCH_H
#define MP3_ALLOC_DISPATCH_H
#include <stddef.h>
extern void *(*pv_mp3_alloc_fn)(size_t size);
void *pv_mp3_alloc(size_t size);
#endif
