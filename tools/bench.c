/* bench: how long the library takes, against real time at 48 kHz. */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "md12.h"

enum { RATE = 48000, SECONDS = 30, BLOCK = 64 };

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(void)
{
    static float in[BLOCK], out[BLOCK], outs[17][BLOCK];
    static uint16_t snd[65536];
    static md12_effect e;
    static md12_machine m;
    md12_calib cal;
    md12_outputs o;
    uint32_t seed = 1;
    int i, ch, b, blocks = RATE * SECONDS / BLOCK;
    double t0, t;

    md12_calib_sp1200(&cal);
    for (i = 0; i < BLOCK; i++) {
        seed = seed * 1664525u + 1013904223u;
        in[i] = (float)((int32_t)seed) * (0.3f / 2147483648.0f);
    }
    for (ch = 0; ch < 8; ch += 3) {
        md12_effect_init(&e, &cal, RATE);
        md12_effect_set(&e, ch, 11, 31, MD12_TAP_RING);
        t0 = now();
        for (b = 0; b < blocks; b++) {
            if (b % 375 == 0)
                md12_effect_trigger(&e);
            md12_effect_process(&e, in, out, BLOCK);
        }
        t = now() - t0;
        printf("bench: effect on output %d, 45 rpm trick: %.2f%% of real time, %.0f ns a sample\n",
               ch + 1, 100.0 * t / SECONDS, 1e9 * t / ((double)blocks * BLOCK));
    }

    for (i = 0; i < 65536; i++) {
        seed = seed * 1664525u + 1013904223u;
        snd[i] = (uint16_t)(seed >> 20);
    }
    md12_machine_init(&m, &cal, RATE);
    memset(&o, 0, sizeof o);
    o.mix = outs[16];
    for (ch = 0; ch < 8; ch++) {
        o.tip[ch] = outs[ch];
        o.ring[ch] = outs[8 + ch];
    }
    t0 = now();
    for (b = 0; b < blocks; b++) {
        if (b % 750 == 0)
            for (ch = 0; ch < 8; ch++)
                md12_machine_trigger(&m, ch, snd, 65536, 8 + 2 * ch, 31);
        md12_machine_process(&m, &o, BLOCK);
    }
    t = now() - t0;
    printf("bench: eight channels playing, every jack and the mix: %.2f%% of real time, %.0f ns a sample\n",
           100.0 * t / SECONDS, 1e9 * t / ((double)blocks * BLOCK));
    return 0;
}
