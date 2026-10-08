#include "internal.h"

/* What the two machines share: the fixed filters and C111 at their drawn
 * values, the SSM2044 at 27 C, and the measured SP-12's sweep, since both
 * service manuals draw that network part for part the same. */
static void common(md12_calib *c)
{
    c->fixed_scale[0] = c->fixed_scale[1] = c->fixed_scale[2] = c->fixed_scale[3] = 1.0f;
    c->dyn_vt = 0.025852f;
    c->dyn_pin13_ohms = MD12_PIN13_MEASURED;
    c->dyn_cap_scale[0] = c->dyn_cap_scale[1] = 1.0f;
    c->dyn_gain[0] = c->dyn_gain[1] = 1.0f;
}

/* SP-1200 service manual, 1987: RT1 is trimmed to read "804 or greater",
 * never exactly 800, and "Re-trim to approx. 804" cures clicking at silence;
 * the factory method trims the dynamic filters to oscillate at 1.0 kHz. The
 * tip of jacks 1 to 6 is unfiltered and the ring filtered (owner's manual;
 * schematic R156 to R161). */
void md12_calib_sp1200(md12_calib *c)
{
    common(c);
    c->adc_offset = 4.5f;
    c->split_jacks = 1;
    c->pitch_lsb = 1;
    c->dyn_rest_hz[0] = c->dyn_rest_hz[1] = 1000.0f;
}

/* SP-12 service manual, 1985: RT1 is trimmed until the display reads 800,
 * and the dynamic filters to oscillate at 1.3 kHz. Jacks 1 to 6 are filtered
 * mono jacks. */
void md12_calib_sp12(md12_calib *c)
{
    common(c);
    c->adc_offset = 0.5f;
    c->split_jacks = 0;
    c->pitch_lsb = 0;
    c->dyn_rest_hz[0] = c->dyn_rest_hz[1] = 1300.0f;
}

/* Nolting's SP-12, from noise played through every output: its fixed filters
 * have the schematic's shape within 0.14 to 0.22 dB rms, 6 to 10% higher;
 * its dynamic filters rest at 1398 and 1481 Hz, against the manual's 1.3 kHz
 * trim, and their sweeps fit the model within 0.024 octaves rms. */
void md12_calib_sp12_measured(md12_calib *c)
{
    md12_calib_sp12(c);
    c->fixed_scale[0] = 1.080f;
    c->fixed_scale[1] = 1.060f;
    c->fixed_scale[2] = 1.083f;
    c->fixed_scale[3] = 1.104f;
    c->dyn_rest_hz[0] = 1398.0f;
    c->dyn_rest_hz[1] = 1481.0f;
    c->dyn_cap_scale[0] = 1.120f;
    c->dyn_cap_scale[1] = 1.090f;
    c->dyn_gain[0] = 0.670f;
    c->dyn_gain[1] = 0.930f;
}
