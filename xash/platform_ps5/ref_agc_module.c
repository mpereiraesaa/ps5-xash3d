/*
ref_agc_module.c - Xash3D RefAPI v18 adapter for the native PS5 AGC backend
Copyright (C) 2026 Manuel Pereira
SPDX-License-Identifier: GPL-3.0-or-later

The Phase 4 renderer was deliberately proven as a standalone owner before it
was admitted into the engine.  This adapter keeps that ownership boundary
inside ref_agc.prx and exposes the complete RefAPI surface expected by Xash.
The null renderer supplies harmless defaults for callbacks that do not yet
translate an engine model into a native draw; the lifecycle and frame boundary
callbacks below are always the project-owned AGC implementation.
*/

#include <pthread.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ref_agc_live_frame.h"
#include "ref_agc_lightmap_atlas.h"
#include "ref_agc_texture_store.h"
#include "ref_agc_world_store.h"

/* Reuse the upstream ABI-complete callback skeleton without modifying the
 * pinned submodule.  Its public entry point is renamed and wrapped below. */
#define GetRefAPI PS5_RefAgcNullGetRefAPI
#include "../../third_party/xash3d-fwgs/ref/null/r_context.c"
#undef GetRefAPI
#include "ref_params.h"

int ps5_ref_agc_native_main(void);

enum
{
	REF_AGC_IDLE = 0,
	REF_AGC_STARTING = 1,
	REF_AGC_READY = 2,
	REF_AGC_COMPLETE = 3,
	REF_AGC_FAILED = 4,
	REF_AGC_JOINED = 5,
};

static ref_api_t ref_agc_engine;
static volatile int ref_agc_runtime_state;
static volatile int ref_agc_runtime_result;
static volatile int ref_agc_teardown_result;
static volatile uint64_t ref_agc_runtime_frames;
static volatile uint64_t ref_agc_frame_hash;
static volatile uint64_t ref_agc_bright_pixels;
static volatile uint64_t ref_agc_begin_calls;
static volatile uint64_t ref_agc_scene_calls;
static volatile uint64_t ref_agc_end_calls;
static volatile uint64_t ref_agc_newmap_calls;
static volatile uint64_t ref_agc_live_frames;
static volatile uint64_t ref_agc_live_view_frames;
static volatile uint64_t ref_agc_live_view_hash;
static volatile uint64_t ref_agc_live_view_changes;
static volatile uint64_t ref_agc_live_map_serial;
static volatile uint64_t ref_agc_live_world_surfaces;
static volatile uint64_t ref_agc_live_entity_peak;
static volatile uint64_t ref_agc_live_2d_peak;
static volatile uint64_t ref_agc_live_dropped_entities;
static volatile uint64_t ref_agc_live_dropped_2d;
static volatile uint64_t ref_agc_consumed_frames;
static volatile uint64_t ref_agc_consumed_serial;
static volatile uint64_t ref_agc_consumed_view_frames;
static volatile uint64_t ref_agc_consumed_camera_hash;
static volatile uint64_t ref_agc_consumed_camera_changes;
static volatile uint64_t ref_agc_texture_revision;
static volatile uint64_t ref_agc_texture_creates;
static volatile uint64_t ref_agc_texture_updates;
static volatile uint64_t ref_agc_texture_frees;
static volatile uint64_t ref_agc_texture_peak_bytes;
static volatile uint64_t ref_agc_texture_handles;
static volatile uint64_t ref_agc_texture_peak_active;
static volatile uint64_t ref_agc_world_texture_refs;
static volatile uint64_t ref_agc_world_textures_resolved;
static pthread_t ref_agc_thread;
static int ref_agc_thread_created;
static RefAgcLiveStore ref_agc_live;
static int ref_agc_live_initialized;
static RefAgcTextureStore ref_agc_textures;
static int ref_agc_textures_initialized;
static RefAgcWorldStore ref_agc_world;
static int ref_agc_world_initialized;
static poolhandle_t ref_agc_storage_pool;

static void *RefAgcStorageAlloc(size_t bytes, void *unused)
{
	(void)unused;
	return ref_agc_engine._Mem_Alloc( ref_agc_storage_pool, bytes,
		false, __FILE__, __LINE__ );
}

static void RefAgcStorageFree(void *memory, void *unused)
{
	(void)unused;
	if( memory )
		ref_agc_engine._Mem_Free( memory, __FILE__, __LINE__ );
}

static int RefAgcStoragePoolInit(void)
{
	if( ref_agc_storage_pool ) return 0;
	if( !ref_agc_engine._Mem_AllocPool || !ref_agc_engine._Mem_FreePool ||
		!ref_agc_engine._Mem_Alloc || !ref_agc_engine._Mem_Free )
		return -1;
	ref_agc_storage_pool = ref_agc_engine._Mem_AllocPool(
		"RefAGC CPU store", 0u, __FILE__, __LINE__ );
	return ref_agc_storage_pool ? 0 : -1;
}

static void RefAgcStoragePoolDestroy(void)
{
	if( ref_agc_storage_pool )
		ref_agc_engine._Mem_FreePool( &ref_agc_storage_pool,
			__FILE__, __LINE__ );
}

static void RefAgcCopy3(float out[3], const float in[3])
{
	memcpy( out, in, 3u * sizeof(float) );
}

static uint64_t RefAgcHashBytes(const void *data, size_t bytes)
{
	const unsigned char *cursor = (const unsigned char *)data;
	uint64_t hash = UINT64_C(14695981039346656037);
	while( bytes-- )
	{
		hash ^= *cursor++;
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

static int RefAgcBrushVertexIndex(const model_t *model, int surfedge,
	uint32_t *out)
{
	int edge_number;
	uint32_t vertex;
	if( !model || !out || surfedge < 0 || surfedge >= model->numsurfedges ||
		!model->surfedges || !model->vertexes )
		return -1;
	edge_number = model->surfedges[surfedge];
	if( edge_number == INT_MIN || edge_number >= model->numedges ||
		edge_number <= -model->numedges )
		return -1;
	if( model->flags & MODEL_QBSP2 )
	{
		const medge32_t *edge = &model->edges32[
			edge_number < 0 ? -edge_number : edge_number];
		vertex = edge->v[edge_number < 0 ? 1 : 0];
	}
	else
	{
		const medge16_t *edge = &model->edges16[
			edge_number < 0 ? -edge_number : edge_number];
		vertex = edge->v[edge_number < 0 ? 1 : 0];
	}
	if( vertex >= (uint32_t)model->numvertexes )
		return -1;
	*out = vertex;
	return 0;
}

static uint32_t RefAgcLightStyleScale(const lightstyle_t *styles,
	uint8_t style)
{
	if( !styles || style >= 255u || styles[style].length <= 0 )
		return 256u;
	if( styles[style].map[0] <= 0.0f )
		return 0u;
	return (uint32_t)(styles[style].map[0] * 22.0f);
}

static int RefAgcBuildSurfaceLightmap(const msurface_t *surface,
	uint32_t width, uint32_t height, uint8_t *rgba)
{
	const lightstyle_t *styles = ref_agc_engine.EngineGetParm ?
		(const lightstyle_t *)ref_agc_engine.EngineGetParm(
			PARM_GET_LIGHTSTYLES_PTR, 0 ) : NULL;
	const uint16_t *gamma = ref_agc_engine.EngineGetParm ?
		(const uint16_t *)ref_agc_engine.EngineGetParm(
			PARM_GET_LIGHTGAMMATABLE_PTR, 0 ) : NULL;
	if( !surface || !surface->samples || !rgba || !width || !height ||
		width > SIZE_MAX / height )
		return -1;
	const size_t texels = (size_t)width * height;
	for( size_t texel = 0u; texel < texels; ++texel )
	{
		uint32_t sum[3] = { 0u, 0u, 0u };
		for( unsigned map = 0u; map < MAXLIGHTMAPS; ++map )
		{
			const uint8_t style = surface->styles[map];
			if( style >= 255u ) break;
			const uint32_t scale = RefAgcLightStyleScale( styles, style );
			const color24 *sample = &surface->samples[map * texels + texel];
			sum[0] += (uint32_t)sample->r * scale;
			sum[1] += (uint32_t)sample->g * scale;
			sum[2] += (uint32_t)sample->b * scale;
		}
		for( unsigned channel = 0u; channel < 3u; ++channel )
		{
			uint32_t value = (sum[channel] * 256u) >> 14u;
			if( value > 1023u ) value = 1023u;
			rgba[texel * 4u + channel] = gamma ?
				(uint8_t)(gamma[value] >> 2u) : (uint8_t)(value >> 2u);
		}
		rgba[texel * 4u + 3u] = 255u;
	}
	return 0;
}

static int RefAgcExtractWorld(const model_t *model, RefAgcWorldInput *out,
	RefAgcWorldVertex **out_vertices, uint32_t **out_indices,
	RefAgcWorldDraw **out_draws, uint8_t **out_lightmap_pixels)
{
	uint64_t vertex_count = 0, index_count = 0, draw_count = 0;
	RefAgcWorldVertex *vertices = NULL;
	RefAgcWorldDraw *draws = NULL;
	RefAgcLightmapRect *lightmap_rects = NULL;
	RefAgcLightmapPlacement *lightmap_placements = NULL;
	uint8_t *lightmap_pixels = NULL;
	uint32_t *indices = NULL;
	uint32_t vertex_cursor = 0, index_cursor = 0, draw_cursor = 0;
	uint32_t lightmap_width = 0u, lightmap_height = 0u;
	uint32_t lightmapped_surfaces = 0u;
	if( !model || !out || !out_vertices || !out_indices || !out_draws ||
		!out_lightmap_pixels ||
		model->type != mod_brush || !(model->flags & MODEL_WORLD) ||
		model->numsurfaces <= 0 || model->numvertexes <= 0 ||
		model->numedges <= 0 || model->numsurfedges <= 0 ||
		!model->surfaces || !model->vertexes || !model->surfedges ||
		(!(model->flags & MODEL_QBSP2) && !model->edges16) ||
		((model->flags & MODEL_QBSP2) && !model->edges32) )
		return -1;
	lightmap_rects = calloc( (size_t)model->numsurfaces,
		sizeof(*lightmap_rects) );
	lightmap_placements = calloc( (size_t)model->numsurfaces,
		sizeof(*lightmap_placements) );
	if( !lightmap_rects || !lightmap_placements )
		goto allocation_failed;
	for( int surface_index = 0; surface_index < model->numsurfaces;
		++surface_index )
	{
		const msurface_t *surface = &model->surfaces[surface_index];
		const texture_t *texture = surface->texinfo ?
			surface->texinfo->texture : NULL;
		if( surface->numedges < 3 || surface->firstedge < 0 ||
			surface->firstedge > model->numsurfedges ||
			surface->numedges > model->numsurfedges - surface->firstedge ||
			!texture || texture->width == 0u || texture->height == 0u ||
			texture->gl_texturenum <= 0 )
			goto extraction_failed;
		if( model->lightdata && surface->samples && surface->info &&
			!(surface->flags & SURF_DRAWTILED) )
		{
			const int sample_size = ref_agc_engine.Mod_SampleSizeForFace ?
				ref_agc_engine.Mod_SampleSizeForFace( surface ) : 16;
			if( sample_size <= 0 || surface->info->lightextents[0] < 0 ||
				surface->info->lightextents[1] < 0 )
				goto extraction_failed;
			lightmap_rects[surface_index].width =
				(uint32_t)(surface->info->lightextents[0] / sample_size) + 1u;
			lightmap_rects[surface_index].height =
				(uint32_t)(surface->info->lightextents[1] / sample_size) + 1u;
			++lightmapped_surfaces;
		}
		for( int edge = 0; edge < surface->numedges; ++edge )
		{
			uint32_t unused;
			if( RefAgcBrushVertexIndex( model,
				surface->firstedge + edge, &unused ) != 0 )
				goto extraction_failed;
		}
		vertex_count += (uint32_t)surface->numedges;
		index_count += (uint64_t)(surface->numedges - 2) * 3u;
		++draw_count;
		if( vertex_count > UINT32_MAX || index_count > UINT32_MAX ||
			draw_count > UINT32_MAX )
			goto extraction_failed;
	}
	if( lightmapped_surfaces && ref_agc_lightmap_atlas_layout(
		lightmap_rects, (uint32_t)model->numsurfaces, lightmap_placements,
		&lightmap_width, &lightmap_height ) != REF_AGC_LIGHTMAP_ATLAS_OK )
		goto extraction_failed;
	if( vertex_count == 0u || index_count == 0u || draw_count == 0u ||
		vertex_count > SIZE_MAX / sizeof(*vertices) ||
		index_count > SIZE_MAX / sizeof(*indices) ||
		draw_count > SIZE_MAX / sizeof(*draws) )
		goto extraction_failed;
	vertices = malloc( (size_t)vertex_count * sizeof(*vertices) );
	indices = malloc( (size_t)index_count * sizeof(*indices) );
	draws = malloc( (size_t)draw_count * sizeof(*draws) );
	if( !vertices || !indices || !draws )
	{
		goto allocation_failed;
	}
	if( lightmapped_surfaces )
	{
		const size_t atlas_bytes =
			(size_t)lightmap_width * lightmap_height * 4u;
		lightmap_pixels = calloc( 1u, atlas_bytes );
		if( !lightmap_pixels ) goto allocation_failed;
	}
	for( int surface_index = 0; surface_index < model->numsurfaces;
		++surface_index )
	{
		const msurface_t *surface = &model->surfaces[surface_index];
		const mtexinfo_t *texinfo = surface->texinfo;
		const texture_t *texture = texinfo->texture;
		const uint32_t first_vertex = vertex_cursor;
		const uint32_t first_index = index_cursor;
		const float sample_size = (float)(ref_agc_engine.Mod_SampleSizeForFace ?
			ref_agc_engine.Mod_SampleSizeForFace( surface ) : 16);
		const RefAgcLightmapPlacement *placement =
			&lightmap_placements[surface_index];
		if( placement->active )
		{
			const size_t source_bytes =
				(size_t)placement->width * placement->height * 4u;
			uint8_t *source = malloc( source_bytes );
			if( !source || RefAgcBuildSurfaceLightmap( surface,
				placement->width, placement->height, source ) != 0 ||
				ref_agc_lightmap_atlas_blit_rgba8( lightmap_pixels,
					lightmap_width, lightmap_height, lightmap_width * 4u,
					placement, source, placement->width * 4u ) !=
					REF_AGC_LIGHTMAP_ATLAS_OK )
			{
				free( source );
				goto extraction_failed;
			}
			free( source );
		}
		for( int edge = 0; edge < surface->numedges; ++edge )
		{
			uint32_t source_index;
			const float *position;
			RefAgcWorldVertex *vertex = &vertices[vertex_cursor++];
			if( RefAgcBrushVertexIndex( model, surface->firstedge + edge,
				&source_index ) != 0 )
				goto extraction_failed;
			position = model->vertexes[source_index].position;
			vertex->position[0] = position[0];
			vertex->position[1] = position[2];
			vertex->position[2] = -position[1];
			vertex->base_uv[0] = position[0] * texinfo->vecs[0][0] +
				position[1] * texinfo->vecs[0][1] +
				position[2] * texinfo->vecs[0][2];
			vertex->base_uv[1] = position[0] * texinfo->vecs[1][0] +
				position[1] * texinfo->vecs[1][1] +
				position[2] * texinfo->vecs[1][2];
			if( !(surface->flags & SURF_DRAWTURB) )
			{
				vertex->base_uv[0] = (vertex->base_uv[0] +
					texinfo->vecs[0][3]) / (float)texture->width;
				vertex->base_uv[1] = (vertex->base_uv[1] +
					texinfo->vecs[1][3]) / (float)texture->height;
			}
			vertex->light_uv[0] = 0.0f;
			vertex->light_uv[1] = 0.0f;
			if( placement->active && surface->info && sample_size > 0.0f )
			{
				const mextrasurf_t *info = surface->info;
				vertex->light_uv[0] = ((float)placement->x +
					(position[0] * info->lmvecs[0][0] +
						position[1] * info->lmvecs[0][1] +
						position[2] * info->lmvecs[0][2] + info->lmvecs[0][3] -
						(float)info->lightmapmins[0]) / sample_size + 0.5f) /
					(float)lightmap_width;
				vertex->light_uv[1] = ((float)placement->y +
					(position[0] * info->lmvecs[1][0] +
						position[1] * info->lmvecs[1][1] +
						position[2] * info->lmvecs[1][2] + info->lmvecs[1][3] -
						(float)info->lightmapmins[1]) / sample_size + 0.5f) /
					(float)lightmap_height;
			}
			vertex->surface_id = (uint32_t)surface_index;
			if( !isfinite( vertex->position[0] ) ||
				!isfinite( vertex->position[1] ) ||
				!isfinite( vertex->position[2] ) ||
				!isfinite( vertex->base_uv[0] ) ||
				!isfinite( vertex->base_uv[1] ) ||
				!isfinite( vertex->light_uv[0] ) ||
				!isfinite( vertex->light_uv[1] ) )
				goto extraction_failed;
		}
		for( uint32_t triangle = 1u;
			triangle + 1u < (uint32_t)surface->numedges; ++triangle )
		{
			indices[index_cursor++] = first_vertex;
			indices[index_cursor++] = first_vertex + triangle;
			indices[index_cursor++] = first_vertex + triangle + 1u;
		}
		memset( &draws[draw_cursor], 0, sizeof(draws[draw_cursor]) );
		draws[draw_cursor].first_index = first_index;
		draws[draw_cursor].index_count = index_cursor - first_index;
		draws[draw_cursor].texture_handle =
			(uint32_t)texture->gl_texturenum;
		draws[draw_cursor].surface_id = (uint32_t)surface_index;
		draws[draw_cursor].surface_flags = (uint32_t)surface->flags;
		if( surface->flags & SURF_TRANSPARENT )
			draws[draw_cursor].draw_flags |=
				REF_AGC_WORLD_DRAW_ALPHA_TEST;
		if( surface->flags & SURF_DRAWSKY )
			draws[draw_cursor].draw_flags |= REF_AGC_WORLD_DRAW_SKY;
		if( surface->flags & SURF_DRAWTURB )
			draws[draw_cursor].draw_flags |= REF_AGC_WORLD_DRAW_TURB;
		if( placement->active )
			draws[draw_cursor].draw_flags |= REF_AGC_WORLD_DRAW_LIGHTMAP;
		++draw_cursor;
	}
	memset( out, 0, sizeof(*out) );
	out->model_name = model->name;
	out->model_flags = (uint32_t)model->flags;
	out->vertices = vertices;
	out->vertex_count = vertex_cursor;
	out->indices = indices;
	out->index_count = index_cursor;
	out->draws = draws;
	out->draw_count = draw_cursor;
	out->lightmap_pixels = lightmap_pixels;
	out->lightmap_width = lightmap_width;
	out->lightmap_height = lightmap_height;
	out->lightmap_row_pitch = lightmap_width * 4u;
	out->lightmap_pixel_bytes =
		(size_t)lightmap_width * lightmap_height * 4u;
	*out_vertices = vertices;
	*out_indices = indices;
	*out_draws = draws;
	*out_lightmap_pixels = lightmap_pixels;
	free( lightmap_rects );
	free( lightmap_placements );
	return 0;

extraction_failed:
	free( vertices ); free( indices ); free( draws ); free( lightmap_pixels );
	free( lightmap_rects ); free( lightmap_placements );
	return -6;

allocation_failed:
	free( vertices ); free( indices ); free( draws ); free( lightmap_pixels );
	free( lightmap_rects ); free( lightmap_placements );
	return -5;
}

static qboolean RefAgcProcessRenderData(model_t *model, qboolean create,
	const byte *buffer, size_t buffer_size)
{
	RefAgcWorldInput input;
	RefAgcWorldVertex *vertices = NULL;
	RefAgcWorldDraw *draws = NULL;
	uint8_t *lightmap_pixels = NULL;
	uint32_t *indices = NULL;
	int result;
	(void)buffer;
	(void)buffer_size;
	if( !model || model->type != mod_brush || !(model->flags & MODEL_WORLD) )
		return true;
	if( !ref_agc_world_initialized )
		return false;
	if( !create )
	{
		result = ref_agc_world_store_clear( &ref_agc_world, model->name );
		return result == REF_AGC_WORLD_OK ||
			result == REF_AGC_WORLD_NOT_FOUND;
	}
	result = RefAgcExtractWorld( model, &input, &vertices, &indices, &draws,
		&lightmap_pixels );
	if( result == 0 )
		result = ref_agc_world_store_publish( &ref_agc_world, &input );
	free( vertices ); free( indices ); free( draws ); free( lightmap_pixels );
	return result == 0;
}

static void RefAgcCaptureWorld(void)
{
	const ref_client_t *client;
	const model_t *model;
	RefAgcLiveWorld world;
	RefAgcTextureView texture_view;
	uint64_t texture_refs = 0;
	uint64_t textures_resolved = 0;
	if( !ref_agc_live_initialized || !ref_agc_engine.EngineGetParm )
		return;
	client = (const ref_client_t *)ref_agc_engine.EngineGetParm(
		PARM_GET_CLIENT_PTR, 0 );
	model = client ? client->models[1] : NULL;
	if( !model )
		return;
	memset( &world, 0, sizeof(world) );
	strncpy( world.model_name, model->name, sizeof(world.model_name) - 1u );
	world.model_type = model->type;
	world.model_flags = (uint32_t)model->flags;
	world.surfaces = model->numsurfaces > 0 ? (uint32_t)model->numsurfaces : 0u;
	world.vertices = model->numvertexes > 0 ? (uint32_t)model->numvertexes : 0u;
	world.edges = model->numedges > 0 ? (uint32_t)model->numedges : 0u;
	world.textures = model->numtextures > 0 ? (uint32_t)model->numtextures : 0u;
	world.leafs = model->numleafs > 0 ? (uint32_t)model->numleafs : 0u;
	world.has_visibility = model->visdata != NULL;
	world.has_lightdata = model->lightdata != NULL;
	for( int i = 0; i < model->numtextures; ++i )
	{
		const texture_t *texture = model->textures[i];
		if( !texture ) continue;
		++texture_refs;
		if( texture->gl_texturenum > 0 &&
			ref_agc_texture_store_get( &ref_agc_textures,
				(uint32_t)texture->gl_texturenum, &texture_view ) == 0 )
			++textures_resolved;
	}
	ref_agc_world_texture_refs = texture_refs;
	ref_agc_world_textures_resolved = textures_resolved;
	RefAgcCopy3( world.mins, model->mins );
	RefAgcCopy3( world.maxs, model->maxs );
	ref_agc_live_set_world( &ref_agc_live, &world );
}

void PS5_RefAgcRuntimeReady(void)
{
	ref_agc_runtime_state = REF_AGC_READY;
}

void PS5_RefAgcRuntimeComplete(uint64_t frames, uint64_t frame_hash,
	uint64_t bright_pixels, int teardown_result)
{
	ref_agc_runtime_frames = frames;
	ref_agc_frame_hash = frame_hash;
	ref_agc_bright_pixels = bright_pixels;
	ref_agc_teardown_result = teardown_result;
	ref_agc_runtime_result = teardown_result;
	ref_agc_runtime_state = teardown_result == 0 ? REF_AGC_COMPLETE : REF_AGC_FAILED;
}

void PS5_RefAgcRuntimeLiveStats(uint64_t consumed_frames,
	uint64_t consumed_serial, uint64_t consumed_view_frames,
	uint64_t camera_hash, uint64_t camera_changes)
{
	ref_agc_consumed_frames = consumed_frames;
	ref_agc_consumed_serial = consumed_serial;
	ref_agc_consumed_view_frames = consumed_view_frames;
	ref_agc_consumed_camera_hash = camera_hash;
	ref_agc_consumed_camera_changes = camera_changes;
}

void PS5_RefAgcRuntimeFailed(int result)
{
	if( result == 0 )
	{
		ref_agc_runtime_result = 0;
		ref_agc_teardown_result = 0;
		ref_agc_runtime_frames = 0;
		ref_agc_frame_hash = 0;
		ref_agc_bright_pixels = 0;
		ref_agc_runtime_state = REF_AGC_STARTING;
		return;
	}
	ref_agc_runtime_result = result;
	ref_agc_runtime_state = REF_AGC_FAILED;
	if( ref_agc_live_initialized )
		(void)ref_agc_live_request_stop( &ref_agc_live, result );
}

static void *RefAgcRuntimeThread(void *unused)
{
	const int result = ps5_ref_agc_native_main();
	(void)unused;
	if( ref_agc_runtime_state != REF_AGC_COMPLETE &&
		ref_agc_runtime_state != REF_AGC_FAILED )
		PS5_RefAgcRuntimeFailed( result != 0 ? result : -1 );
	return NULL;
}

static qboolean RefAgcInit(void)
{
	struct timespec wait = { 0, 1000000L };
	const RefAgcTextureAllocator texture_allocator = {
		RefAgcStorageAlloc, RefAgcStorageFree, NULL };
	const RefAgcWorldAllocator world_allocator = {
		RefAgcStorageAlloc, RefAgcStorageFree, NULL };
	unsigned attempt;
	if( ref_agc_thread_created )
		return ref_agc_runtime_state == REF_AGC_READY ||
			ref_agc_runtime_state == REF_AGC_COMPLETE;
	if( !ref_agc_engine.R_Init_Video( REF_SOFTWARE ))
		return false;
	if( !ref_agc_live_initialized )
	{
		if( ref_agc_live_store_init( &ref_agc_live ) != 0 )
		{
			ref_agc_engine.R_Free_Video();
			return false;
		}
		ref_agc_live_initialized = 1;
	}
	if( RefAgcStoragePoolInit( ) != 0 )
	{
		ref_agc_live_store_destroy( &ref_agc_live );
		ref_agc_live_initialized = 0;
		ref_agc_engine.R_Free_Video();
		return false;
	}
	if( !ref_agc_textures_initialized )
	{
		if( ref_agc_texture_store_init( &ref_agc_textures,
			&texture_allocator ) != 0 )
		{
			RefAgcStoragePoolDestroy( );
			ref_agc_live_store_destroy( &ref_agc_live );
			ref_agc_live_initialized = 0;
			ref_agc_engine.R_Free_Video();
			return false;
		}
		ref_agc_textures_initialized = 1;
	}
	if( !ref_agc_world_initialized )
	{
		if( ref_agc_world_store_init( &ref_agc_world,
			&world_allocator ) != 0 )
		{
			ref_agc_texture_store_destroy( &ref_agc_textures );
			ref_agc_textures_initialized = 0;
			RefAgcStoragePoolDestroy( );
			ref_agc_live_store_destroy( &ref_agc_live );
			ref_agc_live_initialized = 0;
			ref_agc_engine.R_Free_Video();
			return false;
		}
		ref_agc_world_initialized = 1;
	}
	ref_agc_runtime_state = REF_AGC_STARTING;
	if( pthread_create( &ref_agc_thread, NULL, RefAgcRuntimeThread, NULL ) != 0 )
	{
		ref_agc_runtime_state = REF_AGC_FAILED;
		ref_agc_runtime_result = -2;
		ref_agc_live_store_destroy( &ref_agc_live );
		ref_agc_live_initialized = 0;
		ref_agc_texture_store_destroy( &ref_agc_textures );
		ref_agc_textures_initialized = 0;
		ref_agc_world_store_destroy( &ref_agc_world );
		ref_agc_world_initialized = 0;
		RefAgcStoragePoolDestroy( );
		ref_agc_engine.R_Free_Video();
		return false;
	}
	ref_agc_thread_created = 1;
	for( attempt = 0; attempt < 5000u; ++attempt )
	{
		if( ref_agc_runtime_state == REF_AGC_READY ||
			ref_agc_runtime_state == REF_AGC_COMPLETE )
			return true;
		if( ref_agc_runtime_state == REF_AGC_FAILED )
			break;
		(void)nanosleep( &wait, NULL );
	}
	return false;
}

static void RefAgcShutdown(void)
{
	RefAgcTextureStats texture_stats;
	if( ref_agc_thread_created )
	{
		if( ref_agc_live_initialized )
			(void)ref_agc_live_request_stop( &ref_agc_live, 0 );
		(void)pthread_join( ref_agc_thread, NULL );
		ref_agc_thread_created = 0;
		if( ref_agc_runtime_state == REF_AGC_COMPLETE )
			ref_agc_runtime_state = REF_AGC_JOINED;
	}
	if( ref_agc_live_initialized )
	{
		ref_agc_live_store_destroy( &ref_agc_live );
		ref_agc_live_initialized = 0;
	}
	if( ref_agc_world_initialized )
	{
		ref_agc_world_store_destroy( &ref_agc_world );
		ref_agc_world_initialized = 0;
	}
	if( ref_agc_textures_initialized )
	{
		if( ref_agc_texture_store_stats( &ref_agc_textures,
			&texture_stats ) == 0 )
		{
			ref_agc_texture_revision = texture_stats.revision;
			ref_agc_texture_creates = texture_stats.creates;
			ref_agc_texture_updates = texture_stats.updates;
			ref_agc_texture_frees = texture_stats.frees;
			ref_agc_texture_peak_bytes = texture_stats.peak_resident_bytes;
			ref_agc_texture_handles = texture_stats.handles_issued;
			ref_agc_texture_peak_active = texture_stats.peak_active;
		}
		ref_agc_texture_store_destroy( &ref_agc_textures );
		ref_agc_textures_initialized = 0;
	}
	RefAgcStoragePoolDestroy( );
	ref_agc_engine.R_Free_Video();
}

static void RefAgcSkyTextureTrace(const char *stage, const char *name,
	const rgbdata_t *image, texFlags_t flags, int result)
{
	(void)flags;
	if( !name || strncmp( name, "gfx/env/", 8u ) != 0 ||
		!ref_agc_engine.Con_Printf )
		return;
	ref_agc_engine.Con_Printf(
		"REF_AGC_SKY_TEXTURE stage=%s name=%s result=%d "
		"width=%u height=%u depth=%u type=%u size=%lu buffer=%d\n",
		stage ? stage : "unknown", name ? name : "(null)", result,
		image ? (unsigned)image->width : 0u,
		image ? (unsigned)image->height : 0u,
		image ? (unsigned)image->depth : 0u,
		image ? (unsigned)image->type : 0u,
		(unsigned long)(image ? image->size : 0u),
		image && image->buffer ? 1 : 0 );
}

static int RefAgcStoreImage(const char *name, const rgbdata_t *image,
	texFlags_t flags, qboolean update)
{
	rgbdata_t *owned = NULL;
	const rgbdata_t *source = image;
	RefAgcTextureInput input;
	uint32_t handle = 0;
	uint process_flags = IMAGE_FORCE_RGBA;
	if( !ref_agc_textures_initialized || !name || !name[0] || !image ||
		image->width == 0 || image->height == 0 || image->size == 0 )
	{
		RefAgcSkyTextureTrace( "invalid-input", name, image, flags, -1 );
		return 0;
	}
	if( image->buffer )
	{
		owned = ref_agc_engine.FS_CopyImage( image );
		if( !owned )
		{
			RefAgcSkyTextureTrace( "copy-failed", name, image, flags, -2 );
			return 0;
		}
		if( flags & TF_MAKELUMA )
			process_flags |= IMAGE_MAKE_LUMA;
		if( !ref_agc_engine.Image_Process( &owned, 0, 0,
			process_flags, 0.0f ) && owned->type != PF_RGBA_32 )
		{
			RefAgcSkyTextureTrace( "rgba-conversion-failed", name,
				owned, flags, -3 );
			ref_agc_engine.FS_FreeImage( owned );
			return 0;
		}
		source = owned;
	}
	if( source->type != PF_RGBA_32 || source->size == 0 )
	{
		RefAgcSkyTextureTrace( "rgba-contract-failed", name,
			source, flags, -4 );
		if( owned ) ref_agc_engine.FS_FreeImage( owned );
		return 0;
	}
	if( source->flags & IMAGE_HAS_ALPHA ) flags |= TF_HAS_ALPHA;
	if( source->flags & IMAGE_HAS_LUMA ) flags |= TF_HAS_LUMA;
	flags &= ~(TF_MAKELUMA|TF_UPDATE);
	memset( &input, 0, sizeof(input) );
	input.name = name;
	input.width = source->width;
	input.height = source->height;
	input.depth = source->depth ? source->depth : 1u;
	input.format = source->type;
	input.flags = (uint32_t)flags;
	input.mip_count = source->numMips ? source->numMips : 1u;
	input.sampler_clamp = (flags & TF_CLAMP) != 0;
	input.pixels = source->buffer;
	input.pixel_bytes = source->size;
	const int store_result = ref_agc_texture_store_upsert(
		&ref_agc_textures, &input, update != false, &handle );
	if( store_result != 0 )
	{
		RefAgcSkyTextureTrace( "store-failed", name, source, flags,
			store_result );
		handle = 0;
	}
	if( owned ) ref_agc_engine.FS_FreeImage( owned );
	return (int)handle;
}

static int RefAgcLoadTextureFromBuffer(const char *name, rgbdata_t *image,
	texFlags_t flags, qboolean update)
{
	return RefAgcStoreImage( name, image, flags, update );
}

static int RefAgcLoadTexture(const char *name, const byte *buffer,
	size_t size, int flags)
{
	rgbdata_t *image;
	uint32_t existing;
	uint image_flags = 0;
	int handle;
	if( !name || !name[0] )
		return 0;
	if( ref_agc_texture_store_find( &ref_agc_textures, name,
		&existing ) == 0 )
		return (int)existing;
	if( flags & TF_NOFLIP_TGA ) image_flags |= IL_DONTFLIP_TGA;
	ref_agc_engine.Image_SetForceFlags( image_flags );
	image = ref_agc_engine.FS_LoadImage( name, buffer, size );
	if( !image )
	{
		RefAgcSkyTextureTrace( "load-image-failed", name, NULL,
			(texFlags_t)flags, -5 );
		return 0;
	}
	handle = RefAgcStoreImage( name, image, (texFlags_t)flags, false );
	ref_agc_engine.FS_FreeImage( image );
	return handle;
}

static int RefAgcCreateTexture(const char *name, int width, int height,
	const void *buffer, texFlags_t flags)
{
	rgbdata_t image;
	size_t bytes;
	if( width <= 0 || height <= 0 || width > UINT16_MAX ||
		height > UINT16_MAX ||
		(size_t)width > SIZE_MAX / 4u / (size_t)height )
		return 0;
	bytes = (size_t)width * (size_t)height * 4u;
	memset( &image, 0, sizeof(image) );
	image.width = (word)width;
	image.height = (word)height;
	image.depth = 1;
	image.type = PF_RGBA_32;
	image.buffer = (byte *)buffer;
	image.size = bytes;
	if( flags & TF_HAS_ALPHA ) image.flags |= IMAGE_HAS_ALPHA;
	return RefAgcStoreImage( name, &image, flags,
		(flags & TF_UPDATE) != 0 );
}

static int RefAgcFindTexture(const char *name)
{
	uint32_t handle = 0;
	return ref_agc_texture_store_find( &ref_agc_textures, name,
		&handle ) == 0 ? (int)handle : 0;
}

static const char *RefAgcTextureName(unsigned int handle)
{
	RefAgcTextureView view;
	return ref_agc_texture_store_get( &ref_agc_textures, handle,
		&view ) == 0 ? view.name : NULL;
}

static const byte *RefAgcTextureData(unsigned int handle)
{
	RefAgcTextureView view;
	return ref_agc_texture_store_get( &ref_agc_textures, handle,
		&view ) == 0 ? view.pixels : NULL;
}

static void RefAgcFreeTexture(unsigned int handle)
{
	if( handle != 0u )
		(void)ref_agc_texture_store_free( &ref_agc_textures, handle );
}

static intptr_t RefAgcGetParm(int parm, int arg)
{
	RefAgcTextureView view;
	RefAgcTextureStats stats;
	if( parm == PARM_GL_CONTEXT_TYPE )
		return CONTEXT_TYPE_SOFTWARE;
	if( parm == PARM_TEX_MEMORY )
		return ref_agc_texture_store_stats( &ref_agc_textures, &stats ) == 0 ?
			(intptr_t)stats.resident_bytes : 0;
	if( ref_agc_texture_store_get( &ref_agc_textures,
		(uint32_t)arg, &view ) != 0 )
		return 0;
	switch( parm )
	{
	case PARM_TEX_WIDTH:
	case PARM_TEX_SRC_WIDTH: return view.width;
	case PARM_TEX_HEIGHT:
	case PARM_TEX_SRC_HEIGHT: return view.height;
	case PARM_TEX_DEPTH: return view.depth;
	case PARM_TEX_GLFORMAT: return view.format;
	case PARM_TEX_MIPCOUNT: return view.mip_count;
	case PARM_TEX_FLAGS: return view.flags;
	case PARM_TEX_TEXNUM: return view.handle;
	default: return 0;
	}
}

int PS5_RefAgcVisitTextures(uint64_t after_revision,
	RefAgcTextureVisitor visitor, void *user, uint64_t *out_revision)
{
	return ref_agc_texture_store_visit_changed( &ref_agc_textures,
		after_revision, visitor, user, out_revision );
}

int PS5_RefAgcTextureStats(RefAgcTextureStats *out)
{
	return ref_agc_texture_store_stats( &ref_agc_textures, out );
}

int PS5_RefAgcVisitWorld(uint64_t after_revision,
	RefAgcWorldVisitor visitor, void *user, uint64_t *out_revision)
{
	return ref_agc_world_store_visit_changed( &ref_agc_world,
		after_revision, visitor, user, out_revision );
}

int PS5_RefAgcWorldStats(RefAgcWorldStats *out)
{
	return ref_agc_world_store_stats( &ref_agc_world, out );
}

static const char *RefAgcConfigName(void)
{
	return "ref_agc";
}

static void RefAgcBeginFrame(qboolean clear_scene)
{
	++ref_agc_begin_calls;
	ref_agc_live_begin_frame( &ref_agc_live, clear_scene,
		ref_agc_begin_calls );
}

static void RefAgcRenderScene(void)
{
	++ref_agc_scene_calls;
}

static void RefAgcRenderFrame(const struct ref_viewpass_s *view)
{
	RefAgcLiveView live;
	const ref_client_t *client;
	++ref_agc_scene_calls;
	if( !view )
		return;
	memset( &live, 0, sizeof(live) );
	memcpy( live.viewport, view->viewport, sizeof(live.viewport) );
	RefAgcCopy3( live.origin, view->vieworigin );
	RefAgcCopy3( live.angles, view->viewangles );
	live.fov_x = view->fov_x;
	live.fov_y = view->fov_y;
	live.view_entity = view->viewentity;
	live.flags = (uint32_t)view->flags;
	client = ref_agc_engine.EngineGetParm ?
		(const ref_client_t *)ref_agc_engine.EngineGetParm(
			PARM_GET_CLIENT_PTR, 0 ) : NULL;
	live.time_seconds = client ? client->time : 0.0;
	live.paused = client ? (uint32_t)(client->paused != false) : 0u;
	ref_agc_live_set_view( &ref_agc_live, &live, ref_agc_scene_calls );
}

static void RefAgcSetupSky(int *skybox_textures)
{
	uint32_t handles[REF_AGC_LIVE_SKY_SIDES];
	_Static_assert(SKYBOX_MAX_SIDES == REF_AGC_LIVE_SKY_SIDES,
		"RefAPI skybox side count");
	if( !ref_agc_live_initialized )
		return;
	if( !skybox_textures )
	{
		ref_agc_live_set_sky( &ref_agc_live, NULL );
		return;
	}
	for( unsigned side = 0u; side < REF_AGC_LIVE_SKY_SIDES; ++side )
		handles[side] = (uint32_t)skybox_textures[side];
	ref_agc_live_set_sky( &ref_agc_live, handles );
}

static void RefAgcEndFrame(void)
{
	int wait_result;
	uint64_t view_hash;
	++ref_agc_end_calls;
	if( ref_agc_live_publish( &ref_agc_live, ref_agc_end_calls ) != 0 )
	{
		ref_agc_runtime_result = -3;
		ref_agc_runtime_state = REF_AGC_FAILED;
		return;
	}
	ref_agc_live_frames = ref_agc_live.building.serial;
	ref_agc_live_map_serial = ref_agc_live.building.map_serial;
	ref_agc_live_world_surfaces = ref_agc_live.building.world.surfaces;
	if( ref_agc_live.building.entity_count > ref_agc_live_entity_peak )
		ref_agc_live_entity_peak = ref_agc_live.building.entity_count;
	if( ref_agc_live.building.command_2d_count > ref_agc_live_2d_peak )
		ref_agc_live_2d_peak = ref_agc_live.building.command_2d_count;
	ref_agc_live_dropped_entities +=
		ref_agc_live.building.dropped_entities;
	ref_agc_live_dropped_2d +=
		ref_agc_live.building.dropped_2d_commands;
	if( ref_agc_live.building.view.valid )
	{
		++ref_agc_live_view_frames;
		view_hash = RefAgcHashBytes( &ref_agc_live.building.view,
			sizeof(ref_agc_live.building.view) );
		if( ref_agc_live_view_hash != 0 && view_hash != ref_agc_live_view_hash )
			++ref_agc_live_view_changes;
		ref_agc_live_view_hash = view_hash;
	}
	wait_result = ref_agc_live_wait_consumed( &ref_agc_live,
		ref_agc_live.building.serial );
	if( wait_result != 0 )
	{
		ref_agc_runtime_result = wait_result;
		ref_agc_runtime_state = REF_AGC_FAILED;
	}
}

static void RefAgcNewMap(void)
{
	++ref_agc_newmap_calls;
	RefAgcCaptureWorld();
}

static void RefAgcClearScene(void)
{
	ref_agc_live_clear_scene( &ref_agc_live );
}

static qboolean RefAgcAddEntity(struct cl_entity_s *entity, int type)
{
	RefAgcLiveEntity live;
	if( !entity )
		return false;
	memset( &live, 0, sizeof(live) );
	live.index = entity->index;
	live.entity_type = type;
	live.model_type = entity->model ? entity->model->type : mod_bad;
	live.model_index = entity->curstate.modelindex;
	live.sequence = entity->curstate.sequence;
	live.body = entity->curstate.body;
	live.skin = entity->curstate.skin;
	live.render_mode = entity->curstate.rendermode;
	live.render_amount = entity->curstate.renderamt;
	live.render_fx = entity->curstate.renderfx;
	live.effects = (uint32_t)entity->curstate.effects;
	live.render_color[0] = entity->curstate.rendercolor.r;
	live.render_color[1] = entity->curstate.rendercolor.g;
	live.render_color[2] = entity->curstate.rendercolor.b;
	live.render_color[3] = (uint8_t)(entity->curstate.renderamt < 0 ? 0 :
		entity->curstate.renderamt > 255 ? 255 : entity->curstate.renderamt);
	RefAgcCopy3( live.origin, entity->origin );
	RefAgcCopy3( live.angles, entity->angles );
	live.scale = entity->curstate.scale;
	live.frame = entity->curstate.frame;
	if( entity->model )
		strncpy( live.model_name, entity->model->name,
			sizeof(live.model_name) - 1u );
	return ref_agc_live_add_entity( &ref_agc_live, &live ) == 0;
}

static void RefAgcSet2DMode(qboolean enable)
{
	RefAgcLive2DCommand command;
	memset( &command, 0, sizeof(command) );
	command.type = REF_AGC_LIVE_2D_MODE;
	command.enabled = enable != false;
	(void)ref_agc_live_add_2d( &ref_agc_live, &command );
}

static void RefAgcDrawStretchPic(float x, float y, float w, float h,
	float s1, float t1, float s2, float t2, int texture)
{
	RefAgcLive2DCommand command;
	memset( &command, 0, sizeof(command) );
	command.type = REF_AGC_LIVE_2D_STRETCH_PIC;
	command.texture = texture;
	command.x = x; command.y = y; command.width = w; command.height = h;
	command.s1 = s1; command.t1 = t1; command.s2 = s2; command.t2 = t2;
	memset( command.color, 255, sizeof(command.color) );
	(void)ref_agc_live_add_2d( &ref_agc_live, &command );
}

static void RefAgcFillRGBA(int render_mode, float x, float y, float w,
	float h, byte r, byte g, byte b, byte a)
{
	RefAgcLive2DCommand command;
	memset( &command, 0, sizeof(command) );
	command.type = REF_AGC_LIVE_2D_FILL_RGBA;
	command.render_mode = render_mode;
	command.x = x; command.y = y; command.width = w; command.height = h;
	command.color[0] = r; command.color[1] = g;
	command.color[2] = b; command.color[3] = a;
	(void)ref_agc_live_add_2d( &ref_agc_live, &command );
}

int PS5_RefAgcTakeLiveFrame(uint64_t after_serial, RefAgcLiveFrame *out)
{
	return ref_agc_live_take_latest( &ref_agc_live, after_serial, out );
}

int PS5_RefAgcWaitLiveFrame(uint64_t after_serial, RefAgcLiveFrame *out)
{
	return ref_agc_live_wait_latest( &ref_agc_live, after_serial, out );
}

int PS5_RefAgcConsumeLiveFrame(uint64_t serial)
{
	return ref_agc_live_mark_consumed( &ref_agc_live, serial );
}

int PS5_RefAgcPrxRuntimeState(void) { return ref_agc_runtime_state; }
int PS5_RefAgcPrxRuntimeResult(void) { return ref_agc_runtime_result; }
int PS5_RefAgcPrxTeardownResult(void) { return ref_agc_teardown_result; }
uint64_t PS5_RefAgcPrxRuntimeFrames(void) { return ref_agc_runtime_frames; }
uint64_t PS5_RefAgcPrxFrameHash(void) { return ref_agc_frame_hash; }
uint64_t PS5_RefAgcPrxBrightPixels(void) { return ref_agc_bright_pixels; }
uint64_t PS5_RefAgcPrxBeginCalls(void) { return ref_agc_begin_calls; }
uint64_t PS5_RefAgcPrxSceneCalls(void) { return ref_agc_scene_calls; }
uint64_t PS5_RefAgcPrxEndCalls(void) { return ref_agc_end_calls; }
uint64_t PS5_RefAgcPrxNewMapCalls(void) { return ref_agc_newmap_calls; }
uint64_t PS5_RefAgcPrxLiveFrames(void) { return ref_agc_live_frames; }
uint64_t PS5_RefAgcPrxLiveViewFrames(void) { return ref_agc_live_view_frames; }
uint64_t PS5_RefAgcPrxLiveViewHash(void) { return ref_agc_live_view_hash; }
uint64_t PS5_RefAgcPrxLiveViewChanges(void) { return ref_agc_live_view_changes; }
uint64_t PS5_RefAgcPrxLiveMapSerial(void) { return ref_agc_live_map_serial; }
uint64_t PS5_RefAgcPrxLiveWorldSurfaces(void) { return ref_agc_live_world_surfaces; }
uint64_t PS5_RefAgcPrxLiveEntityPeak(void) { return ref_agc_live_entity_peak; }
uint64_t PS5_RefAgcPrxLive2DPeak(void) { return ref_agc_live_2d_peak; }
uint64_t PS5_RefAgcPrxLiveDroppedEntities(void) { return ref_agc_live_dropped_entities; }
uint64_t PS5_RefAgcPrxLiveDropped2D(void) { return ref_agc_live_dropped_2d; }
uint64_t PS5_RefAgcPrxConsumedFrames(void) { return ref_agc_consumed_frames; }
uint64_t PS5_RefAgcPrxConsumedSerial(void) { return ref_agc_consumed_serial; }
uint64_t PS5_RefAgcPrxConsumedViewFrames(void) { return ref_agc_consumed_view_frames; }
uint64_t PS5_RefAgcPrxConsumedCameraHash(void) { return ref_agc_consumed_camera_hash; }
uint64_t PS5_RefAgcPrxConsumedCameraChanges(void) { return ref_agc_consumed_camera_changes; }
uint64_t PS5_RefAgcPrxTextureRevision(void) { return ref_agc_texture_revision; }
uint64_t PS5_RefAgcPrxTextureCreates(void) { return ref_agc_texture_creates; }
uint64_t PS5_RefAgcPrxTextureUpdates(void) { return ref_agc_texture_updates; }
uint64_t PS5_RefAgcPrxTextureFrees(void) { return ref_agc_texture_frees; }
uint64_t PS5_RefAgcPrxTexturePeakBytes(void) { return ref_agc_texture_peak_bytes; }
uint64_t PS5_RefAgcPrxTextureHandles(void) { return ref_agc_texture_handles; }
uint64_t PS5_RefAgcPrxTexturePeakActive(void) { return ref_agc_texture_peak_active; }
uint64_t PS5_RefAgcPrxWorldTextureRefs(void) { return ref_agc_world_texture_refs; }
uint64_t PS5_RefAgcPrxWorldTexturesResolved(void) { return ref_agc_world_textures_resolved; }

int PS5_RefAgcPrxEngineTableMask(void)
{
	int mask = 0;
	if( ref_agc_engine.R_Init_Video ) mask |= 1;
	if( ref_agc_engine.R_Free_Video ) mask |= 2;
	if( ref_agc_engine.COM_LoadLibrary ) mask |= 4;
	if( ref_agc_engine.COM_FreeLibrary ) mask |= 8;
	if( ref_agc_engine.fsapi ) mask |= 16;
	if( ref_agc_engine.Host_Error ) mask |= 32;
	return mask;
}

int EXPORT GetRefAPI(int version, ref_interface_t *funcs,
	ref_api_t *engfuncs, ref_globals_t *globals)
{
	int result;
	if( !funcs || !engfuncs || !globals )
		return 0;
	result = PS5_RefAgcNullGetRefAPI( version, funcs, engfuncs, globals );
	if( result != REF_API_VERSION )
		return 0;
	ref_agc_engine = *engfuncs;
	funcs->R_Init = RefAgcInit;
	funcs->R_Shutdown = RefAgcShutdown;
	funcs->R_GetConfigName = RefAgcConfigName;
	funcs->R_BeginFrame = RefAgcBeginFrame;
	funcs->R_RenderScene = RefAgcRenderScene;
	funcs->R_EndFrame = RefAgcEndFrame;
	funcs->R_NewMap = RefAgcNewMap;
	funcs->R_SetupSky = RefAgcSetupSky;
	funcs->GL_RenderFrame = RefAgcRenderFrame;
	funcs->R_ClearScene = RefAgcClearScene;
	funcs->R_AddEntity = RefAgcAddEntity;
	funcs->R_Set2DMode = RefAgcSet2DMode;
	funcs->R_DrawStretchPic = RefAgcDrawStretchPic;
	funcs->FillRGBA = RefAgcFillRGBA;
	funcs->R_GetTextureOriginalBuffer = RefAgcTextureData;
	funcs->GL_LoadTextureFromBuffer = RefAgcLoadTextureFromBuffer;
	funcs->RefGetParm = RefAgcGetParm;
	funcs->GL_CreateTexture = RefAgcCreateTexture;
	funcs->GL_FindTexture = RefAgcFindTexture;
	funcs->GL_TextureName = RefAgcTextureName;
	funcs->GL_TextureData = RefAgcTextureData;
	funcs->GL_LoadTexture = RefAgcLoadTexture;
	funcs->GL_FreeTexture = RefAgcFreeTexture;
	funcs->Mod_ProcessRenderData = RefAgcProcessRenderData;
	return REF_API_VERSION;
}
