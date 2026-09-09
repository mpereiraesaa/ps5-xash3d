#include "../src/goldsrc_render_state.h"

#include <assert.h>
#include <string.h>

static void assert_mode(uint32_t mode, GoldSrcBlendMode blend,
                        uint8_t depth_write)
{
    GoldSrcRenderState state;
    assert(goldsrc_render_state_from_mode(
        mode, GOLDSRC_CULL_BACK, 1, 1, &state) == 0);
    assert(state.blend == blend);
    assert(state.cull == GOLDSRC_CULL_BACK);
    assert(state.depth_write == depth_write);
    assert(state.fog == 1u && state.lightmap == 1u);
    assert(state.screen_space == 0u);
}

int main(void)
{
    assert_mode(GOLDSRC_RENDER_NORMAL, GOLDSRC_BLEND_OPAQUE, 1u);
    assert_mode(GOLDSRC_RENDER_TRANS_COLOR, GOLDSRC_BLEND_ALPHA, 0u);
    assert_mode(GOLDSRC_RENDER_TRANS_TEXTURE, GOLDSRC_BLEND_ALPHA, 0u);
    assert_mode(GOLDSRC_RENDER_GLOW, GOLDSRC_BLEND_ADDITIVE, 0u);
    assert_mode(GOLDSRC_RENDER_TRANS_ALPHA, GOLDSRC_BLEND_ALPHA_TEST, 1u);
    assert_mode(GOLDSRC_RENDER_TRANS_ADD, GOLDSRC_BLEND_ADDITIVE, 0u);

    uint8_t keys[512];
    memset(keys, 0, sizeof(keys));
    unsigned key_count = 0u;
    for (unsigned blend = 0u; blend < GOLDSRC_BLEND_MODE_COUNT; ++blend)
        for (unsigned depth = 0u; depth < 2u; ++depth)
            for (unsigned cull = 0u; cull < GOLDSRC_CULL_MODE_COUNT; ++cull)
                for (unsigned fog = 0u; fog < 2u; ++fog)
                    for (unsigned lightmap = 0u; lightmap < 2u; ++lightmap) {
                        const GoldSrcRenderState state = {
                            (GoldSrcBlendMode)blend,
                            (GoldSrcCullMode)cull,
                            (uint8_t)depth, (uint8_t)fog,
                            (uint8_t)lightmap, 0u
                        };
                        uint32_t key = 0u;
                        assert(goldsrc_render_state_key(&state, &key) == 0);
                        assert(key < sizeof(keys) && keys[key] == 0u);
                        keys[key] = 1u;
                        ++key_count;
                    }
    assert(key_count == 96u);

    /* Extend only screen space; all old 3D keys stay byte-for-byte stable. */
    const GoldSrcBlendMode screen_blends[] = {
        GOLDSRC_BLEND_OPAQUE, GOLDSRC_BLEND_ALPHA,
        GOLDSRC_BLEND_ADDITIVE, GOLDSRC_BLEND_ALPHA_TEST,
        GOLDSRC_BLEND_SCREEN_MODULATE,
    };
    const uint32_t screen_keys[] = {128u, 129u, 130u, 131u, 384u};
    for (unsigned i = 0u; i < 5u; ++i) {
        GoldSrcRenderState screen;
        uint32_t key;
        assert(goldsrc_render_state_2d(screen_blends[i], &screen) == 0);
        assert(goldsrc_render_state_key(&screen, &key) == 0);
        assert(key == screen_keys[i] && keys[key] == 0u);
        keys[key] = 1u;
        screen.depth_write = 1u;
        assert(goldsrc_render_state_validate(&screen) == -2);
        screen.depth_write = 0u;
        screen.screen_space = 0u;
        if (screen_blends[i] == GOLDSRC_BLEND_SCREEN_MODULATE)
            assert(goldsrc_render_state_validate(&screen) == -1);
    }

    GoldSrcRenderState state;
    GoldSrcRenderPass pass;
    int sorted = -1;
    assert(goldsrc_render_state_2d(GOLDSRC_BLEND_ALPHA, &state) == 0);
    assert(goldsrc_render_state_pass(&state, &pass) == 0);
    assert(pass == GOLDSRC_PASS_SCREEN_2D);
    assert(goldsrc_render_state_requires_back_to_front(&state, &sorted) == 0);
    assert(sorted == 0);

    assert(goldsrc_render_state_from_mode(
        GOLDSRC_RENDER_TRANS_TEXTURE, GOLDSRC_CULL_NONE, 0, 0,
        &state) == 0);
    assert(goldsrc_render_state_pass(&state, &pass) == 0);
    assert(pass == GOLDSRC_PASS_TRANSLUCENT);
    assert(goldsrc_render_state_requires_back_to_front(&state, &sorted) == 0);
    assert(sorted == 1);

    assert(goldsrc_render_state_from_mode(
        GOLDSRC_RENDER_TRANS_ALPHA, GOLDSRC_CULL_FRONT, 0, 1,
        &state) == 0);
    assert(goldsrc_render_state_pass(&state, &pass) == 0);
    assert(pass == GOLDSRC_PASS_MASKED);
    assert(goldsrc_render_state_requires_back_to_front(&state, &sorted) == 0);
    assert(sorted == 0);

    assert(goldsrc_render_state_2d(GOLDSRC_BLEND_ALPHA_TEST, &state) == 0);
    assert(goldsrc_render_state_2d(GOLDSRC_BLEND_MODE_COUNT, &state) == -1);
    assert(goldsrc_render_state_2d((GoldSrcBlendMode)-1, &state) == -1);
    assert(goldsrc_render_state_from_mode(
        GOLDSRC_RENDER_MODE_COUNT, GOLDSRC_CULL_BACK, 0, 0,
        &state) == -1);
    assert(goldsrc_render_state_from_mode(
        GOLDSRC_RENDER_NORMAL, GOLDSRC_CULL_MODE_COUNT, 0, 0,
        &state) == -1);
    assert(goldsrc_render_state_from_mode(
        GOLDSRC_RENDER_NORMAL, GOLDSRC_CULL_BACK, 2, 0,
        &state) == -1);
    state = (GoldSrcRenderState){
        GOLDSRC_BLEND_ALPHA_TEST, GOLDSRC_CULL_NONE,
        0u, 0u, 0u, 1u
    };
    assert(goldsrc_render_state_validate(&state) == 0);
    state.blend = (GoldSrcBlendMode)-1;
    assert(goldsrc_render_state_validate(&state) == -1);
    assert(goldsrc_render_state_key(&state, 0) == -1);
    return 0;
}
