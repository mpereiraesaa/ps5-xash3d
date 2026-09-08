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

/* Reuse the upstream ABI-complete callback skeleton without modifying the
 * pinned submodule.  Its public entry point is renamed and wrapped below. */
#define GetRefAPI PS5_RefAgcNullGetRefAPI
#include "../../third_party/xash3d-fwgs/ref/null/r_context.c"
#undef GetRefAPI

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
	ref_agc_runtime_state = REF_AGC_STARTING;
	if( pthread_create( &ref_agc_thread, NULL, RefAgcRuntimeThread, NULL ) != 0 )
	{
		ref_agc_runtime_state = REF_AGC_FAILED;
		ref_agc_runtime_result = -2;
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
	ref_agc_engine.R_Free_Video();
}

static const char *RefAgcConfigName(void)
{
	return "ref_agc";
}

static void RefAgcBeginFrame(qboolean clear_scene)
{
	(void)clear_scene;
	++ref_agc_begin_calls;
}

static void RefAgcRenderScene(void)
{
	++ref_agc_scene_calls;
}

static void RefAgcRenderFrame(const struct ref_viewpass_s *view)
{
	(void)view;
	++ref_agc_scene_calls;
}

static void RefAgcEndFrame(void)
{
	++ref_agc_end_calls;
}

static void RefAgcNewMap(void)
{
	++ref_agc_newmap_calls;
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
	return REF_API_VERSION;
}
