#ifndef PS5_XASH3D_PS5_GOLDSRC_PIPELINE_RUNTIME_H
#define PS5_XASH3D_PS5_GOLDSRC_PIPELINE_RUNTIME_H

#include "goldsrc_pipeline_cache.h"
#include "ps5_shader_pipeline_slot.h"

typedef struct Ps5GoldSrcPipelineRuntime {
    const GoldSrcPipelineCache *cache;
    const Ps5ShaderPipelineSlotResult *shader_slots;
    ps5_agc_register *dynamic_cx;
    uint32_t state_count;
} Ps5GoldSrcPipelineRuntime;

typedef struct Ps5GoldSrcPipelineBinding {
    const GoldSrcPipelinePermutation *permutation;
    const struct ps5_pipeline_registers *pipeline;
    const ps5_agc_register *dynamic_cx;
    uint64_t draw_modifier;
} Ps5GoldSrcPipelineBinding;

size_t ps5_goldsrc_pipeline_runtime_register_bytes(uint32_t state_count);

int ps5_goldsrc_pipeline_runtime_init(
    Ps5GoldSrcPipelineRuntime *out, const GoldSrcPipelineCache *cache,
    const Ps5ShaderPipelineSlotResult *shader_slots,
    uint32_t shader_slot_count, void *gpu_register_storage,
    size_t gpu_register_storage_bytes, const void *gpu_mapping,
    size_t gpu_mapping_bytes);

int ps5_goldsrc_pipeline_runtime_bind(
    const Ps5GoldSrcPipelineRuntime *runtime,
    const GoldSrcRenderState *state, uint32_t framebuffer_slot,
    Ps5GoldSrcPipelineBinding *out);

#endif
