#include "../src/ps5_goldsrc_pipeline_runtime.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    GoldSrcPipelineCache cache;
    assert(goldsrc_pipeline_cache_build(&cache, 0xb6u, 0x240u) == 0);
    struct ps5_pipeline_registers pipelines[GOLDSRC_SHADER_VARIANT_COUNT][2];
    Ps5ShaderPipelineSlotResult slots[GOLDSRC_SHADER_VARIANT_COUNT];
    memset(pipelines, 0, sizeof(pipelines));
    memset(slots, 0, sizeof(slots));
    for (uint32_t i = 0; i < GOLDSRC_SHADER_VARIANT_COUNT; ++i) {
        slots[i].pipelines[0] = &pipelines[i][0];
        slots[i].pipelines[1] = &pipelines[i][1];
        slots[i].draw_modifier = 0x1000u + i;
    }
    _Alignas(8) ps5_agc_register gpu_registers[
        GOLDSRC_PIPELINE_PERMUTATION_COUNT *
        PS5_GOLDSRC_RENDER_REGISTER_COUNT];
    Ps5GoldSrcPipelineRuntime runtime;
    assert(ps5_goldsrc_pipeline_runtime_init(
        &runtime, &cache, slots, GOLDSRC_SHADER_VARIANT_COUNT,
        gpu_registers, sizeof(gpu_registers), gpu_registers,
        sizeof(gpu_registers)) == 0);
    assert(ps5_goldsrc_pipeline_runtime_register_bytes(cache.count) ==
           sizeof(gpu_registers));

    const GoldSrcRenderState state = {
        GOLDSRC_BLEND_ALPHA_TEST, GOLDSRC_CULL_BACK,
        1u, 1u, 1u, 0u,
    };
    Ps5GoldSrcPipelineBinding binding;
    assert(ps5_goldsrc_pipeline_runtime_bind(
        &runtime, &state, 1u, &binding) == 0);
    assert(binding.permutation->shader == GOLDSRC_SHADER_MASKED_LIGHTMAP_FOG);
    assert(binding.pipeline ==
           &pipelines[GOLDSRC_SHADER_MASKED_LIGHTMAP_FOG][1]);
    assert(binding.draw_modifier ==
           0x1000u + GOLDSRC_SHADER_MASKED_LIGHTMAP_FOG);
    assert(binding.dynamic_cx[0].offset ==
           PS5_GOLDSRC_CB_BLEND0_CONTROL);
    assert(binding.dynamic_cx[1].offset ==
           PS5_GOLDSRC_DB_DEPTH_CONTROL);
    assert(binding.dynamic_cx[2].offset ==
           PS5_GOLDSRC_PA_SU_SC_MODE_CNTL);
    assert(binding.dynamic_cx != binding.permutation->dynamic_cx);
    assert(memcmp(binding.dynamic_cx, binding.permutation->dynamic_cx,
                  PS5_GOLDSRC_RENDER_REGISTER_COUNT *
                      sizeof(ps5_agc_register)) == 0);

    /* A native world/clear pipeline must explicitly overwrite inherited HUD
     * blending. The reset source remains GPU-visible and immutable. */
    GoldSrcRenderState hud;
    const GoldSrcRenderState opaque = {
        GOLDSRC_BLEND_OPAQUE, GOLDSRC_CULL_NONE, 1u, 0u, 0u, 0u,
    };
    const GoldSrcBlendMode previous[] = {
        GOLDSRC_BLEND_ALPHA, GOLDSRC_BLEND_ADDITIVE,
        GOLDSRC_BLEND_SCREEN_MODULATE,
    };
    for (unsigned i = 0; i < 3u; ++i) {
        assert(goldsrc_render_state_2d(previous[i], &hud) == 0);
        assert(ps5_goldsrc_pipeline_runtime_bind(&runtime, &hud, 1u, &binding) == 0);
        const ps5_agc_register *hud_registers = binding.dynamic_cx;
        const uint32_t previous_value = hud_registers[0].value;
        assert(previous_value != 0u);
        assert(ps5_goldsrc_pipeline_runtime_bind(&runtime, &opaque, 0u, &binding) == 0);
        assert(binding.dynamic_cx >= gpu_registers &&
               binding.dynamic_cx < gpu_registers + sizeof(gpu_registers)/sizeof(*gpu_registers));
        assert(binding.dynamic_cx[0].offset == PS5_GOLDSRC_CB_BLEND0_CONTROL);
        assert(binding.dynamic_cx[0].value == 0u);
        assert(hud_registers[0].value == previous_value);
    }
    assert(ps5_goldsrc_pipeline_runtime_bind(
        &runtime, &state, 2u, &binding) != 0);
    assert(ps5_goldsrc_pipeline_runtime_init(
        &runtime, &cache, slots, GOLDSRC_SHADER_VARIANT_COUNT,
        gpu_registers, sizeof(gpu_registers) - 1u,
        gpu_registers, sizeof(gpu_registers)) != 0);
    puts("PS5 GoldSrc pipeline runtime passed: GPU-visible state and binding");
    return 0;
}
