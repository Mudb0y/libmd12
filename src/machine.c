#include "internal.h"

static const md12_proto *const fixed_proto[4] = {
    &md12_proto_fixed3, &md12_proto_fixed4, &md12_proto_fixed5, &md12_proto_fixed6,
};

void md12_machine_init(md12_machine *m, const md12_calib *c, uint32_t host_rate)
{
    int ch;

    m->cal = *c;
    for (ch = 0; ch < 4; ch++)
        md12_iir_design(&m->fixed[ch], fixed_proto[ch], c->fixed_scale[ch], host_rate);
    md12_clock_init(&m->clock, host_rate);
    for (ch = 0; ch < MD12_CHANNELS; ch++) {
        m->voice[ch].active = 0;
        md12_render_init(&m->render[ch]);
    }
    m->plugged = 0;
    /* Each channel reaches the mix amplifier through its 1.0K jack resistor
     * and 22K; the mix volume is a 10K rheostat in the feedback (RT9), so
     * full volume is 10K / 23K. */
    m->mix_gain = 10.0f / 23.0f;
}

void md12_machine_trigger(md12_machine *m, int channel, const uint16_t *mem,
                          uint32_t length, int tune_index, int level_index)
{
    if (channel < 0 || channel >= MD12_CHANNELS)
        return;
    md12_voice_start(&m->voice[channel], mem, length, md12_tune_byte(tune_index),
                     level_index);
}

void md12_machine_set_plugged(md12_machine *m, uint8_t mask)
{
    m->plugged = mask;
}

static void strobe(md12_machine *m, int ch, float frac)
{
    md12_voice *v = &m->voice[ch];
    uint16_t code = md12_voice_tick(v);
    md12_render_set(&m->render[ch], md12_dac(code, v->level), frac);
}

void md12_machine_process(md12_machine *m, const md12_outputs *o, int n)
{
    md12_clock *k = &m->clock;
    int i, ch;

    for (i = 0; i < n; i++) {
        float mix = 0.0f;

        /* Every strobe due at or before this host sample, in time order. */
        while (k->whole < 0 || (k->whole == 0 && k->num == 0)) {
            float frac = k->whole < 0 ? 1.0f - (float)k->num * k->inv_den : 0.0f;
            strobe(m, k->slot, frac);
            k->num += k->slot_num;
            while (k->num >= k->den) {
                k->num -= k->den;
                k->whole++;
            }
            k->slot = (k->slot + 1) & (MD12_CHANNELS - 1);
        }
        k->whole--;

        for (ch = 0; ch < MD12_CHANNELS; ch++) {
            float raw = md12_render_out(&m->render[ch]);
            float filtered = ch >= 2 && ch < 6 ? md12_iir_run(&m->fixed[ch - 2], raw) : raw;

            if (ch < 6 && m->cal.split_jacks) {
                if (o->tip[ch])
                    o->tip[ch][i] = raw;
                if (o->ring[ch])
                    o->ring[ch][i] = filtered;
            } else {
                if (o->tip[ch])
                    o->tip[ch][i] = filtered;
                if (o->ring[ch])
                    o->ring[ch][i] = 0.0f;
            }
            if (!(m->plugged & (1u << ch)))
                mix += filtered;
        }
        /* The mix amplifier inverts: "Like all classic instruments in the
         * SP-12 family, the Mix output has reverse polarity" (Rossum
         * reissue quick-start guide). */
        if (o->mix)
            o->mix[i] = -mix * m->mix_gain;
    }
}
