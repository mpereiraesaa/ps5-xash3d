#ifndef PS5_XASH3D_PS5_VIEWPORT_SCISSOR_H
#define PS5_XASH3D_PS5_VIEWPORT_SCISSOR_H

#include "../include/ps5_agc.h"

enum {
    PS5_VIEWPORT_SCISSOR_REGISTER_COUNT = 8,
};

typedef struct Ps5RectU32 {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} Ps5RectU32;

/* Builds a mid-frame viewport plus generic-scissor CX update. */
int ps5_viewport_scissor_build(
    ps5_agc_register out[PS5_VIEWPORT_SCISSOR_REGISTER_COUNT],
    const Ps5RectU32 *viewport, const Ps5RectU32 *scissor,
    uint32_t framebuffer_width, uint32_t framebuffer_height);

#endif
