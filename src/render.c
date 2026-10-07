#include "internal.h"

/* Each sample-and-hold output is a staircase whose steps fall on the 20 MHz
 * clock, not the host's. A step is drawn as a band-limited step centred on its
 * true time, so the images of the 26 kHz staircase stay where the hardware
 * puts them instead of folding back into the band. buf holds the samples
 * from the one about to be output to MD12_TAPS - 1 ahead of it. */

void md12_render_init(md12_render *r)
{
    unsigned j;
    for (j = 0; j < MD12_TAPS; j++)
        r->buf[j] = 0.0f;
    r->tail = 0.0f;
    r->held = 0.0f;
    r->pos = 0;
}

void md12_render_set(md12_render *r, float value, float frac)
{
    float d = value - r->held, x, a;
    const float *t0, *t1;
    unsigned j, p;

    if (d == 0.0f)
        return;
    r->held = value;
    r->tail = value;

    x = frac * (float)MD12_PHASES;
    p = (unsigned)x;
    if (p >= MD12_PHASES)
        p = MD12_PHASES - 1;
    a = x - (float)p;
    t0 = md12_step_tab + p * MD12_TAPS;
    t1 = t0 + MD12_TAPS;
    for (j = 0; j < MD12_TAPS; j++)
        r->buf[(r->pos + j) & (MD12_TAPS - 1)] += d * (t0[j] + a * (t1[j] - t0[j]));
}

float md12_render_out(md12_render *r)
{
    float y = r->buf[r->pos];
    r->buf[r->pos] = r->tail;
    r->pos = (r->pos + 1) & (MD12_TAPS - 1);
    return y;
}
