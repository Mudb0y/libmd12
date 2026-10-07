#include "internal.h"

void md12_voice_start(md12_voice *v, const uint16_t *mem, uint32_t length,
                      int step128, int level_index)
{
    v->mem = mem;
    v->addr = 0;
    v->end = length;
    v->loop = 0;
    v->looping = 0;
    /* Whether the controller clears the fraction at a start is not known;
     * it only shifts which samples a pitched sound repeats or skips. */
    v->frac = 0;
    v->step = (uint16_t)step128;
    v->level = (uint8_t)(level_index < 0 ? 0 : level_index > MD12_LEVEL_MAX ? MD12_LEVEL_MAX : level_index);
    v->active = length > 0;
}

void md12_voice_set_loop(md12_voice *v, uint32_t loop_start)
{
    v->loop = loop_start < v->end ? loop_start : 0;
    v->looping = 1;
}

void md12_voice_stop(md12_voice *v)
{
    v->active = 0;
}

uint16_t md12_voice_tick(md12_voice *v)
{
    uint16_t code;
    unsigned sum;

    if (!v->active)
        return MD12_ZERO_CODE;
    code = v->mem[v->addr] & 0x0FFF;

    /* A carry plays the next sample, none plays this one again, and the
     * pitch byte's top bit adds another (theory of operation, p. 53). */
    sum = v->frac + v->step;
    v->frac = (uint8_t)(sum & 0x7Fu);
    v->addr += sum >> 7;

    if (v->addr >= v->end) {
        if (v->looping)
            v->addr = v->loop + (v->addr - v->end) % (v->end - v->loop);
        else
            v->active = 0;
    }
    return code;
}
