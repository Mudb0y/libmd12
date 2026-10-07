#ifndef WAV_H
#define WAV_H

#include <stdint.h>

/* Reads 16, 24 or 32-bit PCM or 32-bit float, mixed down to mono. Returns
 * the sample count, or -1 with a message in err. */
long wav_read_mono(const char *path, float **out, uint32_t *rate, char *err, int errlen);

/* Writes 32-bit float, mono. Returns 0, or -1 with a message in err. */
int wav_write_mono(const char *path, const float *x, long n, uint32_t rate, char *err, int errlen);

#endif
