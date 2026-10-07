#include "internal.h"

/* Codes held back before playback starts, against the jitter between the
 * input's sampling instants and the channel's strobes. */
#define MARGIN 4

static const md12_proto *const fixed_proto[4] = {
    &md12_proto_fixed3, &md12_proto_fixed4, &md12_proto_fixed5, &md12_proto_fixed6,
};

static void restart(md12_effect *e)
{
    uint8_t pb = md12_tune_byte(e->tune);
    float gain = e->in.gain;

    /* Sampled faster by 64 / step, played back slower by step / 64. */
    md12_input_init(&e->in, &e->cal, e->host_rate, 64u, (uint32_t)md12_step64(pb));
    e->in.gain = gain;
    e->pitch = pb;
    e->frac = 0;
    e->written = 0;
    e->addr = 0;
    e->started = 0;
}

void md12_effect_init(md12_effect *e, const md12_calib *c, uint32_t host_rate)
{
    e->cal = *c;
    e->host_rate = host_rate;
    e->in.gain = 1.0f;
    e->channel = 7;
    e->level = MD12_LEVEL_MAX;
    e->tap = MD12_TAP_TIP;
    e->tune = MD12_TUNE_UNITY;
    e->mix_gain = 10.0f / 23.0f;
    md12_clock_init(&e->clock, host_rate);
    md12_render_init(&e->render);
    md12_iir_design(&e->fixed, fixed_proto[0], c->fixed_scale[0], host_rate);
    md12_dyn_init(&e->dyn, c, 1, host_rate);
    restart(e);
}

void md12_effect_trigger(md12_effect *e)
{
    md12_dyn_trigger(&e->dyn);
}

void md12_effect_set(md12_effect *e, int channel, int tune_index, int level_index, int tap)
{
    if (channel < 0)
        channel = 0;
    if (channel >= MD12_CHANNELS)
        channel = MD12_CHANNELS - 1;
    if (tune_index < 0)
        tune_index = 0;
    if (tune_index > 31)
        tune_index = 31;
    if (channel != e->channel && channel >= 2 && channel < 6)
        md12_iir_design(&e->fixed, fixed_proto[channel - 2], e->cal.fixed_scale[channel - 2], e->host_rate);
    if (channel != e->channel && channel < 2)
        md12_dyn_init(&e->dyn, &e->cal, channel, e->host_rate);
    e->channel = channel;
    e->level = level_index < 0 ? 0 : level_index > MD12_LEVEL_MAX ? MD12_LEVEL_MAX : level_index;
    e->tap = tap;
    if (tune_index != e->tune) {
        e->tune = tune_index;
        restart(e);
    }
}

void md12_effect_set_gain(md12_effect *e, float gain)
{
    e->in.gain = gain;
}

int md12_effect_latency(const md12_effect *e)
{
    /* The input's interpolator, the codes held back, and the render stage. */
    double frames = MARGIN * 64.0 / md12_step64(e->pitch);
    return MD12_TAPS + (int)(frames * e->host_rate / MD12_TICK_HZ + 0.5);
}

/* One frame of the channel: the controller reads the buffer as memory. */
static uint16_t play(md12_effect *e)
{
    uint16_t code;
    unsigned sum;

    if (!e->started) {
        if (e->written < MARGIN + 1)
            return MD12_ZERO_CODE;
        e->started = 1;
        e->addr = e->written - MARGIN;
    }
    /* Rates match exactly, so neither guard fires in normal running. */
    if ((int32_t)(e->written - e->addr) <= 0)
        e->addr = e->written - 1;
    else if (e->written - e->addr > MD12_EFFECT_BUF - 2)
        e->addr = e->written - MARGIN;
    code = e->buf[e->addr & (MD12_EFFECT_BUF - 1)];
    sum = e->frac + (e->pitch & 0x7Eu) + 2u;
    e->frac = (uint8_t)(sum & 0x7Fu);
    e->addr += (e->pitch >> 7) + (sum >> 7);
    return code;
}

void md12_effect_process(md12_effect *e, const float *in, float *out, int n)
{
    md12_clock *k = &e->clock;
    int i;

    for (i = 0; i < n; i++) {
        uint16_t codes[4];
        int got = md12_input_process(&e->in, in + i, 1, codes, 4), j;
        float raw, y;

        for (j = 0; j < got; j++)
            e->buf[e->written++ & (MD12_EFFECT_BUF - 1)] = codes[j];

        while (k->whole < 0 || (k->whole == 0 && k->num == 0)) {
            if (k->slot == e->channel) {
                float frac = k->whole < 0 ? 1.0f - (float)k->num * k->inv_den : 0.0f;
                md12_render_set(&e->render, md12_dac(play(e), e->level), frac);
            }
            k->num += k->slot_num;
            while (k->num >= k->den) {
                k->num -= k->den;
                k->whole++;
            }
            k->slot = (k->slot + 1) & (MD12_CHANNELS - 1);
        }
        k->whole--;

        raw = md12_render_out(&e->render);
        y = e->channel < 2 ? md12_dyn_run(&e->dyn, raw)
          : e->channel < 6 ? md12_iir_run(&e->fixed, raw) : raw;
        if (e->tap == MD12_TAP_MIX)
            out[i] = -e->mix_gain * y;
        else if (e->channel < 6 && e->cal.split_jacks)
            out[i] = e->tap == MD12_TAP_RING ? y : raw;
        else
            out[i] = e->tap == MD12_TAP_TIP ? y : 0.0f;
    }
}
