#ifndef REF_AGC_2D_STATE_H
#define REF_AGC_2D_STATE_H

#include <stdint.h>
#include <string.h>

/* RefAPI mode values, including the engine-only screen fade extension.
 * Keep requested mode separate from the GPU blend translation. */
enum { REF_AGC_2D_SCREEN_FADE_MODULATE = 0x1000 };

typedef struct RefAgc2DState {
    int render_mode;
    int alpha_test;
    int in_2d;
    uint8_t color[4];
} RefAgc2DState;

static inline void ref_agc_2d_reset(RefAgc2DState *state)
{
    memset(state, 0, sizeof(*state));
    memset(state->color, 255, sizeof(state->color));
}

static inline void ref_agc_2d_set_render_mode(RefAgc2DState *state, int mode)
{
    /* Upstream GL_SetRenderMode falls back to opaque for unknown modes. */
    state->render_mode = ((mode >= 0 && mode <= 5) ||
        mode == REF_AGC_2D_SCREEN_FADE_MODULATE) ? mode : 0;
    state->alpha_test = state->render_mode == 4;
}

static inline void ref_agc_2d_set_mode(RefAgc2DState *state, int enabled)
{
    if (enabled && !state->in_2d) {
        state->alpha_test = 1;
        memset(state->color, 255, sizeof(state->color));
    }
    state->in_2d = enabled != 0;
}

static inline void ref_agc_2d_after_fill(RefAgc2DState *state,
                                       uint8_t r, uint8_t g,
                                       uint8_t b, uint8_t a)
{
    state->color[0] = r;
    state->color[1] = g;
    state->color[2] = b;
    state->color[3] = a;
    /* CL_FillRGBA disables blend, but preserves alpha-test and current color. */
    state->render_mode = state->alpha_test ? 4 : 0;
}

#endif
