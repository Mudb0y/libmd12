#include "internal.h"

/* The semitone table from the operating system: file offset 0x224E of the
 * SP-1200 OS 1.04 image, and offset 0x7DA of the SP-12 Turbo 2.6 ROM, byte
 * for byte. Entry i is round(128 * 2^((i - 16) / 12)) - 1, clipped at 255. */
static const uint8_t tune_table[32] = {
    50, 53, 56, 59, 63, 67, 71, 75, 80, 84, 90, 95, 101, 107, 113, 120,
    127, 135, 143, 151, 160, 170, 180, 191, 202, 214, 227, 241, 255, 255, 255, 255,
};

/* The level PROM, IC88 (82S123), as dumped by Lytrix/EMU-SP1200: mute, then
 * roughly -48 dB to 0 dB. */
static const uint8_t level_table[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 13, 15, 17, 20, 23,
    27, 31, 36, 42, 49, 57, 66, 77, 90, 104, 121, 140, 163, 189, 220, 255,
};

/* The decay table just before the semitone table, at 0x220E: per decay
 * value, the ticks between level steps and the number of such periods the
 * level is held first, as the tick routine at 0x0776 reads them (the same
 * in E-mu's OS 1.2). */
static const uint8_t decay_table[32][2] = {
    {2, 0}, {2, 4}, {3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1}, {8, 1},
    {9, 1}, {10, 1}, {11, 1}, {12, 2}, {13, 3}, {14, 5}, {15, 7}, {16, 9},
    {17, 11}, {18, 14}, {19, 17}, {20, 21}, {21, 24}, {22, 29}, {23, 34}, {24, 39},
    {25, 46}, {26, 53}, {27, 61}, {28, 70}, {29, 80}, {30, 91}, {31, 104}, {32, 119},
};

void md12_decay_pair(int decay_index, int *period, int *hold)
{
    if (decay_index < 0)
        decay_index = 0;
    if (decay_index > 31)
        decay_index = 31;
    *period = decay_table[decay_index][0];
    *hold = decay_table[decay_index][1];
}

int md12_midi_level(int velocity)
{
    /* The OS: 124 and up plays at full level, below that velocity / 4,
     * never under 2. */
    if (velocity >= 124)
        return MD12_LEVEL_MAX;
    return velocity / 4 < 2 ? 2 : velocity / 4;
}

uint8_t md12_tune_byte(int tune_index)
{
    if (tune_index < 0)
        tune_index = 0;
    if (tune_index > 31)
        tune_index = 31;
    return tune_table[tune_index];
}

/* The pitch byte's top bit adds a whole sample and its low seven bits feed
 * the fraction adder, IC45 and IC46, above an input and a carry-in tied
 * high (schematic page 8 of 18): (byte + 1) / 128 a frame, which pitcher's
 * ratios, measured on an SP-1200, follow to within 0.2% at every step from
 * -1 to -8. A real SP-12 (Nolting's CCRMA recording) loses the lowest bit:
 * its ratios fit (byte | 1) + 1 over 128 to 0.23% rms, and include a step of
 * 1.0434 that the other rule cannot make. */
int md12_step128(uint8_t pitch_byte, int keep_lsb)
{
    return keep_lsb ? pitch_byte + 1 : (pitch_byte | 1) + 1;
}

uint8_t md12_level_code(int level_index)
{
    if (level_index < 0)
        level_index = 0;
    if (level_index > MD12_LEVEL_MAX)
        level_index = MD12_LEVEL_MAX;
    return level_table[level_index];
}

/* The AD7541 multiplies the code, offset to bipolar, by its reference; the
 * level DAC multiplies that again by its code over 256, in analogue, so a
 * quiet sound keeps all twelve bits. */
float md12_dac(uint16_t code, int level_index)
{
    return (float)((int)(code & 0x0FFF) - MD12_ZERO_CODE) * (1.0f / 2048.0f) *
           (float)md12_level_code(level_index) * (1.0f / 256.0f);
}
