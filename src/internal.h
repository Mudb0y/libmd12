#ifndef MD12_INTERNAL_H
#define MD12_INTERNAL_H

#include "md12.h"

/* Row p of each table is the kernel at phase p / MD12_PHASES, with one extra
 * row so the last phase can be interpolated. */
extern const float md12_step_tab[(MD12_PHASES + 1) * MD12_TAPS];
extern const float md12_sinc_tab[(MD12_PHASES + 1) * MD12_TAPS];

uint64_t md12_gcd64(uint64_t a, uint64_t b);

/* An analogue filter: H(s) = gain * prod(s - zero) / prod(s - pole), roots in
 * rad/s. An entry with a positive imaginary part stands for a conjugate pair. */
typedef struct md12_proto {
    int npole, nzero;
    double pole[8][2], zero[8][2];
    double gain;
} md12_proto;

extern const md12_proto md12_proto_input;
extern const md12_proto md12_proto_fixed3, md12_proto_fixed4, md12_proto_fixed5, md12_proto_fixed6;
extern const md12_proto md12_proto_dynout;

/* Designs f to follow p with its frequencies multiplied by scale, at the host
 * rate fs. Uses double precision and the maths library: setup only. */
void md12_iir_design(md12_iir *f, const md12_proto *p, double scale, double fs);
float md12_iir_run(md12_iir *f, float x);

#define MD12_LADDER_LO (-14)
#define MD12_LADDER_STEPS 8
#define MD12_LADDER_ROWS 129
extern const float md12_ladder_tab[MD12_LADDER_ROWS][4];

void md12_dyn_init(md12_dyn *d, const md12_calib *c, int channel, double fs);
float md12_dyn_run(md12_dyn *d, float x);

/* The OS's housekeeping interrupt: CTC prescaler 256 times 16 at the Z80's
 * 20 MHz / 6, which is 24576 master cycles, exactly 32 frames. The filter
 * trigger's GAIN line rises on the eighth tick after a note, 182 to 263 us
 * into the routine: about 6 frames. */
#define MD12_TICK_FRAMES 32
#define MD12_GATE_TICKS 8
#define MD12_GATE_LATE 6

#endif
