// PicoSing - sd_stubs.c
//
// The PicoVerse 2350 talks to its microSD card over SPI (hw_config.c), so the
// SD library's SDIO driver and crash recorder are left out of this build
// (with clang they clash with the SDK's intrinsics). These stand in for the
// few functions the rest of the library refers to; the SDIO ones are never
// called with an SPI card.

#include <stddef.h>
#include "pico/stdlib.h"
#include "hw_config.h"
#include "rp2040_sdio.h"

sdio_status_t rp2040_sdio_command_R1(sd_card_t *c, uint8_t cmd, uint32_t arg, uint32_t *resp)
{ (void)c; (void)cmd; (void)arg; (void)resp; return SDIO_ERR_RESPONSE_TIMEOUT; }
sdio_status_t rp2040_sdio_command_R2(const sd_card_t *c, uint8_t cmd, uint32_t arg, uint8_t *resp)
{ (void)c; (void)cmd; (void)arg; (void)resp; return SDIO_ERR_RESPONSE_TIMEOUT; }
sdio_status_t rp2040_sdio_command_R3(sd_card_t *c, uint8_t cmd, uint32_t arg, uint32_t *resp)
{ (void)c; (void)cmd; (void)arg; (void)resp; return SDIO_ERR_RESPONSE_TIMEOUT; }
sdio_status_t rp2040_sdio_rx_start(sd_card_t *c, uint8_t *b, uint32_t n, size_t bs)
{ (void)c; (void)b; (void)n; (void)bs; return SDIO_ERR_RESPONSE_TIMEOUT; }
sdio_status_t rp2040_sdio_rx_poll(sd_card_t *c, size_t w) { (void)c; (void)w; return SDIO_ERR_RESPONSE_TIMEOUT; }
sdio_status_t rp2040_sdio_tx_start(sd_card_t *c, const uint8_t *b, uint32_t n)
{ (void)c; (void)b; (void)n; return SDIO_ERR_RESPONSE_TIMEOUT; }
sdio_status_t rp2040_sdio_tx_poll(sd_card_t *c, uint32_t *done) { (void)c; (void)done; return SDIO_ERR_RESPONSE_TIMEOUT; }
bool rp2040_sdio_init(sd_card_t *c, float clk_div) { (void)c; (void)clk_div; return false; }
void sdio_irq_handler(sd_card_t *c) { (void)c; }

void capture_assert(const char *file, int line, const char *func, const char *pred)
{
    (void)file; (void)line; (void)func; (void)pred;
    panic("SD assert");
}

// picolibc's malloc (FatFs's long-file-name buffers) asks sbrk() for memory;
// the SDK only provides _sbrk() for newlib. Weak, so a newlib build keeps its own.
extern char end;            // linker: end of .bss, start of the heap
extern char __StackLimit;   // linker: end of the heap

__attribute__((weak)) void *sbrk(ptrdiff_t incr)
{
    static char *heap_end;
    if (!heap_end) heap_end = &end;
    char *prev = heap_end;
    if (heap_end + incr > &__StackLimit) return (void *)-1;
    heap_end += incr;
    return prev;
}
