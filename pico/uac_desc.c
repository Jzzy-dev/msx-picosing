// SingStar USB microphone test for PicoVerse 2350
// uac_desc.c - USB Audio Class 1.0 descriptor parser for microphones

#include "uac_desc.h"
#include <string.h>

#define DESC_INTERFACE     0x04
#define DESC_ENDPOINT      0x05
#define DESC_CS_INTERFACE  0x24
#define DESC_CS_ENDPOINT   0x25

#define CLASS_AUDIO        0x01
#define SUB_CONTROL        0x01
#define SUB_STREAMING      0x02
#define SUB_MIDISTREAMING  0x03

// Candidate alt setting while walking one AudioStreaming interface
typedef struct {
    bool     valid_format;
    bool     pcm;
    uint8_t  channels, subframe, bits;
    bool     continuous;
    uint8_t  n_rates;
    uint32_t rates[UAC_MAX_RATES];
    bool     has_ep;
    uint8_t  ep_addr, ep_attr, ep_interval;
    uint16_t ep_mps;
    bool     freq_ctrl;
} alt_cand_t;

static int cand_score(const alt_cand_t *c)
{
    if (!c->valid_format || !c->pcm || !c->has_ep) return -1;
    if ((c->ep_addr & 0x80u) == 0) return -1;         // must be IN (microphone)
    if ((c->ep_attr & 0x03u) != 0x01u) return -1;     // must be isochronous
    if (c->subframe != 2 || c->bits != 16) return -1; // 16-bit samples only
    if (c->channels == 2) return 3;                   // stereo: both SingStar mics
    if (c->channels == 1) return 2;
    return -1;
}

static void cand_commit(const alt_cand_t *c, uint8_t itf, uint8_t alt, uac_mic_t *m, int *best)
{
    int s = cand_score(c);
    if (s <= *best) return;
    *best = s;
    m->has_stream      = true;
    m->as_itf          = itf;
    m->alt             = alt;
    m->channels        = c->channels;
    m->subframe        = c->subframe;
    m->bits            = c->bits;
    m->ep_addr         = c->ep_addr;
    m->ep_attr         = c->ep_attr;
    m->ep_mps          = c->ep_mps;
    m->ep_interval     = c->ep_interval;
    m->ep_freq_ctrl    = c->freq_ctrl;
    m->rate_continuous = c->continuous;
    m->n_rates         = c->n_rates;
    memcpy(m->rates, c->rates, sizeof(m->rates));
}

bool uac_parse(const uint8_t *desc, uint16_t len, uac_mic_t *m)
{
    const uint8_t *p = desc;
    const uint8_t *end = desc + len;
    bool found = false;

    uint8_t cur_itf = 0xFF, cur_alt = 0, cur_class = 0, cur_sub = 0;
    alt_cand_t cand;
    bool in_cand = false;
    int best = m->has_stream ? 1 : 0;   // keep an earlier choice unless beaten

    memset(&cand, 0, sizeof(cand));

    while (p + 2 <= end)
    {
        uint8_t dlen = p[0];
        uint8_t dtype = p[1];
        if (dlen < 2 || p + dlen > end) break;

        if (dtype == DESC_INTERFACE && dlen >= 9)
        {
            if (in_cand) cand_commit(&cand, cur_itf, cur_alt, m, &best);
            in_cand = false;

            cur_itf   = p[2];
            cur_alt   = p[3];
            cur_class = p[5];
            cur_sub   = p[6];

            if (cur_class == CLASS_AUDIO && cur_sub == SUB_CONTROL)
            {
                m->has_ac = true;
                m->ac_itf = cur_itf;
                found = true;
            }
            else if (cur_class == CLASS_AUDIO && cur_sub == SUB_MIDISTREAMING)
            {
                m->is_midi = true;
            }
            else if (cur_class == CLASS_AUDIO && cur_sub == SUB_STREAMING)
            {
                found = true;
                memset(&cand, 0, sizeof(cand));
                in_cand = true;
            }
        }
        else if (dtype == DESC_CS_INTERFACE && dlen >= 3 && cur_class == CLASS_AUDIO)
        {
            uint8_t subtype = p[2];
            if (cur_sub == SUB_CONTROL && subtype == 0x06 && dlen >= 7)
            {
                // FEATURE_UNIT: bUnitID, bSourceID, bControlSize, bmaControls[0..]
                uint8_t csize = p[5];
                if (csize >= 1 && m->fu_id == 0)
                {
                    m->fu_id = p[3];
                    uint8_t master = p[6];
                    uint8_t ch1 = (dlen >= 7u + csize) ? p[6 + csize] : 0;
                    m->fu_mute = (master & 0x01u) != 0;
                    if (ch1 & 0x02u)        { m->fu_volume = true; m->fu_vol_ch = 1; }
                    else if (master & 0x02u){ m->fu_volume = true; m->fu_vol_ch = 0; }
                }
            }
            else if (cur_sub == SUB_STREAMING && in_cand)
            {
                if (subtype == 0x01 && dlen >= 7)          // AS_GENERAL
                {
                    uint16_t tag = (uint16_t)(p[5] | (p[6] << 8));
                    cand.pcm = (tag == 0x0001);
                }
                else if (subtype == 0x02 && dlen >= 8 && p[3] == 0x01)  // FORMAT_TYPE I
                {
                    cand.valid_format = true;
                    cand.channels = p[4];
                    cand.subframe = p[5];
                    cand.bits     = p[6];
                    uint8_t nf = p[7];
                    const uint8_t *f = p + 8;
                    if (nf == 0)
                    {
                        cand.continuous = true;
                        if (dlen >= 14)
                        {
                            cand.rates[0] = (uint32_t)f[0] | ((uint32_t)f[1] << 8) | ((uint32_t)f[2] << 16);
                            cand.rates[1] = (uint32_t)f[3] | ((uint32_t)f[4] << 8) | ((uint32_t)f[5] << 16);
                            cand.n_rates = 2;
                        }
                    }
                    else
                    {
                        for (uint8_t i = 0; i < nf && cand.n_rates < UAC_MAX_RATES; i++)
                        {
                            if (8u + 3u * i + 3u > dlen) break;
                            cand.rates[cand.n_rates++] =
                                (uint32_t)f[3 * i] | ((uint32_t)f[3 * i + 1] << 8) | ((uint32_t)f[3 * i + 2] << 16);
                        }
                    }
                }
            }
        }
        else if (dtype == DESC_ENDPOINT && dlen >= 7 && in_cand && !cand.has_ep)
        {
            cand.has_ep      = true;
            cand.ep_addr     = p[2];
            cand.ep_attr     = p[3];
            cand.ep_mps      = (uint16_t)((p[4] | (p[5] << 8)) & 0x07FF);
            cand.ep_interval = p[6];
        }
        else if (dtype == DESC_CS_ENDPOINT && dlen >= 4 && in_cand && p[2] == 0x01)
        {
            cand.freq_ctrl = (p[3] & 0x01u) != 0;
        }

        p += dlen;
    }

    if (in_cand) cand_commit(&cand, cur_itf, cur_alt, m, &best);
    return found;
}

bool uac_rate_supported(const uac_mic_t *m, uint32_t rate)
{
    if (!m->has_stream) return false;
    if (m->rate_continuous)
        return m->n_rates == 2 && rate >= m->rates[0] && rate <= m->rates[1];
    for (uint8_t i = 0; i < m->n_rates; i++)
        if (m->rates[i] == rate) return true;
    return false;
}
