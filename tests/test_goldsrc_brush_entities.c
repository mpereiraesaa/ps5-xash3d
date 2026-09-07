#include "../src/goldsrc_brush_entities.h"
#include "../src/bsp_texture_descriptor.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

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
    assert(cursor && capacity >= 6u && count == 3u && indices && mapping);
    assert(mapping_bytes == 262144u && modifier == UINT64_C(0x1234));
    *cursor += 6u;
    return 0;
}

int main(void)
{
    _Alignas(256) uint8_t memory[262144];
    memset(memory, 0, sizeof(memory));
    BspBundleVertex *vertices = (BspBundleVertex *)memory;
    uint16_t *indices = (uint16_t *)(memory + 256u);
    BspBundleDraw *draws = (BspBundleDraw *)(memory + 512u);
    BspBundleTexture *textures = (BspBundleTexture *)(memory + 1024u);
    BspBundleBrushEntity *entities =
        (BspBundleBrushEntity *)(memory + 1280u);
    for (uint32_t index = 0u; index < 15u; ++index)
        indices[index] = (uint16_t)(index % 3u);
    for (uint32_t index = 0u; index < 5u; ++index) {
        draws[index].first_index = index * 3u;
        draws[index].index_count = 3u;
        draws[index].face_id = 10u + index;
        draws[index].base_texture = index == 4u ? 1u : 0u;
        entities[index].model_index = index + 1u;
        entities[index].first_face = 10u + index;
        entities[index].face_count = 1u;
        entities[index].render_mode =
            (uint32_t[]){0u, 2u, 5u, 2u, 0u}[index];
        entities[index].mins[0] = entities[index].mins[1] =
            entities[index].mins[2] = -1.0f;
        entities[index].maxs[0] = entities[index].maxs[1] =
            entities[index].maxs[2] = 1.0f;
        entities[index].classname_hash = 100u + index;
    }
    entities[3].classname_hash = UINT32_C(0xd5807c07);
    textures[0].name_hash = 1u;
    textures[1].name_hash = UINT32_C(0x10944ba2);
    BspBundleView bundle = {
        .vertices = vertices, .vertex_count = 3u,
        .indices = indices, .index_count = 15u,
        .draws = draws, .draw_count = 5u,
        .textures = textures, .texture_count = 2u,
        .brush_entities = entities, .brush_entity_count = 5u,
    };
    GoldSrcBrushPlan plan;
    assert(goldsrc_brush_plan_build(&plan, &bundle) == 0);
    for (uint32_t index = 0u; index < 3u; ++index) {
        assert(plan.entity_indices[index] == index);
        assert(plan.draw_counts[index] == 1u);
        assert(plan.index_counts[index] == 3u);
    }
    assert(plan.instance_count == GOLDSRC_BRUSH_INSTANCE_COUNT);
    assert(goldsrc_brush_phase4_scene_plan_extend(&plan, &bundle) == 0);
    assert(plan.instance_count == GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT);
    assert(plan.entity_indices[GOLDSRC_BRUSH_INSTANCE_WATER] == 3u);
    assert(plan.entity_indices[GOLDSRC_BRUSH_INSTANCE_GLASS] == 4u);
    assert(plan.draw_counts[GOLDSRC_BRUSH_INSTANCE_WATER] == 1u);
    assert(plan.draw_counts[GOLDSRC_BRUSH_INSTANCE_GLASS] == 1u);
    Ps5TransientRing ring;
    assert(ps5_transient_ring_init(&ring, memory + 4096u,
                                   sizeof(memory) - 4096u, 2u, 256u) ==
           PS5_TRANSIENT_OK);
    assert(ps5_transient_ring_begin(&ring, 0u, 0u, 0) == PS5_TRANSIENT_OK);
    const float camera[3] = {0.0f, 0.0f, 0.0f};
    const float forward[3] = {0.0f, 0.0f, -1.0f};
    GoldSrcBrushFrame frame;
    assert(goldsrc_brush_frame_build(
        &frame, &plan, &bundle, &ring, 0u, memory, sizeof(memory),
        camera, forward, 16.0f / 9.0f, 0u) == 0);
    assert(frame.mode == GOLDSRC_BRUSH_MODE_CONTROL);
    assert(frame.transform_hash != 0u && frame.transient_bytes > 0u);
    BspResourceFrame resource = {
        .map_vertex_table = (const uint32_t *)(memory + 2048u),
        .texture_tables = (const uint32_t *)(memory + 2304u),
        .texture_table_dwords = BSP_TEXTURE_TABLE_DWORDS,
    };
    uint32_t commands[32] = {0};
    uint32_t *cursor = commands;
    GoldSrcBrushComposeResult composed = {0};
    assert(goldsrc_brush_compose_instance(
        &cursor, commands + 32u, &frame, &plan, &resource, &bundle,
        GOLDSRC_BRUSH_INSTANCE_ALPHA, memory, sizeof(memory),
        UINT64_C(0x1234), set_sh, draw, &composed) == 0);
    assert(composed.instances == 1u && composed.draws == 1u &&
           composed.indices == 3u && composed.texture_binds == 1u &&
           composed.command_dwords == 13u && cursor == commands + 13u);
    assert(ps5_transient_ring_abort_unsubmitted(&ring, 0u) ==
           PS5_TRANSIENT_OK);

    assert(goldsrc_brush_mode(600u) == GOLDSRC_BRUSH_MODE_OPAQUE);
    assert(goldsrc_brush_mode(1200u) == GOLDSRC_BRUSH_MODE_ALPHA);
    assert(goldsrc_brush_mode(1800u) == GOLDSRC_BRUSH_MODE_ADDITIVE);
    assert(goldsrc_brush_mode(2400u) == GOLDSRC_BRUSH_MODE_COMBINED);
    assert(strcmp(goldsrc_brush_mode_name(GOLDSRC_BRUSH_MODE_COMBINED),
                  "combined") == 0);
    return 0;
}
