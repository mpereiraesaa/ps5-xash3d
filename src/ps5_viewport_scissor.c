#include "ps5_viewport_scissor.h"

#include <stddef.h>
#include <string.h>

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int rect_valid(const Ps5RectU32 *rect,
                      uint32_t framebuffer_width,
                      uint32_t framebuffer_height)
{
    if (!rect || !rect->width || !rect->height)
        return 0;
    const uint64_t right = (uint64_t)rect->x + rect->width;
    const uint64_t bottom = (uint64_t)rect->y + rect->height;
    return right <= framebuffer_width && bottom <= framebuffer_height &&
           right <= 0x7fffu && bottom <= 0x7fffu;
}

int ps5_viewport_scissor_build(
    ps5_agc_register out[PS5_VIEWPORT_SCISSOR_REGISTER_COUNT],
    const Ps5RectU32 *viewport, const Ps5RectU32 *scissor,
    uint32_t framebuffer_width, uint32_t framebuffer_height)
{
    if (!out || !framebuffer_width || !framebuffer_height ||
        framebuffer_width > 0x7fffu || framebuffer_height > 0x7fffu ||
        !rect_valid(viewport, framebuffer_width, framebuffer_height) ||
        !rect_valid(scissor, framebuffer_width, framebuffer_height))
        return -1;
    const uint32_t scissor_right = scissor->x + scissor->width;
    const uint32_t scissor_bottom = scissor->y + scissor->height;
    const float half_width = (float)viewport->width * 0.5f;
    const float half_height = (float)viewport->height * 0.5f;
    const ps5_agc_register plan[PS5_VIEWPORT_SCISSOR_REGISTER_COUNT] = {
        {0x10f, float_bits(half_width)},
        {0x110, float_bits((float)viewport->x + half_width)},
        {0x111, float_bits(-half_height)},
        {0x112, float_bits((float)viewport->y + half_height)},
        {0x113, float_bits(1.0f)},
        {0x114, 0u},
        {0x090, UINT32_C(0x80000000) | scissor->x |
                    (scissor->y << 16)},
        {0x091, scissor_right | (scissor_bottom << 16)},
    };
    memcpy(out, plan, sizeof(plan));
    return 0;
}
