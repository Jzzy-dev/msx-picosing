// SingStar USB microphone test for PicoVerse 2350
// singstar_uac.c - USB Audio Class 1.0 microphone host driver (TinyUSB app driver)
//
// Flow once the dongle is plugged in:
//   1. TinyUSB enumerates it; open() parses the AudioControl and
//      AudioStreaming interfaces (uac_desc.c) and picks the stereo 16-bit
//      alternate setting.
//   2. ss_task() runs the setup chain: alt 0, read the volume range,
//      unmute, select the streaming alt, set the sample rate, open the
//      endpoint.
//   3. hcd_rp2_iso_start() (TinyUSB patch) receives every packet in the
//      USB interrupt and iso_rx() splits it into one ring per microphone.
//
// The SingStar dongle sends both mics as one stereo stream: left = first
// mic, right = second mic.

#include "tusb.h"
#include "host/usbh.h"
#include "host/usbh_pvt.h"
#include "portable/raspberrypi/rp2040/hcd_rp2_iso.h"
#include "pico/time.h"

#include "singstar_uac.h"
#include "uac_desc.h"
#include "status.h"

#include <string.h>

// UAC1 class requests
#define UAC_SET_CUR   0x01
#define UAC_GET_CUR   0x81
#define UAC_GET_MIN   0x82
#define UAC_GET_MAX   0x83
#define UAC_GET_RES   0x84
#define UAC_FU_MUTE   0x01
#define UAC_FU_VOLUME 0x02
#define UAC_EP_SAMPLING_FREQ 0x01

#define RT_CLASS_ITF_OUT 0x21
#define RT_CLASS_ITF_IN  0xA1
#define RT_CLASS_EP_OUT  0x22
#define RT_CLASS_EP_IN   0xA2

#define ISO_BUFLEN       64u     // one packet; 11025 Hz stereo = 44/48 bytes
#define WATCHDOG_MS      100u

// ---------------------------------------------------------------------
// State
// ---------------------------------------------------------------------
static volatile uint8_t *st;          // status block shared with the MSX

static uac_mic_t mic;
static uint8_t   mic_daddr;           // 0 = no microphone
static bool      start_pending;
static bool      chain_busy;
static bool      streaming;
static uint8_t   opt_flags;           // ST_FLAG_*
static uint32_t  rate;

static int16_t   vol_cur, vol_min, vol_max, vol_res;
static uint8_t   vol_state;
static int16_t   vol_target;
static uint8_t   vol_ch_next;

static tusb_control_request_t req;
static uint8_t   ctl_buf[8] __attribute__((aligned(4)));

// Rings and counters written in the USB IRQ
static int16_t   ring[2][SS_RING_SIZE];
static volatile uint32_t ring_wr;
static volatile uint32_t cnt_packets, cnt_lost, cnt_empty;
static volatile uint8_t  last_len;
static volatile uint16_t last_frame;
static volatile bool     have_frame;
static volatile uint8_t  raw_copy[16];
static volatile uint8_t  rx_channels;

// Task-side bookkeeping
static uint32_t wd_last_ms, wd_last_packets, pps_last_ms, pps_last_packets;
static uint16_t cnt_kicks;

// Command queue (single core: commands are passed in from the main loop)
static uint8_t cmd_q[16];
static uint8_t cmd_head, cmd_tail;

static void set_stage(stage_t s) { st[ST_STAGE] = (uint8_t)s; }

static void set_error(setup_step_t step, uint8_t result)
{
    st[ST_ERR_STEP] = (uint8_t)step;
    st[ST_ERR_RESULT] = result;
}

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

// ---------------------------------------------------------------------
// Isochronous receive (USB interrupt)
// ---------------------------------------------------------------------
static void __not_in_flash_func(iso_rx)(uint8_t const *data, uint16_t len, uint16_t frame)
{
    if (len == 0)
    {
        cnt_empty++;
        return;
    }

    if (have_frame)
    {
        uint16_t diff = (uint16_t)((frame - last_frame) & 0x7FFu);
        if (diff > 1u) cnt_lost += diff - 1u;
    }
    last_frame = frame;
    have_frame = true;

    uint32_t n = cnt_packets + 1u;
    cnt_packets = n;
    last_len = (uint8_t)len;
    if ((n & 31u) == 1u)
        for (int i = 0; i < 16; i++) raw_copy[i] = (i < len) ? data[i] : 0;

    uint32_t w = ring_wr;
    if (rx_channels == 2)
    {
        for (uint16_t i = 0; i + 3u < len; i += 4u)
        {
            ring[0][w & SS_RING_MASK] = (int16_t)(data[i]     | (data[i + 1] << 8));
            ring[1][w & SS_RING_MASK] = (int16_t)(data[i + 2] | (data[i + 3] << 8));
            w++;
        }
    }
    else
    {
        for (uint16_t i = 0; i + 1u < len; i += 2u)
        {
            int16_t s = (int16_t)(data[i] | (data[i + 1] << 8));
            ring[0][w & SS_RING_MASK] = s;
            ring[1][w & SS_RING_MASK] = s;
            w++;
        }
    }
    ring_wr = w;
}

// ---------------------------------------------------------------------
// Setup chain
// ---------------------------------------------------------------------
static void chain_step(setup_step_t done, xfer_result_t result);

static void ctl_cb(tuh_xfer_t *xfer)
{
    chain_step((setup_step_t)xfer->user_data, xfer->result);
}

static bool ctl(uint8_t bm, uint8_t breq, uint16_t wvalue, uint16_t windex, uint16_t wlen, setup_step_t step)
{
    req.bmRequestType = bm;
    req.bRequest = breq;
    req.wValue = tu_htole16(wvalue);
    req.wIndex = tu_htole16(windex);
    req.wLength = tu_htole16(wlen);
    tuh_xfer_t x = {
        .daddr = mic_daddr,
        .ep_addr = 0,
        .setup = &req,
        .buffer = wlen ? ctl_buf : NULL,
        .complete_cb = ctl_cb,
        .user_data = (uintptr_t)step,
    };
    return tuh_control_xfer(&x);
}

static uint16_t fu_windex(void) { return (uint16_t)((mic.fu_id << 8) | mic.ac_itf); }

static bool vol_request(uint8_t breq, setup_step_t step)
{
    return ctl(RT_CLASS_ITF_IN, breq, (uint16_t)((UAC_FU_VOLUME << 8) | mic.fu_vol_ch), fu_windex(), 2, step);
}

static void chain_fail(setup_step_t step, uint8_t result)
{
    set_error(step, result);
    set_stage(STAGE_ERROR);
    chain_busy = false;
}

// Issue the request for 'step'. Optional steps that do not apply move on.
static void chain_issue(setup_step_t step)
{
    bool ok = true;
    switch (step)
    {
        case STEP_ALT0:
            ok = tuh_interface_set(mic_daddr, mic.as_itf, 0, ctl_cb, STEP_ALT0);
            break;

        case STEP_VOL_MIN:
            if (!(mic.fu_id && mic.fu_volume)) { vol_state = 2; chain_issue(STEP_UNMUTE); return; }
            ok = vol_request(UAC_GET_MIN, STEP_VOL_MIN);
            break;
        case STEP_VOL_MAX: ok = vol_request(UAC_GET_MAX, STEP_VOL_MAX); break;
        case STEP_VOL_RES: ok = vol_request(UAC_GET_RES, STEP_VOL_RES); break;
        case STEP_VOL_CUR: ok = vol_request(UAC_GET_CUR, STEP_VOL_CUR); break;

        case STEP_UNMUTE:
            if (!(mic.fu_id && mic.fu_mute)) { chain_issue(STEP_ALT_ON); return; }
            ctl_buf[0] = 0;
            ok = ctl(RT_CLASS_ITF_OUT, UAC_SET_CUR, (uint16_t)(UAC_FU_MUTE << 8), fu_windex(), 1, STEP_UNMUTE);
            break;

        case STEP_ALT_ON:
            ok = tuh_interface_set(mic_daddr, mic.as_itf, mic.alt, ctl_cb, STEP_ALT_ON);
            break;

        case STEP_RATE_SET:
            if (!mic.ep_freq_ctrl) { chain_issue(STEP_EP_OPEN); return; }
            ctl_buf[0] = (uint8_t)rate; ctl_buf[1] = (uint8_t)(rate >> 8); ctl_buf[2] = (uint8_t)(rate >> 16);
            ok = ctl(RT_CLASS_EP_OUT, UAC_SET_CUR, (uint16_t)(UAC_EP_SAMPLING_FREQ << 8), mic.ep_addr, 3, STEP_RATE_SET);
            break;

        case STEP_RATE_GET:
            memset(ctl_buf, 0, 3);
            ok = ctl(RT_CLASS_EP_IN, UAC_GET_CUR, (uint16_t)(UAC_EP_SAMPLING_FREQ << 8), mic.ep_addr, 3, STEP_RATE_GET);
            break;

        case STEP_EP_OPEN:
        {
            // Packets at 11025 Hz stereo are 44-48 bytes; the hardware slot
            // has a 64-byte buffer, so open the endpoint as 64 bytes.
            tusb_desc_endpoint_t ep = {
                .bLength = sizeof(tusb_desc_endpoint_t),
                .bDescriptorType = TUSB_DESC_ENDPOINT,
                .bEndpointAddress = mic.ep_addr,
                .bmAttributes = { .xfer = (opt_flags & ST_FLAG_EP_INT) ? TUSB_XFER_INTERRUPT : TUSB_XFER_ISOCHRONOUS },
                .wMaxPacketSize = ISO_BUFLEN,
                .bInterval = mic.ep_interval ? mic.ep_interval : 1,
            };
            if (!tuh_edpt_open(mic_daddr, &ep)) { chain_fail(STEP_EP_OPEN, 0xFF); return; }
            chain_step(STEP_EP_OPEN, XFER_RESULT_SUCCESS);
            return;
        }

        case STEP_ISO_START:
        {
            uint8_t f = 0;
            if (opt_flags & ST_FLAG_PID_TOGGLE) f |= HCD_RP2_ISO_PID_TOGGLE;
            if (opt_flags & ST_FLAG_LAST)       f |= HCD_RP2_ISO_LAST;
            rx_channels = mic.channels;
            have_frame = false;
            if (!hcd_rp2_iso_start(mic_daddr, mic.ep_addr, ISO_BUFLEN, f, iso_rx)) { chain_fail(STEP_ISO_START, 0xFF); return; }
            streaming = true;
            chain_busy = false;
            wd_last_ms = now_ms();
            wd_last_packets = cnt_packets;
            set_stage(STAGE_STARTED);
            return;
        }

        case STEP_VOL_SET:
            ctl_buf[0] = (uint8_t)vol_target; ctl_buf[1] = (uint8_t)((uint16_t)vol_target >> 8);
            ok = ctl(RT_CLASS_ITF_OUT, UAC_SET_CUR, (uint16_t)((UAC_FU_VOLUME << 8) | vol_ch_next), fu_windex(), 2, STEP_VOL_SET);
            break;

        default:
            chain_busy = false;
            return;
    }

    if (!ok) chain_fail(step, 0xFE);   // could not even queue the request
}

static int16_t buf_s16(void) { return (int16_t)(ctl_buf[0] | (ctl_buf[1] << 8)); }

static void chain_step(setup_step_t done, xfer_result_t result)
{
    bool ok = (result == XFER_RESULT_SUCCESS);
    switch (done)
    {
        case STEP_ALT0:
            if (!ok) set_error(STEP_ALT0, (uint8_t)result);     // not fatal
            chain_issue(STEP_VOL_MIN);
            break;

        case STEP_VOL_MIN:
            if (!ok) { vol_state = 2; set_error(done, (uint8_t)result); chain_issue(STEP_UNMUTE); break; }
            vol_min = buf_s16(); chain_issue(STEP_VOL_MAX);
            break;
        case STEP_VOL_MAX:
            if (!ok) { vol_state = 2; set_error(done, (uint8_t)result); chain_issue(STEP_UNMUTE); break; }
            vol_max = buf_s16(); chain_issue(STEP_VOL_RES);
            break;
        case STEP_VOL_RES:
            vol_res = ok ? buf_s16() : 0;
            chain_issue(STEP_VOL_CUR);
            break;
        case STEP_VOL_CUR:
            if (ok) { vol_cur = buf_s16(); vol_state = 1; }
            else    { vol_state = 2; set_error(done, (uint8_t)result); }
            chain_issue(STEP_UNMUTE);
            break;

        case STEP_UNMUTE:
            if (!ok) set_error(done, (uint8_t)result);
            chain_issue(STEP_ALT_ON);
            break;

        case STEP_ALT_ON:
            if (!ok) { chain_fail(done, (uint8_t)result); break; }
            chain_issue(STEP_RATE_SET);
            break;

        case STEP_RATE_SET:
            if (!ok) set_error(done, (uint8_t)result);
            chain_issue(STEP_RATE_GET);
            break;

        case STEP_RATE_GET:
            if (ok) st_put16(st, ST_RATE_DEV, (uint16_t)(ctl_buf[0] | (ctl_buf[1] << 8)));
            chain_issue(STEP_EP_OPEN);
            break;

        case STEP_EP_OPEN:
            chain_issue(STEP_ISO_START);
            break;

        case STEP_VOL_SET:
            if (!ok) { set_error(done, (uint8_t)result); chain_busy = false; break; }
            vol_cur = vol_target;
            if (mic.fu_vol_ch == 1 && vol_ch_next == 1 && mic.channels == 2)
            {
                vol_ch_next = 2;
                chain_issue(STEP_VOL_SET);
            }
            else
            {
                chain_busy = false;
            }
            break;

        default:
            chain_busy = false;
            break;
    }
}

static void stream_stop(void)
{
    if (streaming) hcd_rp2_iso_stop();
    streaming = false;
    rx_channels = 0;
}

static void stream_start(void)
{
    if (!mic_daddr || !mic.has_stream || chain_busy) return;
    stream_stop();
    rate = (opt_flags & ST_FLAG_RATE_8K) ? 8000u : 11025u;
    if (!uac_rate_supported(&mic, rate) && mic.n_rates) rate = mic.rates[0];
    st_put16(st, ST_RATE_SET, (uint16_t)rate);
    st_put16(st, ST_RATE_DEV, 0);
    set_error(STEP_NONE, 0);
    set_stage(STAGE_SETUP);
    chain_busy = true;
    start_pending = false;
    chain_issue(STEP_ALT0);
}

// ---------------------------------------------------------------------
// TinyUSB application class driver
// ---------------------------------------------------------------------
static bool drv_init(void) { return true; }
static bool drv_deinit(void) { return true; }

// Does the configuration descriptor being enumerated contain a
// MIDIStreaming interface? Then the AudioControl interface belongs to a
// USB-MIDI device and is left for a MIDI driver.
static bool config_has_midi(void)
{
    const uint8_t *cfg = usbh_get_enum_buf();
    if (!cfg || cfg[1] != TUSB_DESC_CONFIGURATION) return false;
    uint16_t total = (uint16_t)(cfg[2] | (cfg[3] << 8));
    if (total > CFG_TUH_ENUMERATION_BUFSIZE) total = CFG_TUH_ENUMERATION_BUFSIZE;
    for (uint16_t i = 0; i + 8u < total && cfg[i] >= 2; i += cfg[i])
        if (cfg[i + 1] == TUSB_DESC_INTERFACE && cfg[i + 5] == TUSB_CLASS_AUDIO && cfg[i + 6] == 0x03)
            return true;
    return false;
}

static bool drv_open(uint8_t rhport, uint8_t daddr, tusb_desc_interface_t const *itf, uint16_t max_len)
{
    (void)rhport;
    if (itf->bInterfaceClass != TUSB_CLASS_AUDIO) return false;
    if (itf->bInterfaceSubClass != 0x01 && itf->bInterfaceSubClass != 0x02) return false;
    if (mic_daddr && mic_daddr != daddr) return false;         // one microphone device at a time
    if (itf->bInterfaceSubClass == 0x01 && config_has_midi()) return false;

    if (!mic_daddr)
    {
        memset(&mic, 0, sizeof(mic));
        mic_daddr = daddr;
    }

    uac_parse((const uint8_t *)itf, max_len, &mic);

    if (itf->bInterfaceSubClass == 0x02 && !mic.has_stream) return false;   // e.g. a speaker stream

    if (mic.has_stream)
    {
        st[ST_ITF]      = mic.as_itf;
        st[ST_EP]       = mic.ep_addr;
        st_put16(st, ST_EP_MPS, mic.ep_mps);
        st[ST_ALT]      = mic.alt;
        st[ST_CHANNELS] = mic.channels;
        st[ST_BITS]     = mic.bits;
        set_stage(STAGE_FOUND);
    }
    st[ST_FU_ID] = mic.fu_id;

    uint16_t vid = 0, pid = 0;
    if (tuh_vid_pid_get(daddr, &vid, &pid))
    {
        st_put16(st, ST_VID, vid);
        st_put16(st, ST_PID, pid);
    }
    return true;
}

static bool drv_set_config(uint8_t daddr, uint8_t itf_num)
{
    if (daddr == mic_daddr && mic.has_stream && itf_num == mic.as_itf)
        start_pending = true;
    usbh_driver_set_config_complete(daddr, itf_num);
    return true;
}

static bool drv_xfer_cb(uint8_t daddr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    (void)daddr; (void)ep_addr; (void)result; (void)xferred_bytes;
    return true;   // the stream bypasses the TinyUSB transfer path
}

static void drv_close(uint8_t daddr)
{
    if (daddr != mic_daddr) return;
    streaming = false;        // hcd_device_close() already dropped the stream
    rx_channels = 0;
    chain_busy = false;
    start_pending = false;
    mic_daddr = 0;
    memset(&mic, 0, sizeof(mic));
    vol_state = 0;
    set_stage(STAGE_WAIT_DEVICE);
}

static usbh_class_driver_t const ss_driver = {
    .name       = "SINGSTAR",
    .init       = drv_init,
    .deinit     = drv_deinit,
    .open       = drv_open,
    .set_config = drv_set_config,
    .xfer_cb    = drv_xfer_cb,
    .close      = drv_close,
};

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = 1;
    return &ss_driver;
}

// ---------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------
void ss_init(volatile uint8_t *status)
{
    st = status;
    set_stage(STAGE_WAIT_DEVICE);
    st[ST_FLAGS] = opt_flags;
}

uint32_t ss_samples_written(void) { return ring_wr; }
const int16_t *ss_ring(uint8_t ch) { return ring[ch & 1u]; }
uint32_t ss_sample_rate(void) { return rate; }
uint8_t ss_channels(void) { return streaming ? rx_channels : 0; }

void ss_command(uint8_t cmd)
{
    uint8_t next = (uint8_t)((cmd_head + 1u) & 15u);
    if (next == cmd_tail) return;
    cmd_q[cmd_head] = cmd;
    cmd_head = next;
}

static void gain_step(int dir)
{
    if (!streaming || chain_busy || vol_state != 1) return;
    int32_t step = (vol_res > 0 && vol_res > 256) ? vol_res : 256;   // 1 dB
    int32_t t = (int32_t)vol_cur + dir * step;
    if (t > vol_max) t = vol_max;
    if (t < vol_min) t = vol_min;
    vol_target = (int16_t)t;
    vol_ch_next = mic.fu_vol_ch;   // 0 = master, or start with channel 1
    chain_busy = true;
    chain_issue(STEP_VOL_SET);
}

static void handle_command(uint8_t c)
{
    switch (c)
    {
        case SS_CMD_RESTART:   stream_start(); break;
        case SS_CMD_GAIN_UP:   gain_step(+1); break;
        case SS_CMD_GAIN_DOWN: gain_step(-1); break;
        case SS_CMD_PID:       opt_flags ^= ST_FLAG_PID_TOGGLE; stream_start(); break;
        case SS_CMD_EPTYPE:    opt_flags ^= ST_FLAG_EP_INT;     stream_start(); break;
        case SS_CMD_LAST:      opt_flags ^= ST_FLAG_LAST;       stream_start(); break;
        case SS_CMD_RATE:      opt_flags ^= ST_FLAG_RATE_8K;    stream_start(); break;
        case SS_CMD_CLEAR:
            cnt_lost = 0; cnt_empty = 0; cnt_kicks = 0;
            hcd_rp2_iso_seq_errors = 0; hcd_rp2_iso_rx_timeouts = 0;
            break;
        default: return;
    }
    st[ST_FLAGS] = opt_flags;
    st[ST_CMD_ACK]++;
}

void ss_task(void)
{
    while (cmd_tail != cmd_head)
    {
        uint8_t c = cmd_q[cmd_tail];
        cmd_tail = (uint8_t)((cmd_tail + 1u) & 15u);
        handle_command(c);
    }

    if (start_pending && mic_daddr && tuh_mounted(mic_daddr) && !chain_busy)
        stream_start();

    uint32_t t = now_ms();

    if (streaming && (uint32_t)(t - wd_last_ms) >= WATCHDOG_MS)
    {
        uint32_t p = cnt_packets;
        if (p == wd_last_packets)
        {
            if (st[ST_STAGE] == STAGE_STREAMING) set_stage(STAGE_STALLED);
            if (hcd_rp2_iso_kick()) cnt_kicks++;
        }
        else if (st[ST_STAGE] == STAGE_STARTED || st[ST_STAGE] == STAGE_STALLED)
        {
            set_stage(STAGE_STREAMING);
        }
        wd_last_packets = p;
        wd_last_ms = t;
    }

    if ((uint32_t)(t - pps_last_ms) >= 1000u)
    {
        uint32_t p = cnt_packets;
        st_put16(st, ST_PKT_PER_S, (uint16_t)(p - pps_last_packets));
        pps_last_packets = p;
        pps_last_ms = t;
    }

    // Counters for the MSX screen
    st_put32(st, ST_PACKETS, cnt_packets);
    st_put32(st, ST_LOST, cnt_lost);
    st_put16(st, ST_SEQERR, (uint16_t)hcd_rp2_iso_seq_errors);
    st_put16(st, ST_KICKS, cnt_kicks);
    st_put16(st, ST_RXTIMEOUT, (uint16_t)hcd_rp2_iso_rx_timeouts);
    st_put16(st, ST_EMPTY, (uint16_t)cnt_empty);
    st[ST_PKT_LEN] = last_len;
    for (int i = 0; i < 16; i++) st[ST_RAW + i] = raw_copy[i];
    st_put16(st, ST_VOL_CUR, (uint16_t)vol_cur);
    st_put16(st, ST_VOL_MIN, (uint16_t)vol_min);
    st_put16(st, ST_VOL_MAX, (uint16_t)vol_max);
    st_put16(st, ST_VOL_RES, (uint16_t)vol_res);
    st[ST_VOL_STATE] = vol_state;
}
