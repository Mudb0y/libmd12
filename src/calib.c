#include "internal.h"

/* SP-1200 service manual, 1987: RT1 is trimmed to read "804 or greater",
 * never exactly 800, and "Re-trim to approx. 804" cures clicking at silence.
 * The tip of jacks 1 to 6 is unfiltered and the ring filtered (owner's
 * manual; schematic R156 to R161). */
void md12_calib_sp1200(md12_calib *c)
{
    c->adc_offset = 4.5f;
    c->split_jacks = 1;
    c->fixed_scale[0] = c->fixed_scale[1] = c->fixed_scale[2] = c->fixed_scale[3] = 1.0f;
}

/* SP-12 service manual, 1985: RT1 is trimmed until the display reads 800.
 * Jacks 1 to 6 are filtered mono jacks. */
void md12_calib_sp12(md12_calib *c)
{
    c->adc_offset = 0.5f;
    c->split_jacks = 0;
    c->fixed_scale[0] = c->fixed_scale[1] = c->fixed_scale[2] = c->fixed_scale[3] = 1.0f;
}
