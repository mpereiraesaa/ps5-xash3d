#include "goldsrc_2d.h"

#include "bsp_resource_draw.h"
#include "bsp_texture_descriptor.h"
#include "ps5_gfx1013_descriptor.h"
#include "ps5_gpu_span.h"
#include "ps5_transient_table.h"

#include <string.h>

_Static_assert(sizeof(GoldSrc2DVertex) == 32u, "screen vertex ABI");
_Static_assert(sizeof(GoldSrc2DConstants) ==
                   GOLDSRC_2D_CONSTANT_DWORDS * sizeof(uint32_t),
               "screen constant ABI");

typedef struct GoldSrc2DBuilder {
    GoldSrc2DVertex *vertices;
    uint16_t *indices;
    uint32_t quads;
    uint64_t layout_hash;
} GoldSrc2DBuilder;

static uint64_t hash_bytes(const void *data, size_t bytes)
{
    const uint8_t *cursor = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0u; i < bytes; ++i) {
        hash ^= cursor[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void glyph_rows(unsigned char glyph, uint8_t rows[7])
{
    static const struct Glyph {
        unsigned char glyph;
        uint8_t rows[7];
    } font[] = {
        {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
        {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
        {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
        {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
        {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
        {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
        {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
        {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
        {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
        {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,2,18,12}},
        {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
        {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
        {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
        {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
        {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
        {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
        {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
        {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
        {'-',{0,0,0,31,0,0,0}}, {':',{0,4,4,0,4,4,0}},
        {'.',{0,0,0,0,0,12,12}}, {'/',{1,2,2,4,8,8,16}},
        {'?',{14,17,1,2,4,0,4}},
    };
    memset(rows, 0, 7u);
    if (glyph == ' ')
        return;
    for (size_t i = 0u; i < sizeof(font) / sizeof(font[0]); ++i)
        if (font[i].glyph == glyph) {
            memcpy(rows, font[i].rows, 7u);
            return;
        }
    memcpy(rows, font[sizeof(font) / sizeof(font[0]) - 1u].rows, 7u);
}

static void build_atlas(uint8_t *pixels)
{
    memset(pixels, 0,
           (size_t)GOLDSRC_2D_ATLAS_ROW_PITCH * GOLDSRC_2D_ATLAS_HEIGHT);
    for (unsigned glyph = 32u; glyph < 96u; ++glyph) {
        uint8_t rows[7];
        glyph_rows((unsigned char)glyph, rows);
        const unsigned cell = glyph - 32u;
        const unsigned origin_x = (cell & 15u) * 8u;
        const unsigned origin_y = (cell >> 4u) * 8u;
        for (unsigned y = 0u; y < 7u; ++y)
            for (unsigned x = 0u; x < 5u; ++x)
                if (rows[y] & (1u << (4u - x))) {
                    uint8_t *pixel = pixels +
                        (origin_y + y) * GOLDSRC_2D_ATLAS_ROW_PITCH +
                        (origin_x + x + 1u) * 4u;
                    pixel[0] = pixel[1] = pixel[2] = pixel[3] = 255u;
                }
    }
    uint8_t *solid = pixels +
        (GOLDSRC_2D_ATLAS_HEIGHT - 1u) * GOLDSRC_2D_ATLAS_ROW_PITCH +
        (GOLDSRC_2D_ATLAS_WIDTH - 1u) * 4u;
    solid[0] = solid[1] = solid[2] = solid[3] = 255u;
}

static int add_quad(GoldSrc2DBuilder *builder, float x, float y,
                    float width, float height, float u0, float v0,
                    float u1, float v1, const float color[4])
{
    if (!builder || !builder->vertices || !builder->indices || !color ||
        width <= 0.0f || height <= 0.0f ||
        builder->quads >= GOLDSRC_2D_MAX_QUADS)
        return -1;
    const uint32_t vertex = builder->quads * 4u;
    const uint32_t index = builder->quads * 6u;
    const float positions[4][2] = {
        {x, y}, {x + width, y}, {x + width, y + height}, {x, y + height},
    };
    const float uv[4][2] = {{u0,v0},{u1,v0},{u1,v1},{u0,v1}};
    for (uint32_t i = 0u; i < 4u; ++i) {
        memcpy(builder->vertices[vertex + i].position, positions[i],
               sizeof(positions[i]));
        memcpy(builder->vertices[vertex + i].uv, uv[i], sizeof(uv[i]));
        memcpy(builder->vertices[vertex + i].color, color,
               4u * sizeof(float));
    }
    const uint16_t quad_indices[6] = {
        (uint16_t)vertex, (uint16_t)(vertex + 1u),
        (uint16_t)(vertex + 2u), (uint16_t)vertex,
        (uint16_t)(vertex + 2u), (uint16_t)(vertex + 3u),
    };
    memcpy(builder->indices + index, quad_indices, sizeof(quad_indices));
    ++builder->quads;
    builder->layout_hash = hash_bytes(builder->vertices,
        (size_t)builder->quads * 4u * sizeof(GoldSrc2DVertex));
    return 0;
}

static int add_solid(GoldSrc2DBuilder *builder, float x, float y,
                     float width, float height, const float color[4])
{
    const float u = 127.5f / (float)GOLDSRC_2D_ATLAS_WIDTH;
    const float v = 31.5f / (float)GOLDSRC_2D_ATLAS_HEIGHT;
    return add_quad(builder, x, y, width, height, u, v, u, v, color);
}

static int add_text(GoldSrc2DBuilder *builder, float x, float y,
                    float scale, const char *text, const float color[4],
                    uint32_t *glyphs)
{
    if (!text || !glyphs || scale <= 0.0f)
        return -1;
    for (; *text; ++text) {
        unsigned glyph = (unsigned char)*text;
        if (glyph < 32u || glyph >= 96u)
            glyph = '?';
        if (glyph != ' ') {
            const unsigned cell = glyph - 32u;
            const float u0 = (float)((cell & 15u) * 8u) /
                (float)GOLDSRC_2D_ATLAS_WIDTH;
            const float v0 = (float)((cell >> 4u) * 8u) /
                (float)GOLDSRC_2D_ATLAS_HEIGHT;
            const float u1 = u0 + 8.0f / (float)GOLDSRC_2D_ATLAS_WIDTH;
            const float v1 = v0 + 8.0f / (float)GOLDSRC_2D_ATLAS_HEIGHT;
            if (add_quad(builder, x, y, 8.0f * scale, 8.0f * scale,
                         u0, v0, u1, v1, color) != 0)
                return -1;
            ++*glyphs;
        }
        x += 7.0f * scale;
    }
    return 0;
}

static int allocate_slice(Ps5TransientRing *ring, uint32_t slot,
                          size_t bytes, size_t alignment,
                          const void *mapping, size_t mapping_bytes,
                          Ps5TransientSlice *out)
{
    return ps5_transient_ring_allocate(ring, slot, bytes, alignment, out) ==
                   PS5_TRANSIENT_OK &&
               ps5_gpu_span_visible(mapping, mapping_bytes,
                                    out->cpu, out->bytes)
           ? 0 : -1;
}

int goldsrc_2d_frame_build(
    GoldSrc2DFrame *out, Ps5TransientRing *ring, uint32_t slot_index,
    const void *gpu_mapping, size_t gpu_mapping_bytes,
    uint32_t framebuffer_width, uint32_t framebuffer_height,
    uint64_t frame_index)
{
    if (!out || !ring || slot_index >= ring->slot_count || !gpu_mapping ||
        gpu_mapping_bytes == 0u || framebuffer_width < 640u ||
        framebuffer_height < 360u)
        return -1;
    const size_t checkpoint = ring->slots[slot_index].used;
    memset(out, 0, sizeof(*out));
    Ps5TransientSlice atlas_slice, constants_slice, vertices_slice;
    Ps5TransientSlice indices_slice;
    Ps5TransientTable constant_table, vertex_table, texture_table;
    const size_t atlas_bytes =
        (size_t)GOLDSRC_2D_ATLAS_ROW_PITCH * GOLDSRC_2D_ATLAS_HEIGHT;
    if (allocate_slice(ring, slot_index, atlas_bytes, 256u,
                       gpu_mapping, gpu_mapping_bytes, &atlas_slice) != 0 ||
        allocate_slice(ring, slot_index, sizeof(GoldSrc2DConstants), 256u,
                       gpu_mapping, gpu_mapping_bytes,
                       &constants_slice) != 0 ||
        allocate_slice(ring, slot_index,
                       GOLDSRC_2D_MAX_QUADS * 4u *
                           sizeof(GoldSrc2DVertex), 16u,
                       gpu_mapping, gpu_mapping_bytes, &vertices_slice) != 0 ||
        allocate_slice(ring, slot_index,
                       GOLDSRC_2D_MAX_QUADS * 6u * sizeof(uint16_t), 2u,
                       gpu_mapping, gpu_mapping_bytes, &indices_slice) != 0 ||
        ps5_transient_table_allocate(ring, slot_index,
                                     PS5_GFX1013_VSHARP_DWORDS,
                                     gpu_mapping, gpu_mapping_bytes,
                                     &constant_table) != 0 ||
        ps5_transient_table_allocate(ring, slot_index,
                                     PS5_GFX1013_VSHARP_DWORDS,
                                     gpu_mapping, gpu_mapping_bytes,
                                     &vertex_table) != 0 ||
        ps5_transient_table_allocate(ring, slot_index,
                                     BSP_GFX1013_COMBINED_DWORDS,
                                     gpu_mapping, gpu_mapping_bytes,
                                     &texture_table) != 0) {
        ring->slots[slot_index].used = checkpoint;
        return -2;
    }

    build_atlas(atlas_slice.cpu);
    GoldSrc2DConstants *constants = constants_slice.cpu;
    memset(constants, 0, sizeof(*constants));
    constants->projection[0] = 2.0f / (float)framebuffer_width;
    constants->projection[5] = -2.0f / (float)framebuffer_height;
    constants->projection[10] = 1.0f;
    constants->projection[12] = -1.0f;
    constants->projection[13] = 1.0f;
    constants->projection[15] = 1.0f;
    constants->color_scale[0] = constants->color_scale[1] =
        constants->color_scale[2] = constants->color_scale[3] = 1.0f;
    constants->debug_values[0] = (float)(frame_index & UINT64_C(0xffff));
    constants->debug_values[1] = (float)slot_index;
    if (ps5_gfx1013_build_constant_vsharp(
            constant_table.words, (uintptr_t)constants,
            sizeof(*constants)) != 0 ||
        bsp_gfx1013_combined_descriptor(
            texture_table.words, (uintptr_t)atlas_slice.cpu,
            GOLDSRC_2D_ATLAS_WIDTH, GOLDSRC_2D_ATLAS_HEIGHT,
            GOLDSRC_2D_ATLAS_ROW_PITCH, 1u,
            BSP_TEXTURE_CLAMP_LAST_TEXEL, BSP_TEXTURE_FILTER_POINT) != 0) {
        ring->slots[slot_index].used = checkpoint;
        return -3;
    }

    GoldSrc2DBuilder builder = {
        vertices_slice.cpu, indices_slice.cpu, 0u,
        UINT64_C(14695981039346656037),
    };
    const float sx = (float)framebuffer_width / 1920.0f;
    const float sy = (float)framebuffer_height / 1080.0f;
    const float dark[4] = {0.015f, 0.025f, 0.020f, 0.78f};
    const float green[4] = {0.18f, 0.95f, 0.40f, 0.95f};
    const float pale[4] = {0.78f, 0.94f, 0.82f, 0.95f};
    const float amber[4] = {1.0f, 0.68f, 0.18f, 0.92f};
    const float menu_bg[4] = {0.035f, 0.055f, 0.045f, 0.86f};
    const float hud_bg[4] = {0.02f, 0.03f, 0.025f, 0.70f};
    uint32_t glyphs = 0u;

    if (add_solid(&builder, 48.0f*sx, 44.0f*sy,
                  960.0f*sx, 360.0f*sy, dark) != 0 ||
        add_solid(&builder, 48.0f*sx, 44.0f*sy,
                  960.0f*sx, 10.0f*sy, green) != 0)
        goto exhausted;
    out->console_quads = 2u;
    if (add_solid(&builder, 1500.0f*sx, 210.0f*sy,
                  360.0f*sx, 500.0f*sy, menu_bg) != 0 ||
        add_solid(&builder, 1500.0f*sx, 210.0f*sy,
                  8.0f*sx, 500.0f*sy, green) != 0 ||
        add_solid(&builder, 1530.0f*sx, 352.0f*sy,
                  285.0f*sx, 48.0f*sy, amber) != 0)
        goto exhausted;
    out->menu_quads = 3u;
    if (add_solid(&builder, 54.0f*sx, 936.0f*sy,
                  440.0f*sx, 92.0f*sy, hud_bg) != 0 ||
        add_solid(&builder, 76.0f*sx, 998.0f*sy,
                  250.0f*sx, 12.0f*sy, green) != 0)
        goto exhausted;
    out->hud_quads = 2u;
    if (add_text(&builder, 76.0f*sx, 78.0f*sy, 2.0f*sy,
                 "XASH3D CONSOLE", green, &glyphs) != 0 ||
        add_text(&builder, 76.0f*sx, 126.0f*sy, 1.5f*sy,
                 "STATUS READY", pale, &glyphs) != 0 ||
        add_text(&builder, 76.0f*sx, 160.0f*sy, 1.5f*sy,
                 "RENDERER GFX1013", pale, &glyphs) != 0 ||
        add_text(&builder, 1540.0f*sx, 248.0f*sy, 2.0f*sy,
                 "MENU", green, &glyphs) != 0 ||
        add_text(&builder, 1550.0f*sx, 322.0f*sy, 1.5f*sy,
                 "NEW GAME", pale, &glyphs) != 0 ||
        add_text(&builder, 1550.0f*sx, 430.0f*sy, 1.5f*sy,
                 "OPTIONS", pale, &glyphs) != 0 ||
        add_text(&builder, 1550.0f*sx, 490.0f*sy, 1.5f*sy,
                 "QUIT", pale, &glyphs) != 0 ||
        add_text(&builder, 76.0f*sx, 954.0f*sy, 1.5f*sy,
                 "HEALTH 100  ARMOR 042", amber, &glyphs) != 0)
        goto exhausted;
    out->font_quads = glyphs;
    out->alpha_first_index = 0u;
    out->alpha_index_count = builder.quads * 6u;

    const float glow[4] = {0.20f, 1.0f, 0.45f, 0.80f};
    if (add_solid(&builder, 947.0f*sx, 539.0f*sy,
                  26.0f*sx, 2.0f*sy, glow) != 0 ||
        add_solid(&builder, 959.0f*sx, 527.0f*sy,
                  2.0f*sx, 26.0f*sy, glow) != 0)
        goto exhausted;
    out->hud_quads += 2u;
    out->additive_first_index = out->alpha_index_count;
    out->additive_index_count = 12u;
    out->vertex_count = builder.quads * 4u;
    if (ps5_gfx1013_build_vsharp(
            vertex_table.words, (uintptr_t)builder.vertices,
            sizeof(GoldSrc2DVertex), out->vertex_count) != 0)
        goto exhausted;
    out->constant_table = constant_table.words;
    out->vertex_table = vertex_table.words;
    out->texture_table = texture_table.words;
    out->vertices = builder.vertices;
    out->indices = builder.indices;
    out->atlas_hash = hash_bytes(atlas_slice.cpu, atlas_bytes);
    out->layout_hash = builder.layout_hash;
    out->transient_bytes = ring->slots[slot_index].used - checkpoint;
    return 0;

exhausted:
    ring->slots[slot_index].used = checkpoint;
    memset(out, 0, sizeof(*out));
    return -4;
}

int goldsrc_2d_compose_range(
    uint32_t **cursor, uint32_t *end, const GoldSrc2DFrame *frame,
    uint32_t first_index, uint32_t index_count,
    const void *gpu_mapping, size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    GoldSrc2DComposeResult *result)
{
    enum { SET_TWO_DWORDS = 4, SET_ONE_DWORDS = 3, DRAW_DWORDS = 6,
           REQUIRED_DWORDS = SET_TWO_DWORDS + SET_ONE_DWORDS + DRAW_DWORDS };
    if (!cursor || !*cursor || !end || *cursor > end || !frame ||
        !gpu_mapping || !gpu_mapping_bytes || !modifier || !set_sh_direct ||
        !draw_indexed || !result || index_count == 0u ||
        first_index > frame->alpha_index_count + frame->additive_index_count ||
        index_count > frame->alpha_index_count + frame->additive_index_count -
                          first_index ||
        (size_t)(end - *cursor) < REQUIRED_DWORDS ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              frame->constant_table, 4u*sizeof(uint32_t)) ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              frame->vertex_table, 4u*sizeof(uint32_t)) ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              frame->texture_table,
                              BSP_GFX1013_COMBINED_DWORDS*sizeof(uint32_t)) ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              frame->indices + first_index,
                              (size_t)index_count*sizeof(uint16_t)))
        return -1;
    uint32_t *const start = *cursor;
    const uint32_t gs[2] = {
        (uint32_t)(uintptr_t)frame->constant_table,
        (uint32_t)(uintptr_t)frame->vertex_table,
    };
    const uint32_t ps = (uint32_t)(uintptr_t)frame->texture_table;
    if (set_sh_direct(cursor, (uint32_t)(end - *cursor),
                      BSP_RESOURCE_GS_SH_OFFSET, gs, 2u) != 0 ||
        set_sh_direct(cursor, (uint32_t)(end - *cursor),
                      BSP_RESOURCE_PS_SH_OFFSET, &ps, 1u) != 0 ||
        draw_indexed(cursor, (uint32_t)(end - *cursor), index_count,
                     frame->indices + first_index, gpu_mapping,
                     gpu_mapping_bytes, modifier) != 0)
        return -2;
    const uint32_t written = (uint32_t)(*cursor - start);
    if (written != REQUIRED_DWORDS)
        return -3;
    ++result->draws;
    result->indices += index_count;
    result->command_dwords += written;
    return 0;
}
