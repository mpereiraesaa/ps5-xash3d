#ifndef PS5_XASH3D_GOLDSRC_RENDER_STATE_H
#define PS5_XASH3D_GOLDSRC_RENDER_STATE_H

#include <stdint.h>

/* GoldSrc entity rendermode values. Keep these numeric values ABI-compatible. */
typedef enum GoldSrcRenderMode {
    GOLDSRC_RENDER_NORMAL = 0,
    GOLDSRC_RENDER_TRANS_COLOR = 1,
    GOLDSRC_RENDER_TRANS_TEXTURE = 2,
    GOLDSRC_RENDER_GLOW = 3,
    GOLDSRC_RENDER_TRANS_ALPHA = 4,
    GOLDSRC_RENDER_TRANS_ADD = 5,
    GOLDSRC_RENDER_MODE_COUNT = 6,
} GoldSrcRenderMode;

typedef enum GoldSrcBlendMode {
    GOLDSRC_BLEND_OPAQUE = 0,
    GOLDSRC_BLEND_ALPHA = 1,
    GOLDSRC_BLEND_ADDITIVE = 2,
    GOLDSRC_BLEND_ALPHA_TEST = 3,
    GOLDSRC_BLEND_MODE_COUNT = 4,
    /* Screen-only extension: do not expand the contiguous 3D permutations. */
    GOLDSRC_BLEND_SCREEN_MODULATE = 0x1000,
    GOLDSRC_BLEND_SCREEN_ALPHA_MASKED = 0x1001,
    GOLDSRC_BLEND_SCREEN_ADDITIVE_MASKED = 0x1002,
    GOLDSRC_BLEND_SCREEN_MODULATE_MASKED = 0x1003,
} GoldSrcBlendMode;

static inline int goldsrc_blend_screen_masked(GoldSrcBlendMode blend)
{
    return blend == GOLDSRC_BLEND_ALPHA_TEST ||
        (blend >= GOLDSRC_BLEND_SCREEN_ALPHA_MASKED &&
         blend <= GOLDSRC_BLEND_SCREEN_MODULATE_MASKED);
}

typedef enum GoldSrcCullMode {
    GOLDSRC_CULL_NONE = 0,
    GOLDSRC_CULL_FRONT = 1,
    GOLDSRC_CULL_BACK = 2,
    GOLDSRC_CULL_MODE_COUNT = 3,
} GoldSrcCullMode;

typedef enum GoldSrcRenderPass {
    GOLDSRC_PASS_OPAQUE = 0,
    GOLDSRC_PASS_MASKED = 1,
    GOLDSRC_PASS_TRANSLUCENT = 2,
    GOLDSRC_PASS_ADDITIVE = 3,
    GOLDSRC_PASS_SCREEN_2D = 4,
} GoldSrcRenderPass;

typedef struct GoldSrcRenderState {
    GoldSrcBlendMode blend;
    GoldSrcCullMode cull;
    uint8_t depth_write;
    uint8_t fog;
    uint8_t lightmap;
    uint8_t screen_space;
} GoldSrcRenderState;

enum {
    GOLDSRC_RENDER_KEY_BLEND_SHIFT = 0,
    GOLDSRC_RENDER_KEY_DEPTH_WRITE_SHIFT = 2,
    GOLDSRC_RENDER_KEY_CULL_SHIFT = 3,
    GOLDSRC_RENDER_KEY_FOG_SHIFT = 5,
    GOLDSRC_RENDER_KEY_LIGHTMAP_SHIFT = 6,
    GOLDSRC_RENDER_KEY_SCREEN_SPACE_SHIFT = 7,
    GOLDSRC_RENDER_KEY_SCREEN_MODULATE = (1u << 8) | (1u << 7),
};

int goldsrc_render_state_validate(const GoldSrcRenderState *state);

int goldsrc_render_state_from_mode(uint32_t render_mode,
                                   GoldSrcCullMode cull,
                                   int fog, int lightmap,
                                   GoldSrcRenderState *out);

int goldsrc_render_state_2d(GoldSrcBlendMode blend,
                            GoldSrcRenderState *out);

int goldsrc_render_state_key(const GoldSrcRenderState *state,
                             uint32_t *out_key);

int goldsrc_render_state_pass(const GoldSrcRenderState *state,
                              GoldSrcRenderPass *out_pass);

int goldsrc_render_state_requires_back_to_front(
    const GoldSrcRenderState *state, int *out_required);

#endif
