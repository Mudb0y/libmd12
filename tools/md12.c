/* md12: renders WAV files through the library.
 *
 *   md12 sample IN.wav OUT.wav [options]
 *     Samples IN through the SP-1200's input, as if recorded into it, and
 *     plays it back once on one channel.
 *   md12 effect IN.wav OUT.wav [options]
 *     Runs IN live through one channel; a tune other than 0 is the 45 rpm
 *     trick at unchanged pitch.
 *
 * Options:
 *   -t SEMITONES   tune, -16 to +15 from the recorded pitch (default 0)
 *   -c CHANNEL     output channel, 1 to 8 (default 8)
 *   -l LEVEL       level, 0 to 31 (default 31)
 *   -o tip|ring|mix   which jack to record (default tip)
 *   -r RATE        output sample rate (default the input's)
 *   -m sp1200|sp12|sp12m  the machine; sp12m is the measured SP-12
 *                  (default sp1200)
 *   -g GAIN        input gain, linear (default 1)
 *   -s A/B         the 45 rpm trick: sample IN as if played A/B times faster
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md12.h"
#include "wav.h"

static void usage(void)
{
    fprintf(stderr, "usage: md12 sample|effect IN.wav OUT.wav [-t semitones] [-c channel] [-l level]\n"
                    "            [-o tip|ring|mix] [-r rate] [-m sp1200|sp12|sp12m] [-g gain] [-s A/B]\n");
    exit(2);
}

static void fail(const char *msg)
{
    fprintf(stderr, "md12: %s\n", msg);
    exit(1);
}

/* "45/33.33" to a ratio of whole numbers, to two decimal places. */
static void parse_speed(const char *s, uint32_t *num, uint32_t *den)
{
    double a, b;
    if (sscanf(s, "%lf/%lf", &a, &b) != 2 || a <= 0 || b <= 0)
        fail("speed must look like 45/33.33");
    *num = (uint32_t)(a * 100.0 + 0.5);
    *den = (uint32_t)(b * 100.0 + 0.5);
}

int main(int argc, char **argv)
{
    const char *in_path, *out_path, *tap = "tip";
    int tune = 0, channel = 8, level = 31, i;
    uint32_t rate = 0, in_rate, speed_num = 1, speed_den = 1;
    float gain = 1.0f, *x, *y;
    long n, max_out, done = 0;
    char err[256];
    md12_calib cal;
    md12_input in;
    md12_machine m;
    md12_outputs o;
    uint16_t *codes;
    int ncodes, more;

    if (argc < 4 || (strcmp(argv[1], "sample") && strcmp(argv[1], "effect")))
        usage();
    in_path = argv[2];
    out_path = argv[3];
    md12_calib_sp1200(&cal);
    for (i = 4; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!v)
            usage();
        if (!strcmp(a, "-t"))
            tune = atoi(v);
        else if (!strcmp(a, "-c"))
            channel = atoi(v);
        else if (!strcmp(a, "-l"))
            level = atoi(v);
        else if (!strcmp(a, "-o"))
            tap = v;
        else if (!strcmp(a, "-r"))
            rate = (uint32_t)atol(v);
        else if (!strcmp(a, "-g"))
            gain = (float)atof(v);
        else if (!strcmp(a, "-s"))
            parse_speed(v, &speed_num, &speed_den);
        else if (!strcmp(a, "-m")) {
            if (!strcmp(v, "sp1200"))
                md12_calib_sp1200(&cal);
            else if (!strcmp(v, "sp12"))
                md12_calib_sp12(&cal);
            else if (!strcmp(v, "sp12m"))
                md12_calib_sp12_measured(&cal);
            else
                usage();
        } else
            usage();
        i++;
    }
    if (tune < -16 || tune > 15 || channel < 1 || channel > 8 || level < 0 || level > 31)
        fail("tune, channel or level out of range");
    if (strcmp(tap, "tip") && strcmp(tap, "ring") && strcmp(tap, "mix"))
        usage();

    n = wav_read_mono(in_path, &x, &in_rate, err, sizeof err);
    if (n < 0)
        fail(err);
    if (!rate)
        rate = in_rate;

    if (!strcmp(argv[1], "effect")) {
        static md12_effect e;
        int t = !strcmp(tap, "tip") ? MD12_TAP_TIP : !strcmp(tap, "ring") ? MD12_TAP_RING : MD12_TAP_MIX;
        long total;
        if (rate != in_rate)
            fail("effect runs at the input's own rate");
        md12_effect_init(&e, &cal, in_rate);
        md12_effect_set(&e, channel - 1, MD12_TUNE_UNITY + tune, level, t);
        md12_effect_set_gain(&e, gain);
        total = n + md12_effect_latency(&e) + 64;
        x = realloc(x, (size_t)total * sizeof *x);
        y = malloc((size_t)total * sizeof *y);
        memset(x + n, 0, (size_t)(total - n) * sizeof *x);
        md12_effect_process(&e, x, y, (int)total);
        if (wav_write_mono(out_path, y, total, rate, err, sizeof err))
            fail(err);
        printf("md12: wrote %s, %.2f s at %u Hz\n", out_path, (double)total / rate, rate);
        free(x);
        free(y);
        return 0;
    }

    /* Sample: the input, then enough silence to flush the interpolator. */
    md12_input_init(&in, &cal, in_rate, speed_num, speed_den);
    md12_input_set_gain(&in, gain);
    codes = malloc(((size_t)md12_input_max_codes(&in, (int)n + MD12_TAPS) + 1) * sizeof *codes);
    ncodes = md12_input_process(&in, x, (int)n, codes, md12_input_max_codes(&in, (int)n));
    {
        float pad[MD12_TAPS] = {0};
        ncodes += md12_input_process(&in, pad, MD12_TAPS / 2 + 1, codes + ncodes,
                                     md12_input_max_codes(&in, MD12_TAPS / 2 + 1));
    }

    /* Play it once, and stop a tenth of a second after the voice ends. */
    md12_machine_init(&m, &cal, rate);
    md12_machine_trigger(&m, channel - 1, codes, (uint32_t)ncodes, MD12_TUNE_UNITY + tune, level);
    max_out = (long)((double)ncodes * 4.0 * rate / MD12_TICK_HZ) + rate;
    y = malloc((size_t)max_out * sizeof *y);
    memset(&o, 0, sizeof o);
    for (more = 1; done < max_out;) {
        int len = 512 < max_out - done ? 512 : (int)(max_out - done);
        if (!strcmp(tap, "mix"))
            o.mix = y + done;
        else if (!strcmp(tap, "tip"))
            o.tip[channel - 1] = y + done;
        else
            o.ring[channel - 1] = y + done;
        md12_machine_process(&m, &o, len);
        done += len;
        if (!m.voice[channel - 1].active) {
            if (!more)
                break;
            more = 0;
            max_out = done + rate / 10 < max_out ? done + rate / 10 : max_out;
        }
    }
    if (wav_write_mono(out_path, y, done, rate, err, sizeof err))
        fail(err);
    printf("md12: wrote %s, %.2f s at %u Hz\n", out_path, (double)done / rate, rate);
    free(x);
    free(y);
    free(codes);
    return 0;
}
