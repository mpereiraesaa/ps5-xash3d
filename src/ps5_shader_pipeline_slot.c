#include "ps5_shader_pipeline_slot.h"

#include <stddef.h>
#include <string.h>

int ps5_shader_pipeline_slot_build(
    void *slot, size_t slot_bytes,
    const Ps5EmbeddedShaderPair *embedded,
    const ps5_agc_register color_targets[2][PS5_PIPELINE_RT_REGISTERS],
    uint32_t width, uint32_t height,
    Ps5CreateShaderFn create_shader, Ps5LinkShadersFn link_shaders,
    Ps5ShaderPipelineSlotResult *out)
{
    const size_t gs_capacity = PS5_SHADER_PIPELINE_PS_CODE_OFFSET -
                               PS5_SHADER_PIPELINE_GS_CODE_OFFSET;
    const size_t ps_capacity = PS5_SHADER_PIPELINE_LINKED_CX_OFFSET -
                               PS5_SHADER_PIPELINE_PS_CODE_OFFSET;
    if (!slot || slot_bytes < PS5_SHADER_PIPELINE_SLOT_BYTES || !embedded ||
        !embedded->gs || !embedded->ps || !embedded->metadata ||
        !embedded->gs_bytes || !embedded->ps_bytes ||
        embedded->gs_bytes > gs_capacity - PS5_SHADER_PIPELINE_FOOTER_BYTES ||
        embedded->ps_bytes > ps_capacity - PS5_SHADER_PIPELINE_FOOTER_BYTES ||
        !color_targets || !width || !height || !create_shader ||
        !link_shaders || !out)
        return -1;

    memset(slot, 0, PS5_SHADER_PIPELINE_SLOT_BYTES);
    uint8_t *const base = slot;
    struct ps5_shader_arena *const gs_arena =
        (struct ps5_shader_arena *)(base +
            PS5_SHADER_PIPELINE_GS_HEADER_OFFSET);
    struct ps5_shader_arena *const ps_arena =
        (struct ps5_shader_arena *)(base +
            PS5_SHADER_PIPELINE_PS_HEADER_OFFSET);
    uint8_t *const gs_code = base + PS5_SHADER_PIPELINE_GS_CODE_OFFSET;
    uint8_t *const ps_code = base + PS5_SHADER_PIPELINE_PS_CODE_OFFSET;
    const uint32_t gs_size = (uint32_t)embedded->gs_bytes +
                             PS5_SHADER_PIPELINE_FOOTER_BYTES;
    const uint32_t ps_size = (uint32_t)embedded->ps_bytes +
                             PS5_SHADER_PIPELINE_FOOTER_BYTES;
    if (ps5_shader_header_build(gs_arena, PS5_SHADER_PRE_RASTER, gs_size,
                                embedded->metadata) != 0 ||
        ps5_shader_header_build(ps_arena, PS5_SHADER_PIXEL, ps_size,
                                embedded->metadata) != 0)
        return -2;
    memcpy(gs_code, embedded->gs, embedded->gs_bytes);
    memcpy(ps_code, embedded->ps, embedded->ps_bytes);
    memcpy(gs_code + gs_size - PS5_SHADER_PIPELINE_FOOTER_BYTES,
           "barefoot", 8u);
    memcpy(ps_code + ps_size - PS5_SHADER_PIPELINE_FOOTER_BYTES,
           "barefoot", 8u);

    void *gs_object = NULL;
    void *ps_object = NULL;
    if (create_shader(&gs_object, gs_arena, gs_code) != 0 ||
        gs_object != gs_arena)
        return -3;
    if (create_shader(&ps_object, ps_arena, ps_code) != 0 ||
        ps_object != ps_arena)
        return -4;
    struct ps5_agc_linked_cx *const linked_cx =
        (struct ps5_agc_linked_cx *)(base +
            PS5_SHADER_PIPELINE_LINKED_CX_OFFSET);
    struct ps5_agc_linked_uc *const linked_uc =
        (struct ps5_agc_linked_uc *)(base +
            PS5_SHADER_PIPELINE_LINKED_UC_OFFSET);
    if (link_shaders(linked_cx, linked_uc, NULL, gs_object, ps_object, 4u) != 0)
        return -5;

    struct ps5_pipeline_registers *const pipelines =
        (struct ps5_pipeline_registers *)(base +
            PS5_SHADER_PIPELINE_REGISTERS_OFFSET);
    for (unsigned i = 0; i < 2; ++i)
        if (ps5_pipeline_build(
                &pipelines[i], color_targets[i], linked_cx, linked_uc,
                gs_arena->cx, ps_arena->cx, gs_arena->sh, ps_arena->sh,
                width, height) != 0)
            return -6;
    out->pipelines[0] = &pipelines[0];
    out->pipelines[1] = &pipelines[1];
    out->draw_modifier = embedded->metadata->draw_modifier;
    return 0;
}
