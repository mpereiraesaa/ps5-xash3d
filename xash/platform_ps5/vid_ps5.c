/*
vid_ps5.c - headless video, voice and status hooks for the client engine boot
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Phase 5 gate 2 runs the client engine without a display. The window hooks
report one fixed mode; the software-renderer buffer lives in ordinary memory
and every frame the renderer presents is hashed into the telemetry stream,
so a headless run still proves that the engine drew something and what.
The AGC renderer of the next gate replaces this file's presentation path.
*/

#include "common.h"
#include "client.h"
#include "vid_common.h"
#include "platform/platform.h"
#include "ps5log.h"
#include "ps5_xash_build.h"

#ifndef PS5_XASH_VIDEO_WIDTH
#define PS5_XASH_VIDEO_WIDTH 640
#endif
#ifndef PS5_XASH_VIDEO_HEIGHT
#define PS5_XASH_VIDEO_HEIGHT 480
#endif
#ifndef PS5_XASH_FRAME_HASH_INTERVAL
#define PS5_XASH_FRAME_HASH_INTERVAL 300
#endif

static struct vidmode_s ps5_vidmode = { "PS5 headless", PS5_XASH_VIDEO_WIDTH, PS5_XASH_VIDEO_HEIGHT };
static void *sw_buffer;
static size_t sw_buffer_bytes;
static unsigned sw_frames, presented_frames;

static unsigned long long hash_buffer( const unsigned char *bytes, size_t count, int *nonzero )
{
	unsigned long long h = 1469598103934665603ull;
	size_t i;
	*nonzero = 0;
	for( i = 0; i < count; i++ )
	{
		if( bytes[i] ) *nonzero = 1;
		h ^= bytes[i];
		h *= 1099511628211ull;
	}
	return h;
}

static void report_frame( const char *source )
{
	presented_frames++;
	if( presented_frames == 1 || presented_frames % PS5_XASH_FRAME_HASH_INTERVAL == 0 )
	{
		int nonzero = 0;
		unsigned long long h = sw_buffer ? hash_buffer( sw_buffer, sw_buffer_bytes, &nonzero ) : 0;
		(void)ps5log_printf( PS5LOG_MARK, "XASH_FRAME source=%s presented=%u width=%d height=%d hash=%016llx nonzero=%d",
			source, presented_frames, ps5_vidmode.width, ps5_vidmode.height, h, nonzero );
	}
}

void Platform_Minimize_f( void )
{
}

void GL_SwapBuffers( void )
{
	report_frame( "swap" );
}

qboolean R_Init_Video( ref_graphic_apis_t type )
{
	(void)type;
	if( !VID_SetMode( ))
		return false;
	host.renderinfo_changed = false;
	return true;
}

void R_Free_Video( void )
{
	if( sw_buffer )
	{
		free( sw_buffer );
		sw_buffer = NULL;
		sw_buffer_bytes = 0;
	}
}

rserr_t R_ChangeDisplaySettings( int width, int height, window_mode_t window_mode )
{
	(void)width; (void)height; (void)window_mode;
	R_SaveVideoMode( ps5_vidmode.width, ps5_vidmode.height, ps5_vidmode.width, ps5_vidmode.height, false );
	return rserr_ok;
}

qboolean VID_SetMode( void )
{
	return R_ChangeDisplaySettings( ps5_vidmode.width, ps5_vidmode.height, WINDOW_MODE_FULLSCREEN ) == rserr_ok;
}

int R_MaxVideoModes( void )
{
	return 1;
}

struct vidmode_s *R_GetVideoMode( int num )
{
	return num == 0 ? &ps5_vidmode : NULL;
}

void *GL_GetProcAddress( const char *name )
{
	(void)name;
	return NULL;
}

void GL_UpdateSwapInterval( void )
{
}

int GL_SetAttribute( int attr, int val )
{
	(void)attr; (void)val;
	return 0;
}

int GL_GetAttribute( int attr, int *val )
{
	(void)attr;
	if( val ) *val = 0;
	return 0;
}

qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	size_t bytes = (size_t)width * (size_t)height * 4;
	void *mem = malloc( bytes );
	if( !mem )
		return false;
	memset( mem, 0, bytes );
	if( sw_buffer )
		free( sw_buffer );
	sw_buffer = mem;
	sw_buffer_bytes = bytes;
	*stride = (uint)width;
	*bpp = 4;
	*r = 0x00ff0000u;
	*g = 0x0000ff00u;
	*b = 0x000000ffu;
	(void)ps5log_printf( PS5LOG_INFO, "XASH_SW_BUFFER width=%d height=%d bpp=4 bytes=%zu", width, height, bytes );
	return true;
}

void *SW_LockBuffer( void )
{
	sw_frames++;
	return sw_buffer;
}

void SW_UnlockBuffer( void )
{
	report_frame( "software" );
}

ref_window_type_t R_GetWindowHandle( void **handle, ref_window_type_t type )
{
	(void)type;
	if( handle ) *handle = NULL;
	return REF_WINDOW_TYPE_NULL;
}

void VID_Info_f( void )
{
	Con_Printf( "PS5 headless video: %dx%d, software buffer %s, %u frames presented\n",
		ps5_vidmode.width, ps5_vidmode.height, sw_buffer ? "allocated" : "none", presented_frames );
}

void Platform_SetStatus( const char *status )
{
	(void)status;
}

qboolean Platform_DebuggerPresent( void )
{
	return false;
}

platform_orientation_t Platform_GetDisplayOrientation( void )
{
	return ORIENTATION_UNKNOWN;
}

