/*
audio_gate_ps5.c - XASH_AUDIO_GATE driver for the PS5 SceAudioOut backend
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Feeds the deterministic 44.1 kHz pattern through exactly the core that
xash/platform_ps5/s_ps5.c will use, so the hardware gate exercises the ring,
the resampler, the worker and AudioOut without the Phase 6 client. The gate is
self-contained and runs before the dedicated engine host starts.
*/

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "audio_ps5.h"
#include "audio_pattern_ps5.h"
#include "ps5_xash_build.h"
#include "ps5log.h"

#include <string.h>
#include <time.h>

/* 8192 stereo frames is ~186 ms of 44.1 kHz headroom and a power of two. */
#define GATE_RING_FRAMES 8192u
#define GATE_PRIME_FRAMES 1024u
#define GATE_CHUNK_FRAMES 512u
/* Bounded producer: ~30 s at the 2 ms idle backoff, far beyond a 1.5 s pattern. */
#define GATE_MAX_IDLE_WAITS 15000

static int16_t gate_ring[GATE_RING_FRAMES * PS5_AUDIO_CHANNELS];

static void gate_sleep( void )
{
	struct timespec idle;
	idle.tv_sec = 0;
	idle.tv_nsec = 2000000L;
	nanosleep( &idle, NULL );
}

/* Writes one chunk at the producer cursor, splitting it across the ring end. */
static uint32_t gate_fill( uint64_t produced, uint32_t count )
{
	const uint32_t position = (uint32_t)( produced & ( GATE_RING_FRAMES - 1 ));
	const uint32_t first = GATE_RING_FRAMES - position < count
		? GATE_RING_FRAMES - position : count;
	uint32_t written = PS5_AudioPatternFill( produced,
		&gate_ring[(size_t)position * PS5_AUDIO_CHANNELS], first );

	if( written == first && count > first )
	{
		written += PS5_AudioPatternFill( produced + first, gate_ring,
			count - first );
	}
	return written;
}

/*
XASH_AUDIO_GATE_FRAMES caps the pattern for the minimal ABI smoke run: open,
volume, a handful of whole blocks, drain and close, with its own expected hash.
0 runs the full audible sequence.
*/
#ifndef PS5_XASH_AUDIO_GATE_FRAMES
#define PS5_XASH_AUDIO_GATE_FRAMES 0
#endif

int PS5_AudioGateRun( int32_t user_id )
{
	const uint32_t available = PS5_AudioPatternFrames( );
	const uint32_t pattern_frames = PS5_XASH_AUDIO_GATE_FRAMES > 0 &&
		(uint32_t)PS5_XASH_AUDIO_GATE_FRAMES < available
		? (uint32_t)PS5_XASH_AUDIO_GATE_FRAMES : available;
	const uint64_t expected_hash = PS5_AudioPatternHashPrefix( pattern_frames );
	const uint32_t segments = PS5_AudioPatternSegmentCount( );
	struct ps5_audio_config config;
	struct ps5_audio_ring ring;
	const struct ps5_audio_stats *stats;
	uint64_t produced = 0, expected_resampled;
	uint32_t index;
	int waits = 0, result, shutdown_result, pass;

	memset( &config, 0, sizeof( config ));
	config.user_id = user_id;
	config.type = PS5_AUDIO_PORT_TYPE_MAIN;
	config.index = PS5_AUDIO_PORT_INDEX;
	config.grain = PS5_AUDIO_GRAIN;
	config.input_rate = PS5_AUDIO_INPUT_RATE;
	config.output_rate = PS5_AUDIO_OUTPUT_RATE;
	config.format = PS5_AUDIO_FORMAT_S16_STEREO;
	config.prime_frames = GATE_PRIME_FRAMES;
	ring.frames = gate_ring;
	ring.capacity = GATE_RING_FRAMES;

	PS5_AudioSetSink( NULL );
	result = PS5_AudioInit( &config, &ring );
	if( result != 0 )
	{
		(void)ps5log_printf( PS5LOG_ERR, "XASH_AUDIO_GATE_ABORT init_rc=%d", result );
		return result;
	}
	stats = PS5_AudioStats( );

	for( index = 0; index < segments; index++ )
	{
		const struct ps5_audio_pattern_segment *segment = PS5_AudioPatternSegment( index );
		(void)ps5log_printf( PS5LOG_INFO,
			"XASH_AUDIO_PATTERN_SEGMENT schema=1 index=%u frames=%u frequency_hz=%u amplitude=%d",
			index, segment->frames, segment->frequency, (int)segment->amplitude );
	}
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_AUDIO_PATTERN schema=1 segments=%u frames=%u available_frames=%u "
		"silent_frames=%u rate=%u channels=%u width=2 source_hash=0x%016llx",
		segments, pattern_frames, available,
		pattern_frames == available ? PS5_AudioPatternSilentFrames( ) : 0,
		(unsigned)PS5_AUDIO_INPUT_RATE, (unsigned)PS5_AUDIO_CHANNELS,
		(unsigned long long)expected_hash );

	result = PS5_AudioStartWorker( );
	if( result != 0 )
	{
		(void)ps5log_printf( PS5LOG_ERR, "XASH_AUDIO_GATE_ABORT worker_rc=%d", result );
		(void)PS5_AudioShutdown( );
		return result;
	}

	while( produced < pattern_frames )
	{
		uint32_t space, want, written;

		PS5_AudioLock( );
		space = GATE_RING_FRAMES - (uint32_t)( produced - stats->frames_consumed );
		PS5_AudioUnlock( );
		want = pattern_frames - (uint32_t)produced;
		if( want > GATE_CHUNK_FRAMES ) want = GATE_CHUNK_FRAMES;
		if( space < want )
		{
			if( ++waits > GATE_MAX_IDLE_WAITS )
			{
				(void)ps5log_printf( PS5LOG_ERR,
					"XASH_AUDIO_GATE_STALLED produced=%llu consumed=%llu",
					(unsigned long long)produced,
					(unsigned long long)stats->frames_consumed );
				break;
			}
			gate_sleep( );
			continue;
		}
		written = gate_fill( produced, want );
		if( written != want )
		{
			(void)ps5log_printf( PS5LOG_ERR,
				"XASH_AUDIO_GATE_PATTERN_SHORT offset=%llu want=%u written=%u",
				(unsigned long long)produced, want, written );
			break;
		}
		produced += written;
		PS5_AudioLock( );
		PS5_AudioPublish( produced );
		PS5_AudioUnlock( );
	}

	shutdown_result = PS5_AudioShutdown( );

	/*
	Exact expected output count: priming consumes one source frame, then the
	resampler emits ceil(M * den / num) frames for the remaining M.
	*/
	expected_resampled = stats->frames_consumed > 0
		? (( stats->frames_consumed - 1 ) * PS5_AUDIO_RATIO_DEN + PS5_AUDIO_RATIO_NUM - 1 )
			/ PS5_AUDIO_RATIO_NUM
		: 0;

	pass = shutdown_result == 0 &&
		stats->handle >= 0 && stats->init_result >= 0 &&
		stats->open_result >= 0 && stats->volume_result >= 0 &&
		stats->output_errors == 0 && stats->underruns == 0 &&
		stats->discarded_frames == 0 &&
		stats->frames_produced == pattern_frames &&
		stats->frames_consumed == pattern_frames &&
		stats->source_hash == expected_hash &&
		( pattern_frames != available ||
			stats->silent_source_frames >= PS5_AudioPatternSilentFrames( )) &&
		stats->blocks_sent > 0 &&
		stats->frames_sent == stats->blocks_sent * PS5_AUDIO_GRAIN &&
		stats->frames_sent == expected_resampled + stats->padding_frames &&
		stats->padding_frames < PS5_AUDIO_GRAIN &&
		stats->drain_calls == 1 && stats->close_calls == 1 &&
		stats->join_calls == 1 && stats->drain_result >= 0 &&
		stats->close_result == 0 &&
		stats->state == PS5_AUDIO_STATE_STOPPED;

	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_AUDIO_COMPLETE schema=1 shutdown_rc=%d produced=%llu consumed=%llu "
		"sent=%llu blocks=%llu expected_resampled=%llu padding=%llu discarded=%llu "
		"underruns=%llu output_errors=%d silent=%llu wraps=%llu high_water=%u "
		"source_hash=0x%016llx expected_source_hash=0x%016llx output_hash=0x%016llx "
		"input_rate=%u output_rate=%u ratio=%u/%u ownership=%s pass=%d",
		shutdown_result,
		(unsigned long long)stats->frames_produced,
		(unsigned long long)stats->frames_consumed,
		(unsigned long long)stats->frames_sent,
		(unsigned long long)stats->blocks_sent,
		(unsigned long long)expected_resampled,
		(unsigned long long)stats->padding_frames,
		(unsigned long long)stats->discarded_frames,
		(unsigned long long)stats->underruns,
		stats->output_errors,
		(unsigned long long)stats->silent_source_frames,
		(unsigned long long)stats->ring_wraps,
		stats->ring_high_water,
		(unsigned long long)stats->source_hash,
		(unsigned long long)expected_hash,
		(unsigned long long)stats->output_hash,
		stats->input_rate, stats->output_rate,
		(unsigned)PS5_AUDIO_RATIO_NUM, (unsigned)PS5_AUDIO_RATIO_DEN,
		stats->drain_calls == 1 && stats->close_calls == 1 && stats->join_calls == 1
			? "exact" : "failed",
		pass );
	return pass ? 0 : -1;
}
