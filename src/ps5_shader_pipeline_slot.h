#ifndef PS5_XASH3D_PS5_SHADER_PIPELINE_SLOT_H
#define PS5_XASH3D_PS5_SHADER_PIPELINE_SLOT_H

#include "ps5_pipeline.h"
#include "ps5_shader_header.h"

enum {
    PS5_SHADER_PIPELINE_SLOT_BYTES = 0x4000,
    PS5_SHADER_PIPELINE_GS_HEADER_OFFSET = 0x0000,
    PS5_SHADER_PIPELINE_PS_HEADER_OFFSET = 0x0200,
    PS5_SHADER_PIPELINE_GS_CODE_OFFSET = 0x1000,
    PS5_SHADER_PIPELINE_PS_CODE_OFFSET = 0x1400,
    PS5_SHADER_PIPELINE_LINKED_CX_OFFSET = 0x2000,
    PS5_SHADER_PIPELINE_LINKED_UC_OFFSET = 0x2200,
    PS5_SHADER_PIPELINE_REGISTERS_OFFSET = 0x2800,
    PS5_SHADER_PIPELINE_FOOTER_BYTES = 0x30,
};

typedef struct Ps5EmbeddedShaderPair {
    const uint8_t *gs;
    size_t gs_bytes;
    const uint8_t *ps;
    size_t ps_bytes;
    const struct ps5_shader_metadata *metadata;
} Ps5EmbeddedShaderPair;

typedef int32_t (*Ps5CreateShaderFn)(void **shader, void *header, void *code);
typedef int32_t (*Ps5LinkShadersFn)(void *cx, void *uc, void *reserved,
                                    void *pre_raster, void *pixel,
                                    uint32_t primitive);

typedef struct Ps5ShaderPipelineSlotResult {
    struct ps5_pipeline_registers *pipelines[2];
    uint64_t draw_modifier;
} Ps5ShaderPipelineSlotResult;

int ps5_shader_pipeline_slot_build(
    void *slot, size_t slot_bytes,
    const Ps5EmbeddedShaderPair *embedded,
    const ps5_agc_register color_targets[2][PS5_PIPELINE_RT_REGISTERS],
    uint32_t width, uint32_t height,
    Ps5CreateShaderFn create_shader, Ps5LinkShadersFn link_shaders,
    Ps5ShaderPipelineSlotResult *out);

#endif
