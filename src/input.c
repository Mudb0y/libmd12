#include "internal.h"

#define HIST (2 * MD12_TAPS)

void md12_input_init(md12_input *in, const md12_calib *c, uint32_t host_rate,
                     uint32_t speed_num, uint32_t speed_den)
{
    uint64_t step = (uint64_t)speed_num * MD12_FRAME_CYCLES * host_rate;
    uint64_t den = (uint64_t)speed_den * MD12_CLOCK_HZ;
    uint64_t g = md12_gcd64(step, den);
    unsigned j;

    for (j = 0; j < HIST; j++)
        in->hist[j] = 0.0f;
    in->seen = 0;
    in->t_whole = 0;
    in->t_frac = 0;
    in->t_step = step / g;
    in->den = den / g;
    in->inv_den = 1.0f / (float)in->den;
    in->gain = 1.0f;
    in->offset = c->adc_offset;
}

void md12_input_set_gain(md12_input *in, float gain)
{
    in->gain = gain;
}

int md12_input_max_codes(const md12_input *in, int n)
{
    return (int)(((uint64_t)n * in->den) / in->t_step) + 2;
}

/* The successive-approximation loop keeps each bit whose trial voltage is
 * still below the input, so it lands on the code below: truncation, shifted
 * by the offset trim. */
static uint16_t convert(float v, float offset)
{
    float y = v * 2048.0f + (float)MD12_ZERO_CODE + offset;
    if (y < 0.0f)
        return 0;
    if (y >= 4095.0f)
        return 4095;
    return (uint16_t)y;
}

int md12_input_process(md12_input *in, const float *x, int n, uint16_t *codes,
                       int max)
{
    int i, out = 0;

    for (i = 0; i < n; i++) {
        int64_t s = in->seen++;
        in->hist[s & (HIST - 1)] = x[i] * in->gain;

        /* The hold takes the input at each sampling instant; the input is
         * band-limited host audio, so its value between host samples is
         * recovered by interpolation once the samples after it are in. */
        while (in->t_whole + MD12_TAPS / 2 <= s && out < max) {
            float xf = (float)in->t_frac * in->inv_den * (float)MD12_PHASES, a, acc = 0.0f;
            unsigned p = (unsigned)xf, j;
            int64_t base = in->t_whole - MD12_TAPS / 2 + 1;
            const float *t0, *t1;

            if (p >= MD12_PHASES)
                p = MD12_PHASES - 1;
            a = xf - (float)p;
            t0 = md12_sinc_tab + p * MD12_TAPS;
            t1 = t0 + MD12_TAPS;
            for (j = 0; j < MD12_TAPS; j++)
                acc += in->hist[(base + j) & (HIST - 1)] * (t0[j] + a * (t1[j] - t0[j]));
            codes[out++] = convert(acc, in->offset);

            in->t_frac += in->t_step;
            while (in->t_frac >= in->den) {
                in->t_frac -= in->den;
                in->t_whole++;
            }
        }
    }
    return out;
}
