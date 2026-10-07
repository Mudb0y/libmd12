#include <math.h>

#include "internal.h"

/* The cutoff control network, SK103 page 16 (tools/spice/env.cir): C111 is
 * charged toward +15 V through R135 and feeds pin 13 through R125, which also
 * sees R118 to ground, R112 to -15 V and R104 from the RT6 wiper; a trigger
 * pulls it down through R98 and D9 into the GAIN line. */
#define C111 10e-6
#define R135 30e3
#define R125 15e3
#define R118 390.0
#define R112 39e3
#define R104 220e3
#define RT6 10e3
#define R98 100.0
/* The 1N914 model of the netlist. */
#define D_IS 2.52e-9
#define D_N 1.752
#define D_RS 0.568
#define D_VT 0.025852
/* The diode table covers the capacitor from 0 V to this. */
#define DIODE_TOP 6.0

/* tanh to within 1e-7 for |x| < 1.5, and bounded beyond; a continued
 * fraction, so no maths library while audio runs. */
static float tanh_cf(float x)
{
    float x2;
    if (x > 4.97f)
        return 1.0f;
    if (x < -4.97f)
        return -1.0f;
    x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) /
           (135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f)));
}

/* Current through R98 and D9 with v across both: bisection on the diode's
 * own voltage, where the diode and the resistors must agree. */
static double diode_current(double v)
{
    double lo = 0.0, hi = v, vd = 0.0;
    int k;
    if (v <= 0.0)
        return 0.0;
    for (k = 0; k < 80; k++) {
        vd = 0.5 * (lo + hi);
        if (D_IS * (exp(vd / (D_N * D_VT)) - 1.0) > (v - vd) / (R98 + D_RS))
            hi = vd;
        else
            lo = vd;
    }
    return (v - vd) / (R98 + D_RS);
}

void md12_dyn_init(md12_dyn *d, const md12_calib *c, int channel, double fs)
{
    /* RT6 at its centre: the wiper's Thevenin source is 0 V behind 2.5K. */
    double g = 1.0 / R125 + 1.0 / R118 + 1.0 / R112 + 1.0 / (R104 + RT6 / 4.0) +
               (c->dyn_pin13_ohms > 0.0f ? 1.0 / c->dyn_pin13_ohms : 0.0);
    double alpha = (1.0 / R125) / g, beta = (-15.0 / R112) / g;
    double rth = 1.0 / (1.0 / R135 + (1.0 - alpha) / R125), cap = C111 * c->dyn_cap_scale[channel];
    int k;

    d->vrest = (float)((15.0 / R135 + beta / R125) * rth);
    d->vc = d->vrest;
    d->decay = (float)exp(-1.0 / (fs * rth * cap));
    d->inv_cap = (float)(1.0 / cap);
    d->dt = (float)(1.0 / fs);
    d->gate_low = 0;
    /* Pin 13 moves by alpha times the capacitor; the exponential converter
     * gives an octave per Vt ln 2, falling as the pin rises. */
    d->oct_per_volt = (float)(alpha / (c->dyn_vt * log(2.0)));
    d->lr0 = (float)(log(c->dyn_rest_hz[channel] / fs) / log(2.0));
    for (k = 0; k < MD12_DIODE_POINTS; k++)
        d->diode[k] = (float)diode_current(DIODE_TOP * k / (MD12_DIODE_POINTS - 1));
    for (k = 0; k < 4; k++)
        d->v1[k] = d->v2[k] = d->y[k] = d->t[k] = 0.0f;
    /* The hold's +-2.5 V full scale through 33K over 200 ohms reaches the
     * chip's differential pair, which compresses as tanh(v / 2Vt). The chip's
     * output current into the 15K stage is not specified, so the whole is
     * scaled to unity gain for small signals: an estimate. */
    d->in_scale = (float)(2.5 * 200.0 / 33200.0 / (2.0 * c->dyn_vt));
    d->out_scale = c->dyn_gain[channel] / d->in_scale;
    md12_iir_design(&d->out, &md12_proto_dynout, 1.0, fs);
}

/* The capacitor for one host sample. With the GAIN line high it relaxes to
 * rest along one time constant; held low, the diode's current is added and
 * the step is split in four. */
static void envelope(md12_dyn *d)
{
    int s;
    if (!d->gate_low) {
        d->vc = d->vrest + (d->vc - d->vrest) * d->decay;
        return;
    }
    for (s = 0; s < 4; s++) {
        float x = d->vc * ((MD12_DIODE_POINTS - 1) / (float)DIODE_TOP), a, i;
        int k;
        if (x < 0.0f)
            x = 0.0f;
        if (x > MD12_DIODE_POINTS - 1.001f)
            x = MD12_DIODE_POINTS - 1.001f;
        k = (int)x;
        a = x - (float)k;
        i = d->diode[k] + a * (d->diode[k + 1] - d->diode[k]);
        /* Relaxation toward rest is (vrest - vc) / (rth C); the diode adds
         * its current. */
        d->vc += 0.25f * d->dt * ((d->vrest - d->vc) * (1.0f - d->decay) / d->dt - i * d->inv_cap);
    }
}

float md12_dyn_run(md12_dyn *d, float x)
{
    float pos, a, u, out;
    const float *r0, *r1;
    float p, b0, b1, b2;
    int k, st;

    envelope(d);
    pos = (d->lr0 + d->oct_per_volt * (d->vrest - d->vc) - (float)MD12_LADDER_LO) * MD12_LADDER_STEPS;
    if (pos < 0.0f)
        pos = 0.0f;
    if (pos > MD12_LADDER_ROWS - 1.001f)
        pos = MD12_LADDER_ROWS - 1.001f;
    k = (int)pos;
    a = pos - (float)k;
    r0 = md12_ladder_tab[k];
    r1 = md12_ladder_tab[k + 1];
    p = r0[0] + a * (r1[0] - r0[0]);
    b0 = r0[1] + a * (r1[1] - r0[1]);
    b1 = r0[2] + a * (r1[2] - r0[2]);
    b2 = r0[3] + a * (r1[3] - r0[3]);

    /* Four matched stages, each dy/dt = w (tanh(in) - tanh(y)) as the patent's
     * ladder reduces to; the tanh(y) - y part is taken from the previous
     * sample, so each stage is a linear one-pole driven by the rest. */
    u = tanh_cf(x * d->in_scale);
    for (st = 0; st < 4; st++) {
        float v = u - (d->t[st] - d->y[st]);
        float y = b0 * v + b1 * d->v1[st] + b2 * d->v2[st] + p * d->y[st];
        if (y > -1e-20f && y < 1e-20f)
            y = 0.0f;
        d->v2[st] = d->v1[st];
        d->v1[st] = v;
        d->y[st] = y;
        d->t[st] = tanh_cf(y);
        u = d->t[st];
    }
    out = d->y[3] * d->out_scale;
    return md12_iir_run(&d->out, out);
}
