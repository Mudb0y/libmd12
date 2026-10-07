#include "internal.h"

static const md12_proto *const fixed_proto[4] = {
    &md12_proto_fixed3, &md12_proto_fixed4, &md12_proto_fixed5, &md12_proto_fixed6,
};

/* From the OS's voice start, timed on its own code (firmware.md, question
 * 5): the filter's GAIN line falls first, the old voice is muted about
 * 0.15 ms later, and the new one runs from about 0.27 ms. */
#define MUTE_FRAMES 4
#define START_FRAMES 7

void md12_machine_init(md12_machine *m, const md12_calib *c, uint32_t host_rate)
{
    int ch;

    m->cal = *c;
    for (ch = 0; ch < 4; ch++)
        md12_iir_design(&m->fixed[ch], fixed_proto[ch], c->fixed_scale[ch], host_rate);
    md12_dyn_init(&m->dyn[0], c, 0, host_rate);
    md12_dyn_init(&m->dyn[1], c, 1, host_rate);
    md12_clock_init(&m->clock, host_rate);
    for (ch = 0; ch < MD12_CHANNELS; ch++) {
        m->voice[ch].active = 0;
        m->os[ch].mem = 0;
        m->os[ch].mute_in = m->os[ch].start_in = 0;
        m->os[ch].gate_ticks = m->os[ch].gate_in = 0;
        md12_render_init(&m->render[ch]);
    }
    m->frame = 0;
    /* Power-up and Clear All Mem set the default decay to 31. */
    m->default_decay = 31;
    m->plugged = 0;
    /* Each channel reaches the mix amplifier through its 1.0K jack resistor
     * and 22K; the mix volume is a 10K rheostat in the feedback (RT9), so
     * full volume is 10K / 23K. */
    m->mix_gain = 10.0f / 23.0f;
}

static void note(md12_machine *m, int channel, const uint16_t *mem, uint32_t length,
                 int tune_index, int decay_index, int level_index)
{
    md12_os_channel *os;

    if (channel < 0 || channel >= MD12_CHANNELS)
        return;
    os = &m->os[channel];
    os->mem = mem;
    os->length = length;
    os->step = md12_step128(md12_tune_byte(tune_index), m->cal.pitch_lsb);
    os->level = level_index < 0 ? 0 : level_index > MD12_LEVEL_MAX ? MD12_LEVEL_MAX : level_index;
    md12_decay_pair(decay_index, &os->period, &os->hold);
    os->mute_in = MUTE_FRAMES;
    os->start_in = START_FRAMES;
    /* Every note on channels 1 and 2 pulls its GAIN line low and restarts
     * the eight-tick count, even if the line is already low. */
    if (channel < 2) {
        m->dyn[channel].gate_low = 1;
        os->gate_ticks = MD12_GATE_TICKS;
        os->gate_in = 0;
    }
}

void md12_machine_trigger(md12_machine *m, int channel, const uint16_t *mem,
                          uint32_t length, int tune_index, int level_index)
{
    note(m, channel, mem, length, tune_index, m->default_decay, level_index);
}

/* One per-sound field holds either the tune or the decay, so a decayed
 * sound plays at table index 16, unity. */
void md12_machine_trigger_decay(md12_machine *m, int channel, const uint16_t *mem,
                                uint32_t length, int decay_index, int level_index)
{
    note(m, channel, mem, length, MD12_TUNE_UNITY, decay_index, level_index);
}

void md12_machine_set_default_decay(md12_machine *m, int decay_index)
{
    m->default_decay = decay_index < 0 ? 0 : decay_index > 31 ? 31 : decay_index;
}

void md12_machine_set_plugged(md12_machine *m, uint8_t mask)
{
    m->plugged = mask;
}

/* The start of a frame: the OS's pending writes, and on every 32nd frame
 * its housekeeping tick, which steps each decay and counts down the filter
 * triggers. */
static void frame(md12_machine *m)
{
    int ch, tick = (m->frame % MD12_TICK_FRAMES) == 0;

    for (ch = 0; ch < MD12_CHANNELS; ch++) {
        md12_os_channel *os = &m->os[ch];
        md12_voice *v = &m->voice[ch];

        if (os->mute_in && --os->mute_in == 0)
            v->level = 0;
        if (os->start_in && --os->start_in == 0) {
            md12_voice_start(v, os->mem, os->length, os->step, os->level);
            os->ticks = 0;
        }
        if (os->gate_in && --os->gate_in == 0)
            m->dyn[ch].gate_low = 0;
        if (!tick)
            continue;
        if (os->gate_ticks && --os->gate_ticks == 0)
            os->gate_in = MD12_GATE_LATE;
        /* Counting the first tick after the start as 1, the level drops by
         * one at every multiple of the period past the hold. */
        if (v->active && !os->start_in) {
            os->ticks++;
            if (os->ticks % (uint32_t)os->period == 0 &&
                os->ticks / (uint32_t)os->period > (uint32_t)os->hold && v->level > 0)
                v->level--;
        }
    }
    m->frame++;
}

static void strobe(md12_machine *m, int ch, float frac)
{
    md12_voice *v = &m->voice[ch];
    uint16_t code;

    if (ch == 0)
        frame(m);
    code = md12_voice_tick(v);
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
            float filtered = ch < 2 ? md12_dyn_run(&m->dyn[ch], raw)
                           : ch < 6 ? md12_iir_run(&m->fixed[ch - 2], raw) : raw;

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
