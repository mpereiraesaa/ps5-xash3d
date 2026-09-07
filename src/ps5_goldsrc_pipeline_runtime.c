#include "ps5_goldsrc_pipeline_runtime.h"

#include "ps5_gpu_span.h"

#include <limits.h>
#include <string.h>

size_t ps5_goldsrc_pipeline_runtime_register_bytes(uint32_t state_count)
{
    const size_t stride = PS5_GOLDSRC_RENDER_REGISTER_COUNT *
                          sizeof(ps5_agc_register);
    return state_count > SIZE_MAX / stride ? 0u : (size_t)state_count * stride;
}

int ps5_goldsrc_pipeline_runtime_init(
    Ps5GoldSrcPipelineRuntime *out, const GoldSrcPipelineCache *cache,
    const Ps5ShaderPipelineSlotResult *shader_slots,
    uint32_t shader_slot_count, void *gpu_register_storage,
    size_t gpu_register_storage_bytes, const void *gpu_mapping,
    size_t gpu_mapping_bytes)
{
    if (!out || !cache || !shader_slots ||
        shader_slot_count != GOLDSRC_SHADER_VARIANT_COUNT ||
        cache->count != GOLDSRC_PIPELINE_PERMUTATION_COUNT ||
        !gpu_register_storage || !gpu_mapping || !gpu_mapping_bytes)
        return -1;
    const size_t required =
        ps5_goldsrc_pipeline_runtime_register_bytes(cache->count);
    if (!required || required > gpu_register_storage_bytes ||
        ((uintptr_t)gpu_register_storage & 7u) != 0u ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              gpu_register_storage, required))
        return -2;
    for (uint32_t variant = 0; variant < shader_slot_count; ++variant)
        if (!shader_slots[variant].pipelines[0] ||
            !shader_slots[variant].pipelines[1] ||
            !shader_slots[variant].draw_modifier)
            return -3;

    memset(out, 0, sizeof(*out));
    out->cache = cache;
    out->shader_slots = shader_slots;
    out->dynamic_cx = gpu_register_storage;
    out->state_count = cache->count;
    for (uint32_t index = 0; index < cache->count; ++index)
        memcpy(out->dynamic_cx +
                   index * PS5_GOLDSRC_RENDER_REGISTER_COUNT,
               cache->entries[index].dynamic_cx,
               PS5_GOLDSRC_RENDER_REGISTER_COUNT * sizeof(ps5_agc_register));
    return 0;
}

int ps5_goldsrc_pipeline_runtime_bind(
    const Ps5GoldSrcPipelineRuntime *runtime,
    const GoldSrcRenderState *state, uint32_t framebuffer_slot,
    Ps5GoldSrcPipelineBinding *out)
{
    if (!runtime || !runtime->cache || !runtime->shader_slots ||
        !runtime->dynamic_cx ||
        runtime->state_count != runtime->cache->count ||
        framebuffer_slot >= 2u || !out)
        return -1;
    const GoldSrcPipelinePermutation *const permutation =
        goldsrc_pipeline_cache_find(runtime->cache, state);
    if (!permutation || permutation->shader >= GOLDSRC_SHADER_VARIANT_COUNT)
        return -2;
    const ptrdiff_t index = permutation - runtime->cache->entries;
    if (index < 0 || (uint32_t)index >= runtime->state_count)
        return -2;
    const Ps5ShaderPipelineSlotResult *const shader =
        &runtime->shader_slots[permutation->shader];
    if (!shader->pipelines[framebuffer_slot] || !shader->draw_modifier)
        return -3;
    out->permutation = permutation;
    out->pipeline = shader->pipelines[framebuffer_slot];
    out->dynamic_cx = runtime->dynamic_cx +
        (uint32_t)index * PS5_GOLDSRC_RENDER_REGISTER_COUNT;
    out->draw_modifier = shader->draw_modifier;
    return 0;
}
