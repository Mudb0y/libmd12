/*
 * libmd12hundwed: the E-mu SP-1200's audio path.
 *
 * Every structure here is owned by the caller and may live anywhere: the
 * library never allocates, locks or calls the operating system, and calls no
 * maths functions while audio runs, only in the init and set functions.
 * Audio is 32-bit float, one buffer per signal; any block size gives the same
 * output. 1.0 is the full scale of the converters.
 */
#ifndef MD12_H
#define MD12_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MD12_CHANNELS 8
#define MD12_TAPS 32
#define MD12_PHASES 64

/* The master clock: a 20 MHz crystal, divided by 768 for one pass over the
 * eight channels (service manual, schematic page 1 of 18). */
#define MD12_CLOCK_HZ 20000000u
#define MD12_FRAME_CYCLES 768u
#define MD12_SLOT_CYCLES 96u
#define MD12_TICK_HZ (20000000.0 / 768.0)

/* Samples are stored as the hardware stores them: 12-bit complement offset
 * binary, 0x000 most negative, 0x800 zero, 0xFFF most positive. */
#define MD12_ZERO_CODE 0x800

/* Tune index 16 plays at the recorded pitch; 0 to 31 are valid. */
#define MD12_TUNE_UNITY 16
#define MD12_LEVEL_MAX 31

/* E-mu's semitone table, the byte written to a channel's pitch register. */
uint8_t md12_tune_byte(int tune_index);

/* Samples advanced per frame by a pitch byte, over 128. keep_lsb is the
 * calibration's: the SP-1200 uses the byte's lowest bit, the SP-12 loses it. */
int md12_step128(uint8_t pitch_byte, int keep_lsb);

/* The OS's decay table: ticks between level steps, and periods held first. */
void md12_decay_pair(int decay_index, int *period, int *hold);

/* The OS's level for a MIDI note velocity. */
int md12_midi_level(int velocity);

/* The level PROM: a 5-bit level index to the 8-bit level DAC's code. */
uint8_t md12_level_code(int level_index);

/* ---- Calibration --------------------------------------------------------
 * Everything that differs between units or models. Each preset documents
 * where its numbers come from. */
typedef struct md12_calib {
    /* The converter's offset in steps, set by the RT1 trim: the code read
     * with no input is 0x800 plus its whole part. */
    float adc_offset;
    /* Outputs 1 to 6 carry the unfiltered signal on the tip and the
     * filtered one on the ring (SP-1200), or filtered on the tip (SP-12). */
    int split_jacks;
    /* Whether the pitch byte's lowest bit reaches the fraction adder. */
    int pitch_lsb;
    /* Outputs 3 to 6: each fixed filter's frequencies over the schematic's,
     * for parts that sit away from their nominal values. */
    float fixed_scale[4];
    /* Outputs 1 and 2: the SSM2044's pole frequency at rest, set by RT6 and
     * RT5, which are trimmed until the filter oscillates at this frequency. */
    float dyn_rest_hz[2];
    /* The SSM2044's thermal voltage, kT/q, which sets its volts per octave;
     * the chip has no temperature compensation. */
    float dyn_vt;
    /* The SSM2044's frequency control input as a resistance to ground, ohms,
     * which loads the pin 13 divider; 0 is an open input. */
    float dyn_pin13_ohms;
} md12_calib;

void md12_calib_sp1200(md12_calib *c);
void md12_calib_sp12(md12_calib *c);

/* ---- Voice: one channel of the playback controller ----------------------
 * Advanced once per frame. A voice reads 12-bit codes from caller memory. */
typedef struct md12_voice {
    const uint16_t *mem;
    uint32_t addr, end, loop;
    uint16_t step;
    uint8_t frac, level, active, looping;
} md12_voice;

/* step128 comes from md12_step128. */
void md12_voice_start(md12_voice *v, const uint16_t *mem, uint32_t length,
                      int step128, int level_index);
void md12_voice_set_loop(md12_voice *v, uint32_t loop_start);
void md12_voice_stop(md12_voice *v);
/* The code this voice sends to the DAC on this frame, then advances. */
uint16_t md12_voice_tick(md12_voice *v);

/* The DAC chain for one code at one level, in full-scale units. */
float md12_dac(uint16_t code, int level_index);

/* ---- Render: a channel's sample-and-hold staircase at the host rate -----
 * Steps are placed at their true time and band-limited; output is delayed
 * by MD12_TAPS / 2 host samples. */
typedef struct md12_render {
    float buf[MD12_TAPS];
    float tail, held;
    unsigned pos;
} md12_render;

void md12_render_init(md12_render *r);
/* A new hold value, frac host samples before the sample next produced. */
void md12_render_set(md12_render *r, float value, float frac);
float md12_render_out(md12_render *r);

/* ---- A fixed analogue filter at the host rate ---------------------------
 * The poles are the analogue poles mapped exactly; one short numerator is
 * fitted to the analogue response, phase included, up to 0.46 of the host
 * rate, and any notch below Nyquist is placed exactly. */
#define MD12_IIR_TAPS 16
#define MD12_IIR_SECTIONS 6
typedef struct md12_iir {
    float b[MD12_IIR_TAPS], x[MD12_IIR_TAPS];
    float a1[MD12_IIR_SECTIONS], a2[MD12_IIR_SECTIONS];
    float y1[MD12_IIR_SECTIONS], y2[MD12_IIR_SECTIONS];
    unsigned nb, ns, xpos;
} md12_iir;

/* ---- The dynamic filter of outputs 1 and 2 -------------------------------
 * The SSM2044 at zero resonance, its cutoff driven by the 10 uF capacitor
 * that a trigger discharges, and the output stage after it. */
#define MD12_DIODE_POINTS 128
typedef struct md12_dyn {
    float vc, vrest, decay, dt;
    int gate_low;
    float oct_per_volt, lr0;
    float v1[4], v2[4], y[4], t[4];
    float in_scale, out_scale;
    float diode[MD12_DIODE_POINTS];
    md12_iir out;
} md12_dyn;

/* ---- Clock: frames and channel slots against the host sample clock ------ */
typedef struct md12_clock {
    uint32_t den, slot_num;   /* in host samples, num / den */
    float inv_den;
    uint32_t num;             /* next strobe's fraction */
    int32_t whole;            /* next strobe's sample, relative to now */
    int slot;                 /* channel of the next strobe */
} md12_clock;

void md12_clock_init(md12_clock *k, uint32_t host_rate);

/* ---- Input: the sampling path -------------------------------------------
 * Host audio in, 12-bit codes out at the frame rate. speed is the 45 rpm
 * trick: the input is sampled as if played that many times faster. */
typedef struct md12_input {
    md12_iir aa;
    float hist[2 * MD12_TAPS];
    int64_t seen;            /* host samples received */
    int64_t t_whole;         /* next sampling instant, in host samples, */
    uint64_t t_frac;         /* plus t_frac / den */
    uint64_t t_step, den;
    float inv_den, gain, offset;
} md12_input;

void md12_input_init(md12_input *in, const md12_calib *c, uint32_t host_rate,
                     uint32_t speed_num, uint32_t speed_den);
void md12_input_set_gain(md12_input *in, float gain);
/* The most codes n host samples can produce. */
int md12_input_max_codes(const md12_input *in, int n);
/* Feeds n host samples and returns how many codes it wrote; codes must hold
 * md12_input_max_codes(in, n). Output lags input by MD12_TAPS / 2 samples. */
int md12_input_process(md12_input *in, const float *x, int n, uint16_t *codes,
                       int max);

/* ---- Machine: the eight channels, their outputs and the mix ------------- */
typedef struct md12_outputs {
    float *mix;
    float *tip[MD12_CHANNELS];
    float *ring[MD12_CHANNELS];
} md12_outputs;

/* What the operating system keeps per channel: the note waiting to start,
 * the decay, and the filter trigger. */
typedef struct md12_os_channel {
    const uint16_t *mem;
    uint32_t length, ticks;
    int step, level, period, hold;
    int mute_in, start_in;     /* frames until the old voice is muted, the new starts */
    int gate_ticks, gate_in;   /* ticks until the GAIN line is raised, then frames */
} md12_os_channel;

typedef struct md12_machine {
    md12_calib cal;
    md12_clock clock;
    md12_voice voice[MD12_CHANNELS];
    md12_os_channel os[MD12_CHANNELS];
    uint32_t frame;
    int default_decay;
    md12_render render[MD12_CHANNELS];
    md12_iir fixed[4];
    md12_dyn dyn[2];
    uint8_t plugged;
    float mix_gain;
} md12_machine;

/* Channel 0 is output 1. Notes are played as the operating system plays
 * them: a tuned sound decays at the default decay, which the OS sets to 31
 * (a 4.72 s hold, then a step every 39.3 ms); a decayed sound plays at
 * unity pitch. Levels are 0 to 31; the OS lowers a sound's level by 2 for
 * each step of its mix slider. */
void md12_machine_init(md12_machine *m, const md12_calib *c, uint32_t host_rate);
void md12_machine_trigger(md12_machine *m, int channel, const uint16_t *mem,
                          uint32_t length, int tune_index, int level_index);
void md12_machine_trigger_decay(md12_machine *m, int channel, const uint16_t *mem,
                                uint32_t length, int decay_index, int level_index);
void md12_machine_set_default_decay(md12_machine *m, int decay_index);
/* Channels with a plug in their jack leave the mix (bit n is channel n+1). */
void md12_machine_set_plugged(md12_machine *m, uint8_t mask);
void md12_machine_process(md12_machine *m, const md12_outputs *o, int n);

/* ---- Effect: live audio through one channel -----------------------------
 * The input samples into a short buffer and the playback controller reads it
 * back as it reads memory. A tune index other than unity is the 45 rpm trick:
 * the input is sampled as if sped up by exactly the ratio that tuning slows
 * it down by, so pitch and length are unchanged and only its artefacts are
 * heard. Changing the tune resets the input filter. */
enum { MD12_TAP_TIP, MD12_TAP_RING, MD12_TAP_MIX };

#define MD12_EFFECT_BUF 64

typedef struct md12_effect {
    md12_calib cal;
    md12_input in;
    md12_clock clock;
    md12_render render;
    md12_iir fixed;
    md12_dyn dyn;
    uint16_t buf[MD12_EFFECT_BUF];
    uint32_t written, addr, frame;
    uint32_t host_rate;
    int step, gate_ticks, gate_in;
    uint8_t frac, started;
    int channel, level, tap, tune;
    float mix_gain;
} md12_effect;

void md12_effect_init(md12_effect *e, const md12_calib *c, uint32_t host_rate);
/* channel 0 is output 1; tap is MD12_TAP_TIP, _RING or _MIX. */
void md12_effect_set(md12_effect *e, int channel, int tune_index, int level_index, int tap);
void md12_effect_set_gain(md12_effect *e, float gain);
/* Fires the filter of outputs 1 and 2, as a pad hit would. */
void md12_effect_trigger(md12_effect *e);
void md12_effect_process(md12_effect *e, const float *in, float *out, int n);
/* Delay from input to output, in host samples. */
int md12_effect_latency(const md12_effect *e);

#ifdef __cplusplus
}
#endif

#endif
