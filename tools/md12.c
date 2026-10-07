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
 *   -o tip|ring|mix|tipring   which jack to record; tipring writes the tip
 *                  left and the ring right, as a stereo cable would
 *                  (default tip)
 *   -r RATE        output sample rate (default the input's)
 *   -m sp1200|sp12|sp12m  the machine; sp12m is the measured SP-12
 *                  (default sp1200)
 *   -k FILE        a calibration file, as tools/kit/analyse.py writes,
 *                  read over the machine's
 *   -p MS          effect only: fire the filter of outputs 1 and 2 every MS
 *                  milliseconds, as pad hits would (default never)
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
                    "            [-o tip|ring|mix|tipring] [-r rate] [-m sp1200|sp12|sp12m] [-k file]\n"
                    "            [-g gain] [-s A/B] [-p ms]\n");
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

/* Lines of "name value...", # for comments; a machine line chooses the
 * preset the rest is read over. */
static void load_calib(const char *path, md12_calib *c)
{
    FILE *f = fopen(path, "r");
    char line[256], key[64];
    float v[4];
    int n;

    if (!f)
        fail("cannot open the calibration file");
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, "%63s", key) != 1)
            continue;
        if (!strcmp(key, "machine")) {
            char name[32];
            if (sscanf(line, "%*s %31s", name) == 1)
                !strcmp(name, "sp12") ? md12_calib_sp12(c) : md12_calib_sp1200(c);
            continue;
        }
        n = sscanf(line, "%*s %f %f %f %f", &v[0], &v[1], &v[2], &v[3]);
        if (!strcmp(key, "adc_offset") && n >= 1)
            c->adc_offset = v[0];
        else if (!strcmp(key, "split_jacks") && n >= 1)
            c->split_jacks = (int)v[0];
        else if (!strcmp(key, "pitch_lsb") && n >= 1)
            c->pitch_lsb = (int)v[0];
        else if (!strcmp(key, "fixed_scale") && n >= 4)
            memcpy(c->fixed_scale, v, sizeof c->fixed_scale);
        else if (!strcmp(key, "dyn_rest_hz") && n >= 2)
            memcpy(c->dyn_rest_hz, v, sizeof c->dyn_rest_hz);
        else if (!strcmp(key, "dyn_vt") && n >= 1)
            c->dyn_vt = v[0];
        else if (!strcmp(key, "dyn_pin13_ohms") && n >= 1)
            c->dyn_pin13_ohms = v[0];
        else if (!strcmp(key, "dyn_cap_scale") && n >= 2)
            memcpy(c->dyn_cap_scale, v, sizeof c->dyn_cap_scale);
        else if (!strcmp(key, "dyn_gain") && n >= 2)
            memcpy(c->dyn_gain, v, sizeof c->dyn_gain);
        else {
            fclose(f);
            fprintf(stderr, "md12: %s: cannot read the line starting %s\n", path, key);
            exit(1);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *in_path, *out_path, *tap = "tip", *calfile = NULL;
    int tune = 0, channel = 8, level = 31, i, stereo, ncodes, ended = 0;
    uint32_t rate = 0, in_rate, speed_num = 1, speed_den = 1;
    float gain = 1.0f, every_ms = 0.0f, *x, *y, *y2;
    long n, max_out, done = 0;
    char err[256];
    md12_calib cal;
    md12_input in;
    md12_machine m;
    md12_outputs o;
    uint16_t *codes;

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
        else if (!strcmp(a, "-k"))
            calfile = v;
        else if (!strcmp(a, "-p"))
            every_ms = (float)atof(v);
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
    if (calfile)
        load_calib(calfile, &cal);
    if (tune < -16 || tune > 15 || channel < 1 || channel > 8 || level < 0 || level > 31)
        fail("tune, channel or level out of range");
    if (strcmp(tap, "tip") && strcmp(tap, "ring") && strcmp(tap, "mix") && strcmp(tap, "tipring"))
        usage();
    stereo = !strcmp(tap, "tipring");

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
        if (stereo)
            fail("effect records one jack");
        md12_effect_init(&e, &cal, in_rate);
        md12_effect_set(&e, channel - 1, MD12_TUNE_UNITY + tune, level, t);
        md12_effect_set_gain(&e, gain);
        total = n + md12_effect_latency(&e) + 64;
        x = realloc(x, (size_t)total * sizeof *x);
        y = malloc((size_t)total * sizeof *y);
        memset(x + n, 0, (size_t)(total - n) * sizeof *x);
        if (every_ms > 0.0f) {
            long step = (long)(every_ms * 0.001f * (float)rate), at;
            for (at = 0; at < total; at += step) {
                long len = total - at < step ? total - at : step;
                md12_effect_trigger(&e);
                md12_effect_process(&e, x + at, y + at, (int)len);
            }
        } else {
            md12_effect_process(&e, x, y, (int)total);
        }
        if (wav_write_mono(out_path, y, total, rate, err, sizeof err))
            fail(err);
        printf("md12: wrote %s, %.2f s at %u Hz\n", out_path, (double)total / rate, rate);
        free(x);
        free(y);
        return 0;
    }

    /* Sample: the input, then enough silence to flush the interpolator. As
     * the OS stores a fresh sound, the first five samples are silent and the
     * last five are never played. */
    md12_input_init(&in, &cal, in_rate, speed_num, speed_den);
    md12_input_set_gain(&in, gain);
    codes = malloc(((size_t)md12_input_max_codes(&in, (int)n + MD12_TAPS) + 8) * sizeof *codes);
    for (i = 0; i < 5; i++)
        codes[i] = MD12_ZERO_CODE;
    ncodes = 5 + md12_input_process(&in, x, (int)n, codes + 5, md12_input_max_codes(&in, (int)n));
    {
        float pad[MD12_TAPS] = {0};
        ncodes += md12_input_process(&in, pad, MD12_TAPS / 2 + 1, codes + ncodes,
                                     md12_input_max_codes(&in, MD12_TAPS / 2 + 1));
    }
    ncodes = ncodes > 10 ? ncodes - 5 : ncodes;
    /* A sound cannot cross a 64K-sample bank. */
    if (ncodes > 65536)
        ncodes = 65536;

    /* Play it once, and stop a tenth of a second after the voice ends. */
    md12_machine_init(&m, &cal, rate);
    md12_machine_trigger(&m, channel - 1, codes, (uint32_t)ncodes, MD12_TUNE_UNITY + tune, level);
    max_out = (long)((double)ncodes * 4.0 * rate / MD12_TICK_HZ) + rate;
    y = calloc((size_t)max_out, sizeof *y);
    y2 = calloc((size_t)max_out, sizeof *y2);
    memset(&o, 0, sizeof o);
    while (done < max_out) {
        int len = 512 < max_out - done ? 512 : (int)(max_out - done);
        if (!strcmp(tap, "mix"))
            o.mix = y + done;
        else if (!strcmp(tap, "ring"))
            o.ring[channel - 1] = y + done;
        else
            o.tip[channel - 1] = y + done;
        if (stereo)
            o.ring[channel - 1] = y2 + done;
        md12_machine_process(&m, &o, len);
        done += len;
        /* The voice starts a few frames after the note. */
        if (!ended && done > rate / 100 && !m.voice[channel - 1].active) {
            ended = 1;
            if (done + rate / 10 < max_out)
                max_out = done + rate / 10;
        }
    }
    if (stereo ? wav_write_stereo(out_path, y, y2, done, rate, err, sizeof err)
               : wav_write_mono(out_path, y, done, rate, err, sizeof err))
        fail(err);
    printf("md12: wrote %s, %.2f s at %u Hz\n", out_path, (double)done / rate, rate);
    free(x);
    free(y);
    free(y2);
    free(codes);
    return 0;
}
