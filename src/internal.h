#ifndef MD12_INTERNAL_H
#define MD12_INTERNAL_H

#include "md12.h"

/* Row p of each table is the kernel at phase p / MD12_PHASES, with one extra
 * row so the last phase can be interpolated. */
extern const float md12_step_tab[(MD12_PHASES + 1) * MD12_TAPS];
extern const float md12_sinc_tab[(MD12_PHASES + 1) * MD12_TAPS];

uint64_t md12_gcd64(uint64_t a, uint64_t b);

#endif
