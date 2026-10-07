/* probe: prints what the library predicts for the dynamic filter of output
 * 1 after one note: time in seconds and the pole frequency in Hz, one line
 * per millisecond for 0.6 s. For fitting the model to recordings.
 *
 *   probe sp1200|sp12 REST_HZ PIN13_OHMS CAP_SCALE
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md12.h"
#include "internal.h"

int main(int argc, char **argv)
{
    static uint16_t snd[100];
    md12_calib cal;
    md12_machine m;
    md12_outputs o;
    int i;

    if (argc != 5) {
        fprintf(stderr, "usage: probe sp1200|sp12 REST_HZ PIN13_OHMS CAP_SCALE\n");
        return 2;
    }
    if (!strcmp(argv[1], "sp12"))
        md12_calib_sp12(&cal);
    else
        md12_calib_sp1200(&cal);
    cal.dyn_rest_hz[0] = (float)atof(argv[2]);
    cal.dyn_pin13_ohms = (float)atof(argv[3]);
    cal.dyn_cap_scale[0] = (float)atof(argv[4]);
    md12_machine_init(&m, &cal, 48000);
    memset(&o, 0, sizeof o);
    md12_machine_trigger(&m, 0, snd, 100, MD12_TUNE_UNITY, 31);
    for (i = 0; i < 48000 * 6 / 10; i++) {
        md12_machine_process(&m, &o, 1);
        if (i % 48 == 0) {
            const md12_dyn *d = &m.dyn[0];
            double oct = d->oct_per_volt * (d->vrest - d->vc);
            printf("%.4f %.2f\n", i / 48000.0, cal.dyn_rest_hz[0] * pow(2.0, oct));
        }
    }
    return 0;
}
