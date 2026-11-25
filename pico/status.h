// SingStar USB microphone test for PicoVerse 2350
// status.h - status window shared with the MSX test ROM
//
// The MSX reads the 128-byte status block at 7F00h-7F7Fh of the cartridge
// (page 1) and writes one-byte commands to 7F80h.
// Keep in sync with msx/singtest.asm.

#ifndef SINGSTAR_STATUS_H
#define SINGSTAR_STATUS_H

#include <stdint.h>

#define ST_WINDOW_BASE   0x7F00u
#define ST_WINDOW_SIZE   0x80u
#define ST_CMD_ADDR      0x7F80u

// Byte offsets in the status block
enum {
    ST_MAGIC0     = 0x00,  // 'S'
    ST_MAGIC1     = 0x01,  // 'S'
    ST_VERSION    = 0x02,
    ST_SEQ        = 0x03,  // bumps on every pitch update
    ST_STAGE      = 0x04,  // see stage_t
    ST_ERR_STEP   = 0x05,  // last failed setup step (setup_step_t), 0 = none
    ST_ERR_RESULT = 0x06,  // TinyUSB xfer_result_t of that step
    ST_FLAGS      = 0x07,  // see ST_FLAG_*
    ST_VID        = 0x08,  // u16
    ST_PID        = 0x0A,  // u16
    ST_RATE_SET   = 0x0C,  // u16, sample rate asked for
    ST_RATE_DEV   = 0x0E,  // u16, sample rate read back from the device (0 = unknown)
    ST_ALT        = 0x10,
    ST_CHANNELS   = 0x11,
    ST_BITS       = 0x12,
    ST_PKT_LEN    = 0x13,  // length of the last packet
    ST_PACKETS    = 0x14,  // u32, packets with data
    ST_LOST       = 0x18,  // u32, USB frames without a packet while streaming
    ST_SEQERR     = 0x1C,  // u16, data-sequence errors
    ST_KICKS      = 0x1E,  // u16, watchdog re-arms
    ST_VOL_CUR    = 0x20,  // s16, 1/256 dB
    ST_VOL_MIN    = 0x22,
    ST_VOL_MAX    = 0x24,
    ST_VOL_RES    = 0x26,
    ST_VOL_STATE  = 0x28,  // 0 unknown, 1 read ok, 2 not supported
    ST_CMD_ACK    = 0x29,  // bumps when a command was handled
    ST_RXTIMEOUT  = 0x2A,  // u16
    ST_PKT_PER_S  = 0x2C,  // u16
    ST_EMPTY      = 0x2E,  // u16, completed buffers without data
    ST_MIC1       = 0x30,  // mic block, see MIC_*
    ST_MIC2       = 0x40,
    ST_RAW        = 0x50,  // first 16 bytes of the last packet
    ST_ITF        = 0x60,  // AudioStreaming interface number
    ST_EP         = 0x61,  // endpoint address
    ST_EP_MPS     = 0x62,  // u16 wMaxPacketSize from the descriptor
    ST_FU_ID      = 0x64,  // feature unit id (0 = none)
    ST_DEVICES    = 0x65,  // mounted USB devices
};

// Mic block (16 bytes)
enum {
    MIC_METER   = 0,   // 0..30 bar length (-60..0 dBFS)
    MIC_PEAK    = 1,   // 0..30 peak hold
    MIC_VOICED  = 2,   // 1 = clear pitch found
    MIC_NOTE    = 3,   // MIDI note (60 = C4)
    MIC_CENTS   = 4,   // s8, -50..+50
    MIC_FREQ    = 5,   // u16 Hz
    MIC_CLARITY = 7,   // 0..99
    MIC_DBFS    = 8,   // s8 level in dBFS
};

#define ST_FLAG_PID_TOGGLE  0x01u
#define ST_FLAG_EP_INT      0x02u   // endpoint programmed as interrupt instead of iso
#define ST_FLAG_LAST        0x04u
#define ST_FLAG_RATE_8K     0x08u

typedef enum {
    STAGE_BOOT = 0,
    STAGE_WAIT_DEVICE,      // 1 nothing plugged in
    STAGE_NOT_AUDIO,        // 2 a device is mounted but has no microphone stream
    STAGE_FOUND,            // 3 microphone interface found
    STAGE_SETUP,            // 4 sending setup requests
    STAGE_STARTED,          // 5 stream armed, waiting for the first packet
    STAGE_STREAMING,        // 6 audio packets arriving
    STAGE_STALLED,          // 7 packets stopped (>100 ms)
    STAGE_ERROR,            // 8 a setup step failed, see ST_ERR_*
} stage_t;

typedef enum {
    STEP_NONE = 0,
    STEP_ALT0,          // 1 SET_INTERFACE alt 0
    STEP_VOL_MIN,       // 2
    STEP_VOL_MAX,       // 3
    STEP_VOL_RES,       // 4
    STEP_VOL_CUR,       // 5
    STEP_UNMUTE,        // 6
    STEP_ALT_ON,        // 7 SET_INTERFACE streaming alt
    STEP_RATE_SET,      // 8 SET_CUR sampling frequency
    STEP_RATE_GET,      // 9 GET_CUR sampling frequency
    STEP_EP_OPEN,       // 10 open endpoint
    STEP_ISO_START,     // 11 start stream
    STEP_VOL_SET,       // 12 gain change
} setup_step_t;

#endif
