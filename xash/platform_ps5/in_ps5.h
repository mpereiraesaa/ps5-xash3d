/*
in_ps5.h - native ScePad backend contract for Xash3D on PS5
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#ifndef XASH_PLATFORM_PS5_IN_PS5_H
#define XASH_PLATFORM_PS5_IN_PS5_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ps5_xash_pad_axis {
	PS5_XASH_AXIS_SIDE = 0,
	PS5_XASH_AXIS_FORWARD,
	PS5_XASH_AXIS_PITCH,
	PS5_XASH_AXIS_YAW,
	PS5_XASH_AXIS_RIGHT_TRIGGER,
	PS5_XASH_AXIS_LEFT_TRIGGER,
	PS5_XASH_AXIS_COUNT
};

enum ps5_xash_pad_button {
	PS5_XASH_BUTTON_A = 0,
	PS5_XASH_BUTTON_B,
	PS5_XASH_BUTTON_X,
	PS5_XASH_BUTTON_Y,
	PS5_XASH_BUTTON_L1,
	PS5_XASH_BUTTON_R1,
	PS5_XASH_BUTTON_BACK,
	PS5_XASH_BUTTON_START,
	PS5_XASH_BUTTON_L3,
	PS5_XASH_BUTTON_R3,
	PS5_XASH_BUTTON_L2,
	PS5_XASH_BUTTON_R2,
	PS5_XASH_BUTTON_DPAD_UP,
	PS5_XASH_BUTTON_DPAD_RIGHT,
	PS5_XASH_BUTTON_DPAD_DOWN,
	PS5_XASH_BUTTON_DPAD_LEFT,
	PS5_XASH_BUTTON_TOUCHPAD,
	PS5_XASH_BUTTON_COUNT
};

struct ps5_xash_pad_sink {
	void ( *axis )( void *opaque, enum ps5_xash_pad_axis axis, int16_t value );
	void ( *button )( void *opaque, enum ps5_xash_pad_button button, int down );
	void *opaque;
};

struct ps5_xash_pad_stats {
	uint64_t polls;
	uint64_t samples;
	uint64_t empty_reads;
	uint64_t max_batch;
	uint64_t read_errors;
	uint64_t connected_samples;
	uint64_t disconnected_samples;
	uint64_t intercepted_samples;
	uint64_t generation_changes;
	uint64_t axis_events;
	uint64_t button_events;
	uint64_t movement_samples;
	uint64_t look_samples;
	uint64_t jump_presses;
	uint64_t jump_releases;
	uint64_t crouch_presses;
	uint64_t crouch_releases;
	uint64_t use_presses;
	uint64_t use_releases;
	uint64_t fire_presses;
	uint64_t fire_releases;
	int user_service_result;
	int user_service_owned;
	int user_id;
	int pad_init_result;
	int pad_handle;
	int close_result;
	int terminate_result;
};

void PS5_PadInputSetSink( const struct ps5_xash_pad_sink *sink );
int PS5_PadInputInit( void );
int PS5_PadInputPoll( void );
int PS5_PadInputGatePassed( void );
int PS5_PadInputShutdown( void );
int PS5_PadInputRuntimePoll( void );
int PS5_PadInputRuntimeShutdown( void );
const struct ps5_xash_pad_stats *PS5_PadInputStats( void );

#ifdef __cplusplus
}
#endif

#endif
