#include "ref_agc_live_2d.h"
#include "ref_agc_2d_state.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void flush_memory(const void *memory, size_t bytes, void *user)
{
    (void)memory;
    (void)bytes;
    (void)user;
}

static int set_sh(uint32_t **cursor, uint32_t capacity, uint32_t offset,
                  const uint32_t *values, uint32_t count)
{
    assert(cursor && values && capacity >= count + 2u);
    assert(offset == 0x8du || offset == 0x0du);
    *cursor += count + 2u;
    return 0;
}

static int draw(uint32_t **cursor, uint32_t capacity, uint32_t count,
                const uint16_t *indices, const void *mapping,
                size_t mapping_bytes, uint64_t modifier)
{
    assert(cursor && capacity >= 6u && count != 0u && indices && mapping);
    assert(mapping_bytes == 1048576u && modifier == UINT64_C(0x4567));
    *cursor += 6u;
    return 0;
}

static RefAgcLive2DCommand mode(uint32_t enabled)
{
    RefAgcLive2DCommand command = {0};
    command.type = REF_AGC_LIVE_2D_MODE;
    command.enabled = enabled;
    return command;
}

static RefAgcLive2DCommand stretch(float x, uint8_t alpha)
{
    RefAgcLive2DCommand command = {0};
    command.type = REF_AGC_LIVE_2D_STRETCH_PIC;
    command.render_mode = GOLDSRC_RENDER_TRANS_TEXTURE;
    command.texture = 1;
    command.x = x;
    command.y = 20.0f;
    command.width = 64.0f;
    command.height = 32.0f;
    command.s2 = command.t2 = 1.0f;
    command.color[0] = 128u;
    command.color[1] = 192u;
    command.color[2] = 255u;
    command.color[3] = alpha;
    return command;
}

static RefAgcLive2DCommand fill(float y, int32_t render_mode)
{
    RefAgcLive2DCommand command = {0};
    command.type = REF_AGC_LIVE_2D_FILL_RGBA;
    command.render_mode = render_mode;
    command.x = 10.0f;
    command.y = y;
    command.width = 100.0f;
    command.height = 8.0f;
    command.color[0] = 20u;
    command.color[1] = 40u;
    command.color[2] = 60u;
    command.color[3] = 128u;
    return command;
}

int main(void)
{
    _Alignas(256) static uint8_t memory[1048576];
    _Alignas(256) uint8_t texture_arena[4096];
    uint8_t pixels[4u * 4u * 4u];
    memset(memory, 0, sizeof(memory));
    memset(texture_arena, 0, sizeof(texture_arena));
    memset(pixels, 0xa5, sizeof(pixels));

    RefAgcGpuTextureCache textures;
    RefAgcTextureView texture = {
        .handle = 1u,
        .width = 4u,
        .height = 4u,
        .depth = 1u,
        .mip_count = 1u,
        .revision = 1u,
        .content_hash = UINT64_C(0x1234),
        .pixel_bytes = sizeof(pixels),
        .pixels = pixels,
        .active = 1,
    };
    assert(ref_agc_gpu_texture_cache_init(
        &textures, texture_arena, UINT64_C(0x200000000),
        sizeof(texture_arena), flush_memory, NULL) == 0);
    assert(ref_agc_gpu_texture_cache_apply(&textures, &texture, 1) == 0);

    Ps5TransientRing ring;
    assert(ps5_transient_ring_init(&ring, memory, sizeof(memory),
                                   2u, 256u) == 0);
    assert(ps5_transient_ring_begin(&ring, 0u, 0u, 0) == 0);
    RefAgcLiveFrame live = {0};
    live.canvas_width = 640u;
    live.canvas_height = 480u;
    live.commands_2d[live.command_2d_count++] = mode(0u);
    live.commands_2d[live.command_2d_count++] = mode(1u);
    live.commands_2d[live.command_2d_count++] = stretch(12.0f, 128u);
    live.commands_2d[live.command_2d_count++] = stretch(80.0f, 255u);
    live.commands_2d[live.command_2d_count++] =
        fill(70.0f, GOLDSRC_RENDER_TRANS_TEXTURE);
    live.commands_2d[live.command_2d_count++] =
        fill(90.0f, GOLDSRC_RENDER_TRANS_ADD);

    RefAgcLive2DFrame frame;
    assert(ref_agc_live_2d_frame_build(
        &frame, &ring, 0u, memory, sizeof(memory), 1920u, 1080u,
        &live, &textures) == REF_AGC_LIVE_2D_OK);
    assert(frame.mode_commands == 2u && frame.stretch_quads == 2u &&
           frame.fill_quads == 2u);
    assert(frame.vertex_count == 16u && frame.index_count == 24u);
    assert(frame.batch_count == 3u && frame.alpha_batches == 2u &&
           frame.additive_batches == 1u && frame.opaque_batches == 0u);
    assert(frame.batches[0].texture_handle == 1u &&
           frame.batches[0].index_count == 12u &&
           frame.batches[1].fill == 1u &&
           frame.batches[2].blend == GOLDSRC_BLEND_ADDITIVE);
    assert(frame.vertices[0].position[0] == 12.0f &&
           frame.vertices[0].color[3] > 0.50f &&
           frame.vertices[0].color[3] < 0.51f);
    assert(frame.command_hash != 0u && frame.layout_hash != 0u &&
           frame.transient_bytes != 0u && frame.unresolved_textures == 0u);
    const GoldSrc2DConstants *constants = (const GoldSrc2DConstants *)memory;
    assert(fabsf(constants->projection[0] - 2.0f / 640.0f) < 0.000001f);
    assert(fabsf(constants->projection[5] + 2.0f / 480.0f) < 0.000001f);

    uint32_t commands[128] = {0};
    uint32_t *cursor = commands;
    RefAgcLive2DComposeResult composed = {0};
    for (uint32_t batch = 0u; batch < frame.batch_count; ++batch)
        assert(ref_agc_live_2d_compose_batch(
            &cursor, commands + 128u, &frame, batch, memory,
            sizeof(memory), UINT64_C(0x4567), set_sh, draw,
            &composed) == REF_AGC_LIVE_2D_OK);
    assert(composed.draws == 3u && composed.indices == 24u &&
           composed.command_dwords == 39u && composed.texture_binds == 3u);

    assert(ps5_transient_ring_abort_unsubmitted(&ring, 0u) == 0);
    assert(ps5_transient_ring_begin(&ring, 0u, 0u, 0) == 0);
    const int32_t modes[] = {0, 1, 2, 3, 4, 5,
                            REF_AGC_2D_SCREEN_FADE_MODULATE, 0};
    const GoldSrcBlendMode blends[] = {
        GOLDSRC_BLEND_OPAQUE, GOLDSRC_BLEND_ALPHA, GOLDSRC_BLEND_ALPHA,
        GOLDSRC_BLEND_ADDITIVE, GOLDSRC_BLEND_ALPHA_TEST,
        GOLDSRC_BLEND_ADDITIVE, GOLDSRC_BLEND_SCREEN_MODULATE,
        GOLDSRC_BLEND_OPAQUE,
    };
    RefAgcLiveFrame ordered = {0};
    ordered.commands_2d[ordered.command_2d_count++] = mode(1u);
    for (unsigned i = 0; i < 8u; ++i) {
        RefAgcLive2DCommand pic = stretch((float)i * 10.0f, 128u);
        pic.render_mode = modes[i];
        ordered.commands_2d[ordered.command_2d_count++] = pic;
    }
    assert(ref_agc_live_2d_frame_build(
        &frame, &ring, 0u, memory, sizeof(memory), 1920u, 1080u,
        &ordered, &textures) == REF_AGC_LIVE_2D_OK);
    assert(frame.batch_count == 7u && frame.opaque_batches == 2u &&
           frame.alpha_batches == 1u && frame.additive_batches == 2u &&
           frame.masked_batches == 1u && frame.modulate_batches == 1u);
    for (unsigned i = 0; i < 8u; ++i) {
        const unsigned batch = i < 2u ? i : i - 1u;
        assert(frame.batches[batch].blend == blends[i]);
        assert(frame.vertices[i * 4u].position[0] == (float)i * 10.0f);
    }
    assert(frame.batches[1].index_count == 12u);
    assert(frame.batches[6].first_index == 42u);
    const size_t used_before_invalid = ring.slots[0].used;
    ordered.commands_2d[1].render_mode = 6;
    assert(ref_agc_live_2d_frame_build(
        &frame, &ring, 0u, memory, sizeof(memory), 1920u, 1080u,
        &ordered, &textures) == REF_AGC_LIVE_2D_SEQUENCE_INVALID);
    assert(ring.slots[0].used == used_before_invalid);

    /* FillRGBA's mode argument is not GL_SetRenderMode: only TransAdd adds. */
    for (unsigned i = 0; i < 8u; ++i) {
        assert(ps5_transient_ring_abort_unsubmitted(&ring, 0u) == 0);
        assert(ps5_transient_ring_begin(&ring, 0u, 0u, 0) == 0);
        RefAgcLiveFrame filled = {0};
        filled.commands_2d[filled.command_2d_count++] = mode(1u);
        filled.commands_2d[filled.command_2d_count++] = fill(10.0f, modes[i]);
        assert(ref_agc_live_2d_frame_build(
            &frame, &ring, 0u, memory, sizeof(memory), 1920u, 1080u,
            &filled, &textures) == REF_AGC_LIVE_2D_OK);
        assert(frame.batches[0].blend == (modes[i] == 5 ?
            GOLDSRC_BLEND_ADDITIVE : GOLDSRC_BLEND_ALPHA));
    }

    assert(ps5_transient_ring_abort_unsubmitted(&ring, 0u) == 0);
    assert(ps5_transient_ring_begin(&ring, 0u, 0u, 0) == 0);
    RefAgcLiveFrame invalid = {0};
    invalid.commands_2d[invalid.command_2d_count++] = stretch(0.0f, 255u);
    assert(ref_agc_live_2d_frame_build(
        &frame, &ring, 0u, memory, sizeof(memory), 1920u, 1080u,
        &invalid, &textures) == REF_AGC_LIVE_2D_SEQUENCE_INVALID);

    invalid = (RefAgcLiveFrame){0};
    invalid.commands_2d[invalid.command_2d_count++] = mode(1u);
    RefAgcLive2DCommand missing = stretch(0.0f, 255u);
    missing.texture = 2;
    invalid.commands_2d[invalid.command_2d_count++] = missing;
    assert(ref_agc_live_2d_frame_build(
        &frame, &ring, 0u, memory, sizeof(memory), 1920u, 1080u,
        &invalid, &textures) == REF_AGC_LIVE_2D_TEXTURE_UNRESOLVED);

    invalid = (RefAgcLiveFrame){0};
    invalid.commands_2d[invalid.command_2d_count++] = mode(0u);
    invalid.commands_2d[invalid.command_2d_count++] = mode(1u);
    assert(ref_agc_live_2d_frame_build(
        &frame, &ring, 0u, memory, sizeof(memory), 1920u, 1080u,
        &invalid, &textures) == REF_AGC_LIVE_2D_OK);
    assert(frame.mode_commands == 2u && frame.batch_count == 0u &&
           frame.transient_bytes == 0u);

    _Alignas(256) static uint8_t capacity_memory[2097152];
    static RefAgcLiveFrame capacity_live;
    static RefAgcLive2DFrame capacity_frame;
    Ps5TransientRing capacity_ring;
    memset(capacity_memory, 0, sizeof(capacity_memory));
    memset(&capacity_live, 0, sizeof(capacity_live));
    assert(ps5_transient_ring_init(
        &capacity_ring, capacity_memory, sizeof(capacity_memory),
        2u, 256u) == 0);
    assert(ps5_transient_ring_begin(&capacity_ring, 0u, 0u, 0) == 0);
    capacity_live.commands_2d[capacity_live.command_2d_count++] = mode(1u);
    while (capacity_live.command_2d_count < REF_AGC_LIVE_MAX_2D_COMMANDS) {
        const uint32_t index = capacity_live.command_2d_count;
        capacity_live.commands_2d[capacity_live.command_2d_count++] =
            (index & 1u) ? stretch((float)index, 255u)
                         : fill((float)index,
                                GOLDSRC_RENDER_TRANS_TEXTURE);
    }
    assert(ref_agc_live_2d_frame_build(
        &capacity_frame, &capacity_ring, 0u, capacity_memory,
        sizeof(capacity_memory), 1920u, 1080u,
        &capacity_live, &textures) == REF_AGC_LIVE_2D_OK);
    assert(capacity_frame.vertex_count ==
           (REF_AGC_LIVE_MAX_2D_COMMANDS - 1u) * 4u);
    assert(capacity_frame.batch_count ==
           REF_AGC_LIVE_MAX_2D_COMMANDS - 1u);
    assert(capacity_frame.transient_bytes < capacity_ring.slots[0].bytes);

    ref_agc_gpu_texture_cache_destroy(&textures);
    puts("ref_agc live 2D tests passed");
    return 0;
}
