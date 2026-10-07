#include "internal.h"

static uint64_t gcd64(uint64_t a, uint64_t b)
{
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* Channel n's hold capacitor is strobed in the nth 96-cycle slot of each
 * 768-cycle frame. Strobe times are kept as exact fractions of a host sample,
 * so they never drift against the host clock. */
void md12_clock_init(md12_clock *k, uint32_t host_rate)
{
    uint64_t num = (uint64_t)MD12_SLOT_CYCLES * host_rate;
    uint64_t den = MD12_CLOCK_HZ;
    uint64_t g = gcd64(num, den);

    k->slot_num = (uint32_t)(num / g);
    k->den = (uint32_t)(den / g);
    k->inv_den = 1.0f / (float)k->den;
    k->num = 0;
    k->whole = 0;
    k->slot = 0;
}

uint64_t md12_gcd64(uint64_t a, uint64_t b)
{
    return gcd64(a, b);
}
