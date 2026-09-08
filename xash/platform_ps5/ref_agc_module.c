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
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "ref_agc_live_frame.h"

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
static pthread_t ref_agc_thread;
static int ref_agc_thread_created;
static RefAgcLiveStore ref_agc_live;
static int ref_agc_live_initialized;

static void RefAgcCopy3(float out[3], const float in[3])
{
	memcpy( out, in, 3u * sizeof(float) );
}

static void RefAgcCaptureWorld(void)
{
	const ref_client_t *client;
	const model_t *model;
	RefAgcLiveWorld world;
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
	ref_agc_runtime_state = REF_AGC_STARTING;
	if( pthread_create( &ref_agc_thread, NULL, RefAgcRuntimeThread, NULL ) != 0 )
	{
		ref_agc_runtime_state = REF_AGC_FAILED;
		ref_agc_runtime_result = -2;
		ref_agc_live_store_destroy( &ref_agc_live );
		ref_agc_live_initialized = 0;
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
	if( ref_agc_thread_created )
	{
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
	ref_agc_engine.R_Free_Video();
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
	ref_agc_live_set_view( &ref_agc_live, &live, ref_agc_scene_calls );
}

static void RefAgcEndFrame(void)
{
	++ref_agc_end_calls;
	(void)ref_agc_live_publish( &ref_agc_live, ref_agc_end_calls );
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
	funcs->GL_RenderFrame = RefAgcRenderFrame;
	funcs->R_ClearScene = RefAgcClearScene;
	funcs->R_AddEntity = RefAgcAddEntity;
	funcs->R_Set2DMode = RefAgcSet2DMode;
	funcs->R_DrawStretchPic = RefAgcDrawStretchPic;
	funcs->FillRGBA = RefAgcFillRGBA;
	return REF_API_VERSION;
}
