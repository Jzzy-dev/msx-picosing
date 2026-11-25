#pragma once
#include <stdio.h>
#include <stdint.h>
typedef unsigned int UINT; typedef uint8_t BYTE; typedef uint32_t FSIZE_t;
typedef enum { FR_OK = 0, FR_DISK_ERR, FR_NO_FILE = 4, FR_NO_PATH = 5, FR_INVALID_NAME = 6 } FRESULT;
typedef struct { FILE *f; uint32_t size; } FIL;
#define FA_READ 1
static inline FRESULT f_open(FIL *fp, const char *p, BYTE m) { fp->f = fopen(p, "rb"); if (!fp->f) return FR_NO_FILE; fseek(fp->f, 0, SEEK_END); fp->size = ftell(fp->f); fseek(fp->f, 0, SEEK_SET); return FR_OK; }
static inline FRESULT f_close(FIL *fp) { fclose(fp->f); return FR_OK; }
static inline FRESULT f_read(FIL *fp, void *b, UINT n, UINT *br) { *br = fread(b, 1, n, fp->f); return FR_OK; }
static inline FRESULT f_lseek(FIL *fp, FSIZE_t o) { fseek(fp->f, o, SEEK_SET); return FR_OK; }
#define f_size(fp) ((fp)->size)
#define f_tell(fp) ((uint32_t)ftell((fp)->f))
