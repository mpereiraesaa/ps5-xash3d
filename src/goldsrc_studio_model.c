#include "goldsrc_studio_model.h"

#include "bsp_flat_scene.h"
#include "bsp_texture_descriptor.h"
#include "ps5_gfx1013_descriptor.h"
#include "ps5_gpu_span.h"
#include "ps5_transient_table.h"

#include <math.h>
#include <string.h>

_Static_assert(sizeof(BspBundleVertex) == 32u, "surface vertex ABI");
_Static_assert(sizeof(GoldSrcStudioConstants) ==
                   GOLDSRC_STUDIO_CONSTANT_DWORDS * sizeof(uint32_t),
               "studio constant ABI");

typedef struct StudioVec3 { float x, y, z; } StudioVec3;
typedef struct StudioMatrix { float m[3][4]; } StudioMatrix;

static uint64_t hash_bytes(const void *data, size_t bytes)
{
    const uint8_t *cursor = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0u; index < bytes; ++index) {
        hash ^= cursor[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static float dot(StudioVec3 a, StudioVec3 b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static StudioVec3 add(StudioVec3 a, StudioVec3 b)
{
    return (StudioVec3){a.x+b.x, a.y+b.y, a.z+b.z};
}

static StudioVec3 scale(StudioVec3 value, float amount)
{
    return (StudioVec3){value.x*amount, value.y*amount, value.z*amount};
}

static StudioVec3 cross(StudioVec3 a, StudioVec3 b)
{
    return (StudioVec3){
        a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x,
    };
}

static int normalize(StudioVec3 *value)
{
    const float length = sqrtf(dot(*value, *value));
    if (!(length > 0.000001f) || !__builtin_isfinite(length))
        return -1;
    *value = scale(*value, 1.0f/length);
    return 0;
}

static int camera_basis(const float position[3], const float forward_in[3],
                        StudioVec3 *camera, StudioVec3 *forward,
                        StudioVec3 *right, StudioVec3 *up)
{
    if (!position || !forward_in || !camera || !forward || !right || !up)
        return -1;
    *camera = (StudioVec3){position[0], position[1], position[2]};
    *forward = (StudioVec3){forward_in[0], forward_in[1], forward_in[2]};
    if (normalize(forward) != 0)
        return -1;
    StudioVec3 world_up = {0.0f, 1.0f, 0.0f};
    if (fabsf(dot(*forward, world_up)) > 0.999f)
        world_up = (StudioVec3){0.0f, 0.0f, 1.0f};
    *right = cross(*forward, world_up);
    if (normalize(right) != 0)
        return -1;
    *up = cross(*right, *forward);
    return 0;
}

static void quaternion_lerp(const float left[4], const float right[4],
                            float fraction, float out[4])
{
    float sign = 1.0f;
    float product = 0.0f;
    for (unsigned component = 0u; component < 4u; ++component)
        product += left[component] * right[component];
    if (product < 0.0f)
        sign = -1.0f;
    float length_squared = 0.0f;
    for (unsigned component = 0u; component < 4u; ++component) {
        out[component] = left[component] * (1.0f-fraction) +
                         right[component] * fraction * sign;
        length_squared += out[component] * out[component];
    }
    const float inverse = 1.0f/sqrtf(length_squared);
    for (unsigned component = 0u; component < 4u; ++component)
        out[component] *= inverse;
}

static StudioMatrix pose_matrix(const GoldSrcStudioPose *left,
                                const GoldSrcStudioPose *right,
                                float fraction)
{
    float q[4];
    quaternion_lerp(left->quaternion, right->quaternion, fraction, q);
    const float x=q[0], y=q[1], z=q[2], w=q[3];
    const float px = left->position[0]*(1.0f-fraction) +
                     right->position[0]*fraction;
    const float py = left->position[1]*(1.0f-fraction) +
                     right->position[1]*fraction;
    const float pz = left->position[2]*(1.0f-fraction) +
                     right->position[2]*fraction;
    return (StudioMatrix){{
        {1.0f-2.0f*(y*y+z*z), 2.0f*(x*y-z*w),
         2.0f*(x*z+y*w), px},
        {2.0f*(x*y+z*w), 1.0f-2.0f*(x*x+z*z),
         2.0f*(y*z-x*w), py},
        {2.0f*(x*z-y*w), 2.0f*(y*z+x*w),
         1.0f-2.0f*(x*x+y*y), pz},
    }};
}

static StudioMatrix matrix_multiply(StudioMatrix left, StudioMatrix right)
{
    StudioMatrix out = {{{0}}};
    for (unsigned row = 0u; row < 3u; ++row) {
        for (unsigned column = 0u; column < 3u; ++column)
            for (unsigned inner = 0u; inner < 3u; ++inner)
                out.m[row][column] +=
                    left.m[row][inner]*right.m[inner][column];
        out.m[row][3] = left.m[row][3];
        for (unsigned inner = 0u; inner < 3u; ++inner)
            out.m[row][3] += left.m[row][inner]*right.m[inner][3];
    }
    return out;
}

static StudioVec3 transform_point(StudioMatrix matrix, const float point[3])
{
    return (StudioVec3){
        matrix.m[0][0]*point[0]+matrix.m[0][1]*point[1]+
            matrix.m[0][2]*point[2]+matrix.m[0][3],
        matrix.m[1][0]*point[0]+matrix.m[1][1]*point[1]+
            matrix.m[1][2]*point[2]+matrix.m[1][3],
        matrix.m[2][0]*point[0]+matrix.m[2][1]*point[1]+
            matrix.m[2][2]*point[2]+matrix.m[2][3],
    };
}

static StudioVec3 rotate_vector(StudioMatrix matrix, const float vector[3])
{
    return (StudioVec3){
        matrix.m[0][0]*vector[0]+matrix.m[0][1]*vector[1]+
            matrix.m[0][2]*vector[2],
        matrix.m[1][0]*vector[0]+matrix.m[1][1]*vector[1]+
            matrix.m[1][2]*vector[2],
        matrix.m[2][0]*vector[0]+matrix.m[2][1]*vector[1]+
            matrix.m[2][2]*vector[2],
    };
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

GoldSrcStudioMode goldsrc_studio_mode(uint64_t frame_index)
{
    return (GoldSrcStudioMode)((frame_index/GOLDSRC_STUDIO_HOLD_FRAMES) %
                               GOLDSRC_STUDIO_MODE_COUNT);
}

const char *goldsrc_studio_mode_name(GoldSrcStudioMode mode)
{
    static const char *const names[GOLDSRC_STUDIO_MODE_COUNT] = {
        "control", "textured", "chrome", "additive", "combined",
    };
    return (unsigned)mode < GOLDSRC_STUDIO_MODE_COUNT ? names[mode] :
                                                       "invalid";
}

int goldsrc_studio_frame_build(
    GoldSrcStudioFrame *out, const GoldSrcStudioBundleView *bundle,
    Ps5TransientRing *ring, uint32_t slot_index,
    const void *gpu_mapping, size_t gpu_mapping_bytes,
    const float camera_position[3], const float camera_forward[3],
    float aspect_ratio, uint64_t frame_index)
{
    if (!out || !bundle || !bundle->header || !ring ||
        slot_index >= ring->slot_count || !gpu_mapping ||
        gpu_mapping_bytes == 0u || !camera_position || !camera_forward ||
        !(aspect_ratio > 0.0f) || !__builtin_isfinite(aspect_ratio))
        return -1;
    const GoldSrcStudioBundleHeader *header = bundle->header;
    if (header->vertex_count > UINT16_MAX/GOLDSRC_STUDIO_INSTANCE_COUNT)
        return -1;
    const size_t checkpoint = ring->slots[slot_index].used;
    memset(out, 0, sizeof(*out));
    Ps5TransientSlice constants_slice, vertices_slice, indices_slice;
    Ps5TransientTable constant_tables[GOLDSRC_STUDIO_INSTANCE_COUNT];
    Ps5TransientTable vertex_table, texture_tables;
    const uint32_t total_vertices =
        header->vertex_count*GOLDSRC_STUDIO_INSTANCE_COUNT;
    const uint32_t total_indices =
        header->index_count*GOLDSRC_STUDIO_INSTANCE_COUNT;
    if (allocate_slice(ring, slot_index,
                       GOLDSRC_STUDIO_CONSTANT_STRIDE*
                           GOLDSRC_STUDIO_INSTANCE_COUNT,
                       256u, gpu_mapping, gpu_mapping_bytes,
                       &constants_slice) != 0 ||
        allocate_slice(ring, slot_index,
                       (size_t)total_vertices*sizeof(BspBundleVertex), 16u,
                       gpu_mapping, gpu_mapping_bytes, &vertices_slice) != 0 ||
        allocate_slice(ring, slot_index,
                       (size_t)total_indices*sizeof(uint16_t), 2u,
                       gpu_mapping, gpu_mapping_bytes, &indices_slice) != 0 ||
        ps5_transient_table_allocate(
            ring, slot_index, PS5_GFX1013_VSHARP_DWORDS,
            gpu_mapping, gpu_mapping_bytes, &vertex_table) != 0 ||
        ps5_transient_table_allocate(
            ring, slot_index,
            header->texture_count*BSP_TEXTURE_TABLE_DWORDS,
            gpu_mapping, gpu_mapping_bytes, &texture_tables) != 0)
        goto exhausted;
    for (uint32_t instance = 0u;
         instance < GOLDSRC_STUDIO_INSTANCE_COUNT; ++instance)
        if (ps5_transient_table_allocate(
                ring, slot_index, PS5_GFX1013_VSHARP_DWORDS,
                gpu_mapping, gpu_mapping_bytes,
                &constant_tables[instance]) != 0)
            goto exhausted;

    const uint32_t cycle_frames =
        header->sequence_flags & GOLDSRC_STUDIO_SEQUENCE_LOOPING &&
                header->frame_count > 2u
            ? header->frame_count - 1u : header->frame_count;
    const float animation = fmodf((float)(frame_index % UINT64_C(360000)) *
                                      header->fps/60.0f,
                                  (float)cycle_frames);
    const uint32_t frame0 = (uint32_t)animation;
    const uint32_t frame1 = (frame0 + 1u) % cycle_frames;
    const float blend = animation - (float)frame0;
    StudioMatrix matrices[GOLDSRC_STUDIO_MAX_BONES];
    for (uint32_t bone = 0u; bone < header->bone_count; ++bone) {
        StudioMatrix local = pose_matrix(
            &bundle->poses[(size_t)frame0*header->bone_count+bone],
            &bundle->poses[(size_t)frame1*header->bone_count+bone], blend);
        const int16_t parent = bundle->parents[bone];
        matrices[bone] = parent < 0 ? local :
            matrix_multiply(matrices[(uint32_t)parent], local);
    }
    const uint64_t pose_hash = hash_bytes(
        matrices, (size_t)header->bone_count*sizeof(*matrices));

    StudioVec3 camera, forward, right, up;
    if (camera_basis(camera_position, camera_forward, &camera, &forward,
                     &right, &up) != 0)
        goto exhausted;
    const StudioVec3 source_center = {
        (header->bounds_min[0]+header->bounds_max[0])*0.5f,
        (header->bounds_min[1]+header->bounds_max[1])*0.5f,
        (header->bounds_min[2]+header->bounds_max[2])*0.5f,
    };
    const float extent_x = header->bounds_max[0]-header->bounds_min[0];
    const float extent_y = header->bounds_max[1]-header->bounds_min[1];
    const float extent_z = header->bounds_max[2]-header->bounds_min[2];
    float maximum_extent = extent_x > extent_y ? extent_x : extent_y;
    if (extent_z > maximum_extent)
        maximum_extent = extent_z;
    if (!(maximum_extent > 0.0001f))
        goto exhausted;
    const float model_scale = 30.0f/maximum_extent;
    BspBundleVertex *vertices = vertices_slice.cpu;
    uint16_t *indices = indices_slice.cpu;
    static const float offsets[GOLDSRC_STUDIO_INSTANCE_COUNT] = {
        -34.0f, 0.0f, 34.0f,
    };
    for (uint32_t instance = 0u;
         instance < GOLDSRC_STUDIO_INSTANCE_COUNT; ++instance) {
        StudioVec3 center = add(camera, scale(forward, 92.0f));
        center = add(center, scale(right, offsets[instance]));
        for (uint32_t vertex = 0u; vertex < header->vertex_count; ++vertex) {
            const GoldSrcStudioSourceVertex *source = &bundle->vertices[vertex];
            StudioVec3 point = transform_point(matrices[source->bone],
                                               source->position);
            StudioVec3 normal = rotate_vector(
                matrices[source->normal_bone], source->normal);
            if (normalize(&normal) != 0)
                goto exhausted;
            point.x = (point.x-source_center.x)*model_scale;
            point.y = (point.y-source_center.y)*model_scale;
            point.z = (point.z-source_center.z)*model_scale;
            StudioVec3 world = center;
            world = add(world, scale(right, point.y));
            world = add(world, scale(up, point.z));
            world = add(world, scale(forward, -point.x));
            StudioVec3 world_normal = add(
                add(scale(right, normal.y), scale(up, normal.z)),
                scale(forward, -normal.x));
            BspBundleVertex *target =
                &vertices[instance*header->vertex_count+vertex];
            memset(target, 0, sizeof(*target));
            target->position[0]=world.x;
            target->position[1]=world.y;
            target->position[2]=world.z;
            if (instance == GOLDSRC_STUDIO_INSTANCE_CHROME) {
                target->base_uv[0] = 0.5f + 0.5f*dot(world_normal, right);
                target->base_uv[1] = 0.5f - 0.5f*dot(world_normal, up);
            } else {
                target->base_uv[0]=source->uv[0];
                target->base_uv[1]=source->uv[1];
            }
            target->light_uv[0]=target->light_uv[1]=0.5f;
            target->face_id=instance;
        }
        for (uint32_t index = 0u; index < header->index_count; ++index)
            indices[instance*header->index_count+index] = (uint16_t)(
                bundle->indices[index] + instance*header->vertex_count);
    }

    GoldSrcStudioConstants *constants = constants_slice.cpu;
    static const float colors[GOLDSRC_STUDIO_INSTANCE_COUNT][4] = {
        {1.0f,1.0f,1.0f,1.0f}, {0.42f,0.82f,1.0f,1.0f},
        {1.0f,0.34f,0.08f,0.68f},
    };
    for (uint32_t instance = 0u;
         instance < GOLDSRC_STUDIO_INSTANCE_COUNT; ++instance) {
        GoldSrcStudioConstants *item = (GoldSrcStudioConstants *)(
            (uint8_t *)constants +
            instance*GOLDSRC_STUDIO_CONSTANT_STRIDE);
        memset(item, 0, sizeof(*item));
        if (bsp_flat_camera_matrix(item->mvp, camera_position,
                                   camera_forward, aspect_ratio) != 0)
            goto exhausted;
        memcpy(item->render_color, colors[instance],
               sizeof(item->render_color));
        item->debug_values[0]=(float)frame0;
        item->debug_values[1]=blend;
        if (ps5_gfx1013_build_constant_vsharp(
                constant_tables[instance].words, (uintptr_t)item,
                sizeof(*item)) != 0)
            goto exhausted;
    }
    if (ps5_gfx1013_build_vsharp(
            vertex_table.words, (uintptr_t)vertices,
            sizeof(BspBundleVertex), total_vertices) != 0)
        goto exhausted;
    for (uint32_t texture = 0u; texture < header->texture_count; ++texture) {
        const GoldSrcStudioTexture *source = &bundle->textures[texture];
        const uintptr_t address = (uintptr_t)(bundle->pixels+source->offset);
        uint32_t *table = texture_tables.words+
            texture*BSP_TEXTURE_TABLE_DWORDS;
        if (!ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                                  (const void *)address, source->bytes) ||
            bsp_gfx1013_combined_descriptor(
                table, address, source->width, source->height,
                source->row_pitch, 1u, BSP_TEXTURE_REPEAT,
                BSP_TEXTURE_FILTER_BILINEAR) != 0 ||
            bsp_gfx1013_combined_descriptor(
                table+BSP_GFX1013_COMBINED_DWORDS, address,
                source->width, source->height, source->row_pitch, 1u,
                BSP_TEXTURE_REPEAT, BSP_TEXTURE_FILTER_BILINEAR) != 0)
            goto exhausted;
    }

    for (uint32_t instance = 0u;
         instance < GOLDSRC_STUDIO_INSTANCE_COUNT; ++instance)
        out->constant_tables[instance] = constant_tables[instance].words;
    out->vertex_table=vertex_table.words;
    out->texture_tables=texture_tables.words;
    out->vertices=vertices;
    out->indices=indices;
    out->vertices_per_instance=header->vertex_count;
    out->indices_per_instance=header->index_count;
    out->draw_count=header->draw_count;
    out->texture_count=header->texture_count;
    out->frame0=frame0;
    out->frame1=frame1;
    out->blend=blend;
    out->mode=goldsrc_studio_mode(frame_index);
    out->pose_hash=pose_hash;
    out->skinned_hash=hash_bytes(
        vertices, (size_t)total_vertices*sizeof(*vertices));
    out->transient_bytes=ring->slots[slot_index].used-checkpoint;
    return 0;

exhausted:
    ring->slots[slot_index].used=checkpoint;
    memset(out, 0, sizeof(*out));
    return -2;
}

int goldsrc_studio_compose_instance(
    uint32_t **cursor, uint32_t *end, const GoldSrcStudioFrame *frame,
    const GoldSrcStudioBundleView *bundle, GoldSrcStudioInstance instance,
    const void *gpu_mapping, size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    GoldSrcStudioComposeResult *result)
{
    enum { SET_TWO_DWORDS=4, SET_ONE_DWORDS=3, DRAW_DWORDS=6,
           PREFIX_DWORDS=SET_TWO_DWORDS };
    if (!cursor || !*cursor || !end || *cursor > end || !frame ||
        !bundle || !bundle->header ||
        (unsigned)instance >= GOLDSRC_STUDIO_INSTANCE_COUNT ||
        !gpu_mapping || gpu_mapping_bytes == 0u || modifier == 0u ||
        !set_sh_direct || !draw_indexed || !result ||
        frame->draw_count != bundle->header->draw_count ||
        frame->texture_count != bundle->header->texture_count ||
        (size_t)(end-*cursor) < PREFIX_DWORDS+
            frame->draw_count*(SET_ONE_DWORDS+DRAW_DWORDS) ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              frame->constant_tables[instance], 16u) ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              frame->vertex_table, 16u) ||
        !ps5_gpu_span_visible(
            gpu_mapping, gpu_mapping_bytes, frame->texture_tables,
            (size_t)frame->texture_count*BSP_TEXTURE_TABLE_DWORDS*
                sizeof(uint32_t)))
        return -1;
    uint32_t *start=*cursor;
    const uint32_t gs[2] = {
        (uint32_t)(uintptr_t)frame->constant_tables[instance],
        (uint32_t)(uintptr_t)frame->vertex_table,
    };
    if (set_sh_direct(cursor, (uint32_t)(end-*cursor),
                      BSP_RESOURCE_GS_SH_OFFSET, gs, 2u) != 0)
        return -2;
    const uint32_t index_base=(uint32_t)instance*frame->indices_per_instance;
    for (uint32_t draw=0u; draw<frame->draw_count; ++draw) {
        const GoldSrcStudioDraw *source=&bundle->draws[draw];
        const uint32_t ps=(uint32_t)(uintptr_t)(frame->texture_tables+
            source->texture*BSP_TEXTURE_TABLE_DWORDS);
        const uint16_t *indices=frame->indices+index_base+source->first_index;
        if (!ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes, indices,
                                  (size_t)source->index_count*
                                      sizeof(*indices)) ||
            set_sh_direct(cursor, (uint32_t)(end-*cursor),
                          BSP_RESOURCE_PS_SH_OFFSET, &ps, 1u) != 0 ||
            draw_indexed(cursor, (uint32_t)(end-*cursor),
                         source->index_count, indices, gpu_mapping,
                         gpu_mapping_bytes, modifier) != 0)
            return -3;
        ++result->draws;
        result->indices+=source->index_count;
        ++result->textures;
    }
    const uint32_t written=(uint32_t)(*cursor-start);
    result->command_dwords+=written;
    return written == PREFIX_DWORDS+
        frame->draw_count*(SET_ONE_DWORDS+DRAW_DWORDS) ? 0 : -4;
}
