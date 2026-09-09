#ifndef REF_AGC_2D_STATE_H
#define REF_AGC_2D_STATE_H

/* RefAPI mode values, including the engine-only screen fade extension.
 * Keep requested mode separate from the GPU blend translation. */
enum { REF_AGC_2D_SCREEN_FADE_MODULATE = 0x1000 };

typedef struct RefAgc2DState {
    int render_mode;
} RefAgc2DState;

static inline void ref_agc_2d_set_render_mode(RefAgc2DState *state, int mode)
{
    /* Upstream GL_SetRenderMode falls back to opaque for unknown modes. */
    state->render_mode = ((mode >= 0 && mode <= 5) ||
        mode == REF_AGC_2D_SCREEN_FADE_MODULATE) ? mode : 0;
}

#endif
