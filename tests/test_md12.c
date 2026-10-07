#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md12.h"
#include "../src/internal.h"

static int failures, checks;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        checks++;                                                             \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/* ---- spectrum helpers --------------------------------------------------- */

static void fft(double *re, double *im, int n)
{
    int i, j, len;
    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double a = -2.0 * M_PI / len;
        for (i = 0; i < n; i += len)
            for (j = 0; j < len / 2; j++) {
                double wr = cos(a * j), wi = sin(a * j);
                double xr = re[i + j + len / 2] * wr - im[i + j + len / 2] * wi;
                double xi = re[i + j + len / 2] * wi + im[i + j + len / 2] * wr;
                re[i + j + len / 2] = re[i + j] - xr;
                im[i + j + len / 2] = im[i + j] - xi;
                re[i + j] += xr;
                im[i + j] += xi;
            }
    }
}

/* Magnitude spectrum of x (Blackman-Harris window), scaled so a full-scale
 * sine reads 1.0. */
static double *spectrum(const float *x, int n)
{
    double *re = calloc((size_t)n, sizeof *re), *im = calloc((size_t)n, sizeof *im);
    double *mag = calloc((size_t)n / 2, sizeof *mag), wsum = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        double t = 2.0 * M_PI * i / n;
        double w = 0.35875 - 0.48829 * cos(t) + 0.14128 * cos(2 * t) - 0.01168 * cos(3 * t);
        re[i] = x[i] * w;
        wsum += w;
    }
    fft(re, im, n);
    for (i = 0; i < n / 2; i++)
        mag[i] = 2.0 * sqrt(re[i] * re[i] + im[i] * im[i]) / wsum;
    free(re);
    free(im);
    return mag;
}

/* Amplitude of the component at exactly f, Blackman-Harris weighted, so it
 * does not depend on where f falls between bins. */
static double tone_amp(const float *x, int n, double fs, double f)
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        double t = 2.0 * M_PI * i / n;
        double w = 0.35875 - 0.48829 * cos(t) + 0.14128 * cos(2 * t) - 0.01168 * cos(3 * t);
        re += w * x[i] * cos(2.0 * M_PI * f * i / fs);
        im -= w * x[i] * sin(2.0 * M_PI * f * i / fs);
        wsum += w;
    }
    return 2.0 * sqrt(re * re + im * im) / wsum;
}

static double db(double x) { return 20.0 * log10(x + 1e-30); }

/* ---- the digital half --------------------------------------------------- */

static void test_tables(void)
{
    int i;
    for (i = 0; i < 32; i++) {
        long want = lround(128.0 * pow(2.0, (i - 16) / 12.0)) - 1;
        if (want > 255)
            want = 255;
        CHECK(md12_tune_byte(i) == want, "tune %d: %d, want %ld", i, md12_tune_byte(i), want);
        CHECK(md12_step64(md12_tune_byte(i)) == md12_tune_byte(i) / 2 + 1, "step %d", i);
    }
    CHECK(md12_step64(md12_tune_byte(16)) == 64, "unity tune is one sample per frame");
    CHECK(md12_step64(md12_tune_byte(23)) == 96, "plus 7 is exactly 1.5");
    CHECK(md12_step64(md12_tune_byte(8)) == 41, "minus 8 is 41/64");
    CHECK(md12_tune_byte(-5) == 50 && md12_tune_byte(99) == 255, "tune clamps");
    CHECK(md12_level_code(0) == 0 && md12_level_code(31) == 255 && md12_level_code(16) == 27, "level PROM");
    CHECK(md12_dac(0x800, 31) == 0.0f, "0x800 is zero");
    CHECK(fabsf(md12_dac(0xFFF, 31) - (2047.0f / 2048.0f) * (255.0f / 256.0f)) < 1e-7f, "top code");
    CHECK(md12_dac(0x000, 31) == -255.0f / 256.0f, "bottom code");
    CHECK(md12_dac(0xFFF, 0) == 0.0f, "level 0 mutes");
}

static void test_voice(void)
{
    static uint16_t ramp[4096];
    int t, i;
    for (i = 0; i < 4096; i++)
        ramp[i] = (uint16_t)i;

    for (t = 0; t < 32; t++) {
        md12_voice v;
        uint8_t pb = md12_tune_byte(t);
        int step = md12_step64(pb), ok = 1, prev = -1;
        md12_voice_start(&v, ramp, 4096, pb, 31);
        for (i = 0; i < 64 * 10; i++) {
            int code = md12_voice_tick(&v);
            int adv = prev < 0 ? 0 : code - prev;
            if (prev >= 0 && (adv < 0 || adv > 2))
                ok = 0;
            prev = code;
        }
        /* After 640 frames the voice has read exactly 10 * step samples. */
        CHECK(ok, "tune %d moves 0, 1 or 2 samples a frame", t);
        CHECK(v.addr == (uint32_t)(10 * step), "tune %d: addr %u, want %d", t, v.addr, 10 * step);
    }

    {
        md12_voice v;
        md12_voice_start(&v, ramp, 3, md12_tune_byte(16), 31);
        CHECK(md12_voice_tick(&v) == 0 && md12_voice_tick(&v) == 1 && md12_voice_tick(&v) == 2,
              "unity plays every sample");
        CHECK(!v.active && md12_voice_tick(&v) == 0x800, "voice ends and outputs zero");
    }
    {
        md12_voice v;
        int a, b, c, d;
        md12_voice_start(&v, ramp, 4, md12_tune_byte(16), 31);
        md12_voice_set_loop(&v, 2);
        a = md12_voice_tick(&v); b = md12_voice_tick(&v);
        c = md12_voice_tick(&v); d = md12_voice_tick(&v);
        CHECK(a == 0 && b == 1 && c == 2 && d == 3 && md12_voice_tick(&v) == 2 &&
              md12_voice_tick(&v) == 3 && v.active, "loop");
    }
}

/* ---- the sampling input ------------------------------------------------- */

static void test_input(void)
{
    md12_calib cal;
    md12_input in;
    float x[4800];
    uint16_t codes[4800];
    int i, n, ok = 1;

    md12_calib_sp1200(&cal);
    md12_input_init(&in, &cal, 48000, 1, 1);
    for (i = 0; i < 4800; i++)
        x[i] = 0.0f;
    n = md12_input_process(&in, x, 4800, codes, md12_input_max_codes(&in, 4800));
    for (i = 0; i < n; i++)
        if (codes[i] != 0x804)
            ok = 0;
    CHECK(ok, "silence reads 0x804 on the SP-1200 trim");
    /* 4800 samples at 48 kHz are 0.1 s: 2604 frames, less the 16-sample
     * interpolation lag (8.68 frames). */
    CHECK(n == 2596, "frames in 0.1 s: %d", n);

    md12_input_init(&in, &cal, 48000, 1, 1);
    for (i = 0; i < 4800; i++)
        x[i] = 0.25f;
    n = md12_input_process(&in, x, 4800, codes, md12_input_max_codes(&in, 4800));
    /* 0.25 * 2048 + 2048 + 4.5 = 2564.5, truncated. */
    CHECK(codes[n - 1] == 2564, "DC truncates: %d", codes[n - 1]);

    md12_input_init(&in, &cal, 48000, 1, 1);
    for (i = 0; i < 4800; i++)
        x[i] = -2.0f;
    n = md12_input_process(&in, x, 4800, codes, md12_input_max_codes(&in, 4800));
    CHECK(codes[n - 1] == 0, "clips at the bottom");

    /* The 45 rpm trick at 4/3: three samples for every four frames. */
    md12_input_init(&in, &cal, 48000, 4, 3);
    n = md12_input_process(&in, x, 4800, codes, md12_input_max_codes(&in, 4800));
    CHECK(abs(n - (int)((4800 - 16) * 26041.6667 / 48000.0 * 0.75)) <= 1, "sped-up sampling count %d", n);
}

/* ---- the clock and the render stage ------------------------------------- */

static void test_render_step(void)
{
    md12_render r;
    float y[64];
    int i;
    md12_render_init(&r);
    md12_render_set(&r, 1.0f, 0.0f);
    for (i = 0; i < 64; i++)
        y[i] = md12_render_out(&r);
    /* A step exactly on a sample is half way there at that sample, which
     * comes out MD12_TAPS / 2 samples later. */
    CHECK(fabsf(y[MD12_TAPS / 2] - 0.5f) < 1e-6f, "step centre %f", y[MD12_TAPS / 2]);
    CHECK(fabsf(y[0]) < 1e-4f && y[63] == 1.0f, "step settles: %g %g", y[0], y[63]);
}

enum { SPEC_N = 65536, SPEC_LEN = 80000 };

/* A full-scale-ish sine at f0, sampled at the machine's rate, played on
 * channel 8 at unity and rendered at 48 kHz. */
static float *render_tone(double f0)
{
    static uint16_t sine[SPEC_LEN];
    static float skip[4096];
    float *out = malloc(SPEC_N * sizeof *out);
    md12_calib cal;
    md12_machine m;
    md12_outputs o;
    int i;

    for (i = 0; i < SPEC_LEN; i++)
        sine[i] = (uint16_t)(2048 + (int)floor(2000.0 * sin(2.0 * M_PI * f0 * i / MD12_TICK_HZ) + 0.5));
    md12_calib_sp1200(&cal);
    md12_machine_init(&m, &cal, 48000);
    memset(&o, 0, sizeof o);
    md12_machine_trigger(&m, 7, sine, SPEC_LEN, MD12_TUNE_UNITY, 31);
    o.tip[7] = skip;
    md12_machine_process(&m, &o, 4096);
    o.tip[7] = out;
    md12_machine_process(&m, &o, SPEC_N);
    return out;
}

static double zoh(double f)
{
    double x = M_PI * f / MD12_TICK_HZ;
    return fabs(sin(x) / x);
}

static void test_machine_spectrum(void)
{
    const double tick = MD12_TICK_HZ, amp = 2000.0 / 2048.0 * 255.0 / 256.0;
    double *mag, worst = 0.0, worst_f = 0.0, fund, img, f0;
    float *out;
    int k;

    /* Channel 8 is the raw hold: the fundamental carries the hold's sinc
     * droop, and its image below 20 kHz the same sinc at the image. */
    f0 = 7000.0;
    out = render_tone(f0);
    fund = tone_amp(out, SPEC_N, 48000, f0);
    img = tone_amp(out, SPEC_N, 48000, tick - f0);
    CHECK(fabs(db(fund) - db(amp * zoh(f0))) < 0.02, "fundamental %.3f dB, want %.3f",
          db(fund), db(amp * zoh(f0)));
    CHECK(fabs(db(img) - db(amp * zoh(tick - f0))) < 0.02, "image %.3f dB, want %.3f",
          db(img), db(amp * zoh(tick - f0)));
    free(out);

    f0 = 3000.0;
    out = render_tone(f0);
    fund = tone_amp(out, SPEC_N, 48000, f0);
    mag = spectrum(out, SPEC_N);
    for (k = 20; k < SPEC_N / 2; k++) {
        double f = (double)k * 48000 / SPEC_N;
        if (fabs(f - f0) < 30 || fabs(f - (tick - f0)) < 30 || f > 20000)
            continue;
        if (mag[k] > worst) {
            worst = mag[k];
            worst_f = f;
        }
    }
    /* 12-bit quantisation of this sine leaves spurs near -90 dB; anything
     * folded back by the render stage would show above that. */
    CHECK(db(worst / fund) < -85.0, "worst spur below 20 kHz %.1f dB at %.0f Hz", db(worst / fund), worst_f);
    free(mag);
    free(out);
}

static void test_block_invariance(void)
{
    enum { LEN = 20000, N = 9000 };
    static uint16_t snd[LEN];
    static float a[N], b[N];
    static const int sizes[] = {1, 7, 64, 1000};
    md12_calib cal;
    md12_machine m;
    md12_outputs o;
    int i, s, same;

    for (i = 0; i < LEN; i++)
        snd[i] = (uint16_t)((i * 2654435761u) >> 20);
    md12_calib_sp1200(&cal);
    memset(&o, 0, sizeof o);

    md12_machine_init(&m, &cal, 44100);
    md12_machine_trigger(&m, 2, snd, LEN, 9, 31);
    o.mix = a;
    md12_machine_process(&m, &o, N);

    for (s = 0; s < 4; s++) {
        md12_machine_init(&m, &cal, 44100);
        md12_machine_trigger(&m, 2, snd, LEN, 9, 31);
        for (i = 0; i < N; i += sizes[s]) {
            int len = N - i < sizes[s] ? N - i : sizes[s];
            o.mix = b + i;
            md12_machine_process(&m, &o, len);
        }
        same = memcmp(a, b, sizeof a) == 0;
        CHECK(same, "block size %d changes the output", sizes[s]);
    }
}

static void clock_run(md12_clock *k, int samples, int *frames)
{
    int i;
    for (i = 0; i < samples; i++) {
        while (k->whole < 0 || (k->whole == 0 && k->num == 0)) {
            if (k->slot == 0)
                (*frames)++;
            k->num += k->slot_num;
            while (k->num >= k->den) {
                k->num -= k->den;
                k->whole++;
            }
            k->slot = (k->slot + 1) & 7;
        }
        k->whole--;
    }
}

static void test_clock(void)
{
    md12_clock k, first;
    int frames = 0;
    md12_clock_init(&k, 48000);
    /* 1152 host samples at 48 kHz are exactly 625 frames, so the strobe
     * pattern repeats with that period. */
    clock_run(&k, 1152, &frames);
    first = k;
    frames = 0;
    clock_run(&k, 1152, &frames);
    CHECK(frames == 625 && k.slot == first.slot && k.num == first.num && k.whole == first.whole,
          "625 frames per 1152 samples: %d", frames);
}

/* ---- the analogue filters against ngspice -------------------------------- */

typedef struct { double f, db; } point;

static int load_csv(const char *name, point *pts, int max)
{
    char path[256], line[256];
    FILE *fp;
    int n = 0;
    snprintf(path, sizeof path, "tests/data/spice_%s.csv", name);
    fp = fopen(path, "r");
    if (!fp)
        return 0;
    if (!fgets(line, sizeof line, fp)) {
        fclose(fp);
        return 0;
    }
    while (n < max && fgets(line, sizeof line, fp)) {
        double ph;
        if (sscanf(line, "%lf,%lf,%lf", &pts[n].f, &pts[n].db, &ph) == 3)
            n++;
    }
    fclose(fp);
    return n;
}

static double iir_db(const md12_iir *f, double fs, double hz)
{
    double w = 2.0 * M_PI * hz / fs, re = 0.0, im = 0.0, mag;
    unsigned k, s;
    for (k = 0; k < f->nb; k++) {
        re += f->b[k] * cos(w * k);
        im -= f->b[k] * sin(w * k);
    }
    mag = sqrt(re * re + im * im);
    for (s = 0; s < f->ns; s++) {
        double ar = 1.0 + f->a1[s] * cos(w) + f->a2[s] * cos(2 * w);
        double ai = -f->a1[s] * sin(w) - f->a2[s] * sin(2 * w);
        mag /= sqrt(ar * ar + ai * ai);
    }
    return db(mag);
}

/* Worst error in dB against the circuit, below 20 kHz and 0.45 of the host
 * rate, wherever the circuit is within 30 dB of its reference. */
static double filter_error(const md12_proto *p, const char *csv, double fs, double ref_hz, double *at)
{
    static point pts[2000];
    md12_iir f;
    int n = load_csv(csv, pts, 2000), i;
    double ref = 0.0, worst = 0.0;
    if (!n)
        return 1e9;
    for (i = 0; i < n; i++)
        if (pts[i].f <= ref_hz)
            ref = pts[i].db;
    if (ref_hz <= 10.0)
        ref = pts[0].db;
    md12_iir_design(&f, p, 1.0, fs);
    for (i = 0; i < n; i++) {
        double e;
        if (pts[i].f < 20.0 || pts[i].f > 20000.0 || pts[i].f > 0.45 * fs || pts[i].db - ref < -30.0)
            continue;
        e = fabs(iir_db(&f, fs, pts[i].f) - (pts[i].db - ref));
        if (e != e)
            e = 1e9;
        if (e > worst) {
            worst = e;
            *at = pts[i].f;
        }
    }
    return worst;
}

static void test_filters(void)
{
    static const struct { const md12_proto *p; const char *csv; double ref; } cases[] = {
        {&md12_proto_fixed3, "fixed3", 0.0}, {&md12_proto_fixed4, "fixed4", 0.0},
        {&md12_proto_fixed5, "fixed5", 0.0}, {&md12_proto_fixed6, "fixed6", 0.0},
        {&md12_proto_dynout, "dynout", 1000.0},
    };
    static const double rates[] = {44100.0, 48000.0, 96000.0};
    int c, r;
    for (c = 0; c < 5; c++)
        for (r = 0; r < 3; r++) {
            double at = 0.0, e = filter_error(cases[c].p, cases[c].csv, rates[r], cases[c].ref, &at);
            printf("  %s at %.0f Hz: worst %.3f dB at %.0f Hz\n", cases[c].csv, rates[r], e, at);
            CHECK(e < (rates[r] < 90000.0 ? 0.6 : 0.1), "%s at %.0f: %.3f dB from ngspice at %.0f Hz",
                  cases[c].csv, rates[r], e, at);
        }
}

/* The input path is the preamp's pole and the anti-alias filter in series:
 * their two sweeps add in dB. Its notches must stay notches. */
static void test_input_filter(void)
{
    static point aa[2000], pre[2000];
    static const double rates[] = {44100.0, 48000.0, 96000.0};
    static const double notch[] = {16528.6, 19334.5, 22481.2};
    int n = load_csv("aa", aa, 2000), m = load_csv("preamp_0db", pre, 2000), r, i;
    CHECK(n > 0 && n == m, "spice_aa.csv and spice_preamp_0db.csv");
    for (r = 0; r < 3; r++) {
        md12_iir f;
        double fs = rates[r], worst = 0.0, at = 0.0, ref = aa[0].db + pre[0].db;
        md12_iir_design(&f, &md12_proto_input, 1.0, fs);
        for (i = 0; i < n; i++) {
            double want = aa[i].db + pre[i].db - ref, e;
            if (aa[i].f < 20.0 || aa[i].f > 0.45 * fs || aa[i].f > 20000.0 || want < -30.0)
                continue;
            e = fabs(iir_db(&f, fs, aa[i].f) - want);
            if (e != e)
                e = 1e9;
            if (e > worst) {
                worst = e;
                at = aa[i].f;
            }
        }
        printf("  input filter at %.0f Hz: worst %.3f dB at %.0f Hz\n", fs, worst, at);
        CHECK(worst < (fs < 90000.0 ? 0.6 : 0.1), "input filter at %.0f: %.3f dB at %.0f Hz", fs, worst, at);
        for (i = 0; i < 3; i++)
            if (notch[i] < 0.49 * fs)
                CHECK(iir_db(&f, fs, notch[i]) < -50.0, "notch at %.0f Hz is %.1f dB at %.0f", notch[i],
                      iir_db(&f, fs, notch[i]), fs);
    }
}

int main(void)
{
    test_filters();
    test_input_filter();
    test_tables();
    test_voice();
    test_input();
    test_render_step();
    test_clock();
    test_machine_spectrum();
    test_block_invariance();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
