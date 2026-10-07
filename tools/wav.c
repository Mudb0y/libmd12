#include "wav.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

long wav_read_mono(const char *path, float **out, uint32_t *rate, char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    unsigned char h[12], ck[8], fmt[40];
    unsigned fmt_tag = 0, channels = 0, bits = 0, have_fmt = 0;
    long frames, i;
    unsigned char *raw;
    float *x;
    uint32_t len;

    if (!f) {
        snprintf(err, errlen, "%s: %s", path, strerror(errno));
        return -1;
    }
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        snprintf(err, errlen, "%s: not a WAV file", path);
        fclose(f);
        return -1;
    }
    for (;;) {
        if (fread(ck, 1, 8, f) != 8) {
            snprintf(err, errlen, "%s: no data chunk", path);
            fclose(f);
            return -1;
        }
        len = le32(ck + 4);
        if (!memcmp(ck, "fmt ", 4)) {
            unsigned n = len < sizeof fmt ? len : (unsigned)sizeof fmt;
            if (fread(fmt, 1, n, f) != n || len < 16) {
                snprintf(err, errlen, "%s: short fmt chunk", path);
                fclose(f);
                return -1;
            }
            fseek(f, (long)(len - n + (len & 1)), SEEK_CUR);
            fmt_tag = le16(fmt);
            channels = le16(fmt + 2);
            *rate = le32(fmt + 4);
            bits = le16(fmt + 14);
            if (fmt_tag == 0xFFFE && len >= 26)
                fmt_tag = le16(fmt + 24);
            have_fmt = 1;
        } else if (!memcmp(ck, "data", 4)) {
            break;
        } else {
            fseek(f, (long)(len + (len & 1)), SEEK_CUR);
        }
    }
    if (!have_fmt || channels == 0 ||
        !((fmt_tag == 1 && (bits == 16 || bits == 24 || bits == 32)) || (fmt_tag == 3 && bits == 32))) {
        snprintf(err, errlen, "%s: unsupported format (tag %u, %u bits)", path, fmt_tag, bits);
        fclose(f);
        return -1;
    }
    frames = (long)(len / (channels * (bits / 8)));
    raw = malloc((size_t)frames * channels * (bits / 8));
    x = malloc((size_t)(frames ? frames : 1) * sizeof *x);
    if (!raw || !x || fread(raw, channels * (bits / 8), (size_t)frames, f) != (size_t)frames) {
        snprintf(err, errlen, "%s: short read", path);
        free(raw);
        free(x);
        fclose(f);
        return -1;
    }
    fclose(f);
    for (i = 0; i < frames; i++) {
        double sum = 0.0;
        unsigned c;
        for (c = 0; c < channels; c++) {
            const unsigned char *p = raw + ((size_t)i * channels + c) * (bits / 8);
            double v;
            if (fmt_tag == 3) {
                float fv;
                uint32_t u = le32(p);
                memcpy(&fv, &u, 4);
                v = fv;
            } else if (bits == 16) {
                v = (int16_t)le16(p) / 32768.0;
            } else if (bits == 24) {
                v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.0;
            } else {
                v = (int32_t)le32(p) / 2147483648.0;
            }
            sum += v;
        }
        x[i] = (float)(sum / channels);
    }
    free(raw);
    *out = x;
    return frames;
}

static void put32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static void put16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }

static int write_wav(const char *path, const float *l, const float *r, long n, uint32_t rate,
                     char *err, int errlen)
{
    FILE *f = fopen(path, "wb");
    unsigned char h[44];
    unsigned ch = r ? 2u : 1u;
    long i;

    if (!f) {
        snprintf(err, errlen, "%s: %s", path, strerror(errno));
        return -1;
    }
    memcpy(h, "RIFF", 4);
    put32(h + 4, (uint32_t)(36 + n * 4 * ch));
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 3);
    put16(h + 22, (uint16_t)ch);
    put32(h + 24, rate);
    put32(h + 28, rate * 4 * ch);
    put16(h + 32, (uint16_t)(4 * ch));
    put16(h + 34, 32);
    memcpy(h + 36, "data", 4);
    put32(h + 40, (uint32_t)(n * 4 * ch));
    fwrite(h, 1, 44, f);
    for (i = 0; i < n; i++) {
        unsigned char b[4];
        uint32_t u;
        memcpy(&u, &l[i], 4);
        put32(b, u);
        fwrite(b, 1, 4, f);
        if (r) {
            memcpy(&u, &r[i], 4);
            put32(b, u);
            fwrite(b, 1, 4, f);
        }
    }
    if (fclose(f)) {
        snprintf(err, errlen, "%s: %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

int wav_write_mono(const char *path, const float *x, long n, uint32_t rate, char *err, int errlen)
{
    return write_wav(path, x, 0, n, rate, err, errlen);
}

int wav_write_stereo(const char *path, const float *l, const float *r, long n, uint32_t rate,
                     char *err, int errlen)
{
    return write_wav(path, l, r, n, rate, err, errlen);
}
