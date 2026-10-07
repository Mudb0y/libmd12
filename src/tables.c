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

uint8_t md12_tune_byte(int tune_index)
{
    if (tune_index < 0)
        tune_index = 0;
    if (tune_index > 31)
        tune_index = 31;
    return tune_table[tune_index];
}

/* The pitch byte's top bit adds a whole sample; its low seven feed the
 * fraction adder, whose lowest input and carry-in are tied high (schematic
 * page 8 of 18, IC45). Measured on a real SP-12: the playback ratios of the
 * whole tune range fit (floor(byte / 2) + 1) / 64 to 0.23% rms. */
int md12_step64(uint8_t pitch_byte)
{
    return (pitch_byte >> 1) + 1;
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
