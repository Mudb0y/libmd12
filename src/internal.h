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

#endif
