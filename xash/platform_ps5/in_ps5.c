/*
in_ps5.c - native ScePad backend for Xash3D on PS5
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The native contract and batching rules are derived from the independently
authored, device-tested ps5-native-gamepad-input-research project at commit
16e9b953b26a7102bc801a380f08fbf00060d84b (GPL-3.0).  This file is a C
adapter owned by the Xash3D port; no vendor header is included.
*/

#include "in_ps5.h"
#include "ps5_platform.h"
#include "ps5log.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef PS5_XASH_MODE_CLIENT
#define PS5_XASH_MODE_CLIENT 0
#endif

#if PS5_XASH_MODE_CLIENT
#include "common.h"
#include "input.h"
#include "keydefs.h"
#include "client.h"
#include "ps5_xash_build.h"
#include "pad_aim.h"
#endif

#define PS5_PAD_MAX_SAMPLES 64
#define PS5_PAD_AXIS_ACTIVE 4096
#define PS5_PAD_BUTTON_CREATE UINT32_C(0x00000001)
#define PS5_PAD_BUTTON_L3 UINT32_C(0x00000002)
#define PS5_PAD_BUTTON_R3 UINT32_C(0x00000004)
#define PS5_PAD_BUTTON_OPTIONS UINT32_C(0x00000008)
#define PS5_PAD_BUTTON_UP UINT32_C(0x00000010)
#define PS5_PAD_BUTTON_RIGHT UINT32_C(0x00000020)
#define PS5_PAD_BUTTON_DOWN UINT32_C(0x00000040)
#define PS5_PAD_BUTTON_LEFT UINT32_C(0x00000080)
#define PS5_PAD_BUTTON_L2 UINT32_C(0x00000100)
#define PS5_PAD_BUTTON_R2 UINT32_C(0x00000200)
#define PS5_PAD_BUTTON_L1 UINT32_C(0x00000400)
#define PS5_PAD_BUTTON_R1 UINT32_C(0x00000800)
#define PS5_PAD_BUTTON_TRIANGLE UINT32_C(0x00001000)
#define PS5_PAD_BUTTON_CIRCLE UINT32_C(0x00002000)
#define PS5_PAD_BUTTON_CROSS UINT32_C(0x00004000)
#define PS5_PAD_BUTTON_SQUARE UINT32_C(0x00008000)
#define PS5_PAD_BUTTON_TOUCHPAD UINT32_C(0x00100000)
#define PS5_PAD_BUTTON_INTERCEPTED UINT32_C(0x80000000)

struct ps5_pad_button_map {
	uint32_t mask;
	enum ps5_xash_pad_button button;
};

static const struct ps5_pad_button_map button_map[] = {
	{ PS5_PAD_BUTTON_CROSS, PS5_XASH_BUTTON_A },
	{ PS5_PAD_BUTTON_CIRCLE, PS5_XASH_BUTTON_B },
	{ PS5_PAD_BUTTON_SQUARE, PS5_XASH_BUTTON_X },
	{ PS5_PAD_BUTTON_TRIANGLE, PS5_XASH_BUTTON_Y },
	{ PS5_PAD_BUTTON_L1, PS5_XASH_BUTTON_L1 },
	{ PS5_PAD_BUTTON_R1, PS5_XASH_BUTTON_R1 },
	{ PS5_PAD_BUTTON_CREATE, PS5_XASH_BUTTON_BACK },
	{ PS5_PAD_BUTTON_OPTIONS, PS5_XASH_BUTTON_START },
	{ PS5_PAD_BUTTON_L3, PS5_XASH_BUTTON_L3 },
	{ PS5_PAD_BUTTON_R3, PS5_XASH_BUTTON_R3 },
	{ PS5_PAD_BUTTON_L2, PS5_XASH_BUTTON_L2 },
	{ PS5_PAD_BUTTON_R2, PS5_XASH_BUTTON_R2 },
	{ PS5_PAD_BUTTON_UP, PS5_XASH_BUTTON_DPAD_UP },
	{ PS5_PAD_BUTTON_RIGHT, PS5_XASH_BUTTON_DPAD_RIGHT },
	{ PS5_PAD_BUTTON_DOWN, PS5_XASH_BUTTON_DPAD_DOWN },
	{ PS5_PAD_BUTTON_LEFT, PS5_XASH_BUTTON_DPAD_LEFT },
	{ PS5_PAD_BUTTON_TOUCHPAD, PS5_XASH_BUTTON_TOUCHPAD },
};

static struct {
	struct ps5_xash_pad_sink sink;
	struct ps5_xash_pad_stats stats;
	uint32_t previous_buttons;
	int16_t axes[PS5_XASH_AXIS_COUNT];
	uint8_t connected_count;
	int generation_valid;
	int movement_active;
	int look_active;
	int jump_active;
	int crouch_active;
	int use_active;
	int fire_active;
	int initialized;
	int opened;
} pad;

static int16_t normalize_stick( uint8_t value )
{
	int value32 = ((int)value - 128) * 256;
	if( value32 < INT16_MIN ) value32 = INT16_MIN;
	if( value32 > INT16_MAX ) value32 = INT16_MAX;
	return (int16_t)value32;
}

static int16_t normalize_trigger( uint8_t value )
{
	return (int16_t)(((unsigned)value * (unsigned)INT16_MAX + 127u) / 255u);
}

static int axis_active( int16_t x, int16_t y )
{
	return x < -PS5_PAD_AXIS_ACTIVE || x > PS5_PAD_AXIS_ACTIVE ||
		y < -PS5_PAD_AXIS_ACTIVE || y > PS5_PAD_AXIS_ACTIVE;
}

static void emit_axis( enum ps5_xash_pad_axis axis, int16_t value )
{
	if( pad.axes[axis] == value )
		return;
	pad.axes[axis] = value;
	pad.stats.axis_events++;
	if( pad.sink.axis )
		pad.sink.axis( pad.sink.opaque, axis, value );
}

static void emit_button( enum ps5_xash_pad_button button, int down )
{
	pad.stats.button_events++;
	if( pad.sink.button )
		pad.sink.button( pad.sink.opaque, button, down );
}

static void log_action( const char *name, int down, uint64_t timestamp )
{
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_PAD_ACTION schema=1 name=%s state=%s timestamp_us=%llu",
		name, down ? "pressed" : "released",
		(unsigned long long)timestamp );
}

static void log_axis_action( const char *name, int active, uint64_t timestamp )
{
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_PAD_ACTION schema=1 name=%s state=%s timestamp_us=%llu",
		name, active ? "active" : "neutral",
		(unsigned long long)timestamp );
}

static void update_action( const char *name, int down, int *previous,
	uint64_t *presses, uint64_t *releases, uint64_t timestamp )
{
	if( down == *previous )
		return;
	*previous = down;
	if( down ) ( *presses )++;
	else ( *releases )++;
	log_action( name, down, timestamp );
}

static void process_buttons( uint32_t current, uint64_t timestamp )
{
	const uint32_t changed = current ^ pad.previous_buttons;
	size_t index;
	int jump = ( current & PS5_PAD_BUTTON_CROSS ) != 0;
	int crouch = ( current & ( PS5_PAD_BUTTON_L1 | PS5_PAD_BUTTON_R3 )) != 0;
	int use = ( current & PS5_PAD_BUTTON_CIRCLE ) != 0;
	int fire = ( current & PS5_PAD_BUTTON_R1 ) != 0;

	for( index = 0; index < sizeof( button_map ) / sizeof( button_map[0] ); index++ )
	{
		if( changed & button_map[index].mask )
			emit_button( button_map[index].button,
				( current & button_map[index].mask ) != 0 );
	}
	update_action( "jump", jump, &pad.jump_active, &pad.stats.jump_presses,
		&pad.stats.jump_releases, timestamp );
	update_action( "crouch", crouch, &pad.crouch_active,
		&pad.stats.crouch_presses, &pad.stats.crouch_releases, timestamp );
	update_action( "use", use, &pad.use_active, &pad.stats.use_presses,
		&pad.stats.use_releases, timestamp );
	update_action( "fire", fire, &pad.fire_active, &pad.stats.fire_presses,
		&pad.stats.fire_releases, timestamp );
	pad.previous_buttons = current;
}

static void process_axes( int16_t side, int16_t forward, int16_t pitch,
	int16_t yaw, int16_t right_trigger, int16_t left_trigger,
	uint64_t timestamp, int count_activity )
{
	const int movement = axis_active( side, forward );
	const int look = axis_active( yaw, pitch );
	if( count_activity && movement ) pad.stats.movement_samples++;
	if( count_activity && look ) pad.stats.look_samples++;
	if( movement != pad.movement_active )
	{
		pad.movement_active = movement;
		log_axis_action( "movement", movement, timestamp );
	}
	if( look != pad.look_active )
	{
		pad.look_active = look;
		log_axis_action( "look", look, timestamp );
	}
#if PS5_XASH_MODE_CLIENT
	if (Cvar_VariableValue("ps5_aim_enable") != 0)
		ps5_pad_aim(&yaw, &pitch, Cvar_VariableValue("ps5_aim_deadzone"),
			Cvar_VariableValue("ps5_aim_exponent"));
#endif
	emit_axis( PS5_XASH_AXIS_SIDE, side );
	emit_axis( PS5_XASH_AXIS_FORWARD, forward );
	emit_axis( PS5_XASH_AXIS_PITCH, pitch );
	emit_axis( PS5_XASH_AXIS_YAW, yaw );
	emit_axis( PS5_XASH_AXIS_RIGHT_TRIGGER, right_trigger );
	emit_axis( PS5_XASH_AXIS_LEFT_TRIGGER, left_trigger );
}

static void process_neutral( uint64_t timestamp )
{
	process_buttons( 0, timestamp );
	process_axes( 0, 0, 0, 0, 0, 0, timestamp, 0 );
}

#if PS5_XASH_MODE_CLIENT
static const int xash_button_map[PS5_XASH_BUTTON_COUNT] = {
	K_A_BUTTON, K_B_BUTTON, K_X_BUTTON, K_Y_BUTTON,
	K_L1_BUTTON, K_R1_BUTTON, K_BACK_BUTTON, K_START_BUTTON,
	K_LSTICK, K_RSTICK, K_L2_BUTTON, K_R2_BUTTON,
	K_DPAD_UP, K_DPAD_RIGHT, K_DPAD_DOWN, K_DPAD_LEFT, K_TOUCHPAD
};

static void xash_axis_event( void *opaque, enum ps5_xash_pad_axis axis, int16_t value )
{
	(void)opaque;
	Joy_AxisMotionEvent((engineAxis_t)axis, value );
}

static void xash_button_event( void *opaque, enum ps5_xash_pad_button button, int down )
{
	(void)opaque;
#if PS5_XASH_STUDIO_AB
	if(button == PS5_XASH_BUTTON_TOUCHPAD && cls.state == ca_active) {
		if(down) {
			int unlit = Cvar_VariableValue("r_agc_studio_unlit") == 0;
			Cvar_SetValue("r_agc_studio_unlit", unlit);
			CL_CenterPrint(unlit ? "STUDIO B: SIN ILUMINACION" : "STUDIO A: ILUMINACION NORMAL", 0.15f);
			(void)ps5log_printf(PS5LOG_MARK,
				"XASH_STUDIO_AB_REQUEST schema=1 unlit=%d source=touchpad", unlit);
		}
		return;
	}
#endif
#if PS5_XASH_SAMPLING_PROBE
	if( button == PS5_XASH_BUTTON_TOUCHPAD && cls.state == ca_active )
	{
		if( down )
		{
			float current = Cvar_VariableValue( "r_agc_qa_mode" );
			int next = current >= 0.0f && current < 3.0f ? (int)current + 1 : 0;
			Cvar_SetValue( "r_agc_qa_mode", (float)next );
			(void)ps5log_printf( PS5LOG_MARK,
				"XASH_QA_MODE_REQUEST schema=1 mode=%d source=touchpad", next );
		}
		return;
	}
#endif
	if( button >= 0 && button < PS5_XASH_BUTTON_COUNT )
		Key_Event( xash_button_map[button], down );
}
#endif

void PS5_PadInputSetSink( const struct ps5_xash_pad_sink *sink )
{
	if( sink ) pad.sink = *sink;
	else memset( &pad.sink, 0, sizeof( pad.sink ));
}

int PS5_PadInputInit( void )
{
	struct ps5_xash_pad_sink sink = pad.sink;
	int result;
	memset( &pad, 0, sizeof( pad ));
	pad.sink = sink;
	pad.stats.user_id = -1;
	pad.stats.pad_handle = -1;
	pad.stats.close_result = INT_MIN;
	pad.stats.terminate_result = INT_MIN;
#if PS5_XASH_MODE_CLIENT
	if( !pad.sink.axis && !pad.sink.button )
	{
		pad.sink.axis = xash_axis_event;
		pad.sink.button = xash_button_event;
	}
#endif

	result = sceUserServiceInitialize( NULL );
	pad.stats.user_service_result = result;
	pad.stats.user_service_owned = result == 0;
	/* ShadowMount/LNC launches can leave the process initial user detached from
	 * the currently assigned DualSense.  The foreground user is the owner used
	 * by the already hardware-proven BSP viewer on this console. */
	result = sceUserServiceGetForegroundUser( &pad.stats.user_id );
	if( result < 0 || pad.stats.user_id < 0 )
		goto failed;
	result = scePadInit( );
	pad.stats.pad_init_result = result;
	if( result < 0 )
		goto failed;
	result = scePadOpen( pad.stats.user_id, 0, 0, NULL );
	pad.stats.pad_handle = result;
	if( result < 0 )
		goto failed;
	pad.opened = 1;
	pad.initialized = 1;
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_PAD_INIT schema=1 user_service_rc=%d owns_user_service=%d "
		"user_id=%d pad_init_rc=%d handle=%d read=scePadRead batch=64",
		pad.stats.user_service_result, pad.stats.user_service_owned,
		pad.stats.user_id, pad.stats.pad_init_result, pad.stats.pad_handle );
	return 0;

failed:
	(void)ps5log_printf( PS5LOG_ERR,
		"XASH_PAD_INIT_FAILED schema=1 stage_rc=%d user_service_rc=%d "
		"owns_user_service=%d user_id=%d pad_init_rc=%d handle=%d",
		result, pad.stats.user_service_result, pad.stats.user_service_owned,
		pad.stats.user_id, pad.stats.pad_init_result, pad.stats.pad_handle );
	if( pad.stats.user_service_owned )
	{
		pad.stats.terminate_result = sceUserServiceTerminate( );
		pad.stats.user_service_owned = 0;
	}
	return result < 0 ? result : -1;
}

int PS5_PadInputPoll( void )
{
	struct ps5_pad_data samples[PS5_PAD_MAX_SAMPLES];
	int count, index;
	if( !pad.initialized || !pad.opened )
		return -1;
	pad.stats.polls++;
	count = scePadRead( pad.stats.pad_handle, samples, PS5_PAD_MAX_SAMPLES );
	if( count < 0 )
	{
		pad.stats.read_errors++;
		(void)ps5log_printf( PS5LOG_ERR,
			"XASH_PAD_READ_ERROR schema=1 poll=%llu rc=%d errors=%llu",
			(unsigned long long)pad.stats.polls, count,
			(unsigned long long)pad.stats.read_errors );
		process_neutral( 0 );
		return count;
	}
	if( count == 0 )
	{
		pad.stats.empty_reads++;
		return 0;
	}
	if( count > PS5_PAD_MAX_SAMPLES )
		count = PS5_PAD_MAX_SAMPLES;
	pad.stats.samples += (uint64_t)count;
	if((uint64_t)count > pad.stats.max_batch ) pad.stats.max_batch = (uint64_t)count;
	for( index = 0; index < count; index++ )
	{
		const struct ps5_pad_data *sample = &samples[index];
		const int intercepted = ( sample->buttons & PS5_PAD_BUTTON_INTERCEPTED ) != 0;
		const int usable = sample->connected != 0 && !intercepted;
		if( !pad.generation_valid || sample->connected_count != pad.connected_count )
		{
			process_neutral( sample->timestamp );
			pad.connected_count = sample->connected_count;
			pad.generation_valid = 1;
			pad.stats.generation_changes++;
			(void)ps5log_printf( PS5LOG_INFO,
				"XASH_PAD_GENERATION schema=1 generation=%u changes=%llu timestamp_us=%llu",
				(unsigned)pad.connected_count,
				(unsigned long long)pad.stats.generation_changes,
				(unsigned long long)sample->timestamp );
		}
		if( !usable )
		{
			if( intercepted ) pad.stats.intercepted_samples++;
			else pad.stats.disconnected_samples++;
			process_neutral( sample->timestamp );
			continue;
		}
		pad.stats.connected_samples++;
		process_buttons( sample->buttons & ~PS5_PAD_BUTTON_INTERCEPTED,
			sample->timestamp );
		process_axes( normalize_stick( sample->left_stick.x ),
			normalize_stick( sample->left_stick.y ),
			normalize_stick( sample->right_stick.y ),
			normalize_stick( sample->right_stick.x ),
			normalize_trigger( sample->r2 ),
			normalize_trigger( sample->l2 ), sample->timestamp, 1 );
	}
	return count;
}

int PS5_PadInputGatePassed( void )
{
	return pad.stats.read_errors == 0 && pad.stats.connected_samples > 0 &&
		pad.stats.movement_samples > 0 && pad.stats.look_samples > 0 &&
		pad.stats.jump_presses > 0 && pad.stats.jump_releases > 0 &&
		pad.stats.crouch_presses > 0 && pad.stats.crouch_releases > 0 &&
		pad.stats.use_presses > 0 && pad.stats.use_releases > 0 &&
		pad.stats.fire_presses > 0 && pad.stats.fire_releases > 0;
}

int PS5_PadInputShutdown( void )
{
	int result = 0;
	const int gate_passed = PS5_PadInputGatePassed( );
	if( pad.initialized )
		process_neutral( 0 );
	if( pad.opened )
	{
		pad.stats.close_result = scePadClose( pad.stats.pad_handle );
		if( pad.stats.close_result < 0 ) result = pad.stats.close_result;
		pad.opened = 0;
	}
	if( pad.stats.user_service_owned )
	{
		pad.stats.terminate_result = sceUserServiceTerminate( );
		if( pad.stats.terminate_result < 0 && result == 0 )
			result = pad.stats.terminate_result;
		pad.stats.user_service_owned = 0;
	}
	pad.initialized = 0;
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_PAD_SUMMARY schema=1 polls=%llu samples=%llu empty_reads=%llu "
		"max_batch=%llu connected=%llu disconnected=%llu intercepted=%llu "
		"generation_changes=%llu axis_events=%llu button_events=%llu "
		"movement=%llu look=%llu jump=%llu/%llu crouch=%llu/%llu "
		"use=%llu/%llu fire=%llu/%llu read_errors=%llu",
		(unsigned long long)pad.stats.polls,
		(unsigned long long)pad.stats.samples,
		(unsigned long long)pad.stats.empty_reads,
		(unsigned long long)pad.stats.max_batch,
		(unsigned long long)pad.stats.connected_samples,
		(unsigned long long)pad.stats.disconnected_samples,
		(unsigned long long)pad.stats.intercepted_samples,
		(unsigned long long)pad.stats.generation_changes,
		(unsigned long long)pad.stats.axis_events,
		(unsigned long long)pad.stats.button_events,
		(unsigned long long)pad.stats.movement_samples,
		(unsigned long long)pad.stats.look_samples,
		(unsigned long long)pad.stats.jump_presses,
		(unsigned long long)pad.stats.jump_releases,
		(unsigned long long)pad.stats.crouch_presses,
		(unsigned long long)pad.stats.crouch_releases,
		(unsigned long long)pad.stats.use_presses,
		(unsigned long long)pad.stats.use_releases,
		(unsigned long long)pad.stats.fire_presses,
		(unsigned long long)pad.stats.fire_releases,
		(unsigned long long)pad.stats.read_errors );
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_PAD_TEARDOWN schema=1 handle=%d close_rc=%d owned_user_service=%d "
		"terminate_rc=%d result=%d",
		pad.stats.pad_handle, pad.stats.close_result,
		pad.stats.user_service_result == 0, pad.stats.terminate_result, result );
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_PAD_COMPLETE schema=1 movement=%d look=%d jump=%d crouch=%d "
		"use=%d fire=%d chronological_batches=%d ownership=%s errors=%llu pass=%d",
		pad.stats.movement_samples > 0, pad.stats.look_samples > 0,
		pad.stats.jump_presses > 0 && pad.stats.jump_releases > 0,
		pad.stats.crouch_presses > 0 && pad.stats.crouch_releases > 0,
		pad.stats.use_presses > 0 && pad.stats.use_releases > 0,
		pad.stats.fire_presses > 0 && pad.stats.fire_releases > 0,
		pad.stats.max_batch > 0,
		pad.stats.close_result == 0 &&
			( pad.stats.user_service_result != 0 || pad.stats.terminate_result == 0 )
			? "exact" : "failed",
		(unsigned long long)pad.stats.read_errors,
		gate_passed && result == 0 );
	return result;
}

const struct ps5_xash_pad_stats *PS5_PadInputStats( void )
{
	return &pad.stats;
}

/* Normal client input is independent of the dedicated six-action gate.
 * Start only after client signon, when Joy/Key/Cvar are initialized. */
static int runtime_state;
int PS5_PadInputRuntimePoll( void )
{
	if( runtime_state == 0 )
	{
		int result = PS5_PadInputInit( );
		runtime_state = result == 0 ? 1 : -1;
		(void)ps5log_printf( result == 0 ? PS5LOG_MARK : PS5LOG_ERR,
			"XASH_PAD_RUNTIME_BEGIN schema=1 result=%d autoquit=0", result );
	}
	return runtime_state == 1 ? PS5_PadInputPoll( ) : -1;
}

int PS5_PadInputRuntimeShutdown( void )
{
	if( runtime_state == 0 ) return 0;
	/* Host_Main may already have destroyed input/cvars. Do not send events
	 * into those subsystems while releasing the platform-owned handle. */
	PS5_PadInputSetSink( NULL );
	int result = PS5_PadInputShutdown( );
	runtime_state = 0;
	(void)ps5log_printf( result == 0 ? PS5LOG_MARK : PS5LOG_ERR,
		"XASH_PAD_RUNTIME_END schema=1 result=%d ownership=exact", result );
	return result;
}
