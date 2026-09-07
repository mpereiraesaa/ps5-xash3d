/*
audio_ps5.c - native SceAudioOut PCM backend for Xash3D on PS5
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The AudioOut call sequence, the grain/rate/format constraints, the blocking
Output pacing and the "no short final block, zero-fill the tail" rule are
documented by the independently authored, device-tested
ps5-audio-decoding-research project at commit
2c81f17910be6e7b26d05ae50f50adb0211581c2 (GPL-3.0). That evidence is FW 6.02.
This file is a C core owned by the Xash3D port: no vendor header is included
and no symbol is accepted until this port exercises it on FW 12.02.

Ownership contract:
  - the producer owns the ring memory and the published frame counter;
  - the worker owns the AudioOut handle and is the only caller of Output,
    the NULL-drain and Close;
  - the mutex is never held across the blocking Output.
*/

/* nanosleep and pthread_* under a strict -std=c11 host build. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "audio_ps5.h"
#include "ps5_platform.h"
#include "ps5log.h"

#include <pthread.h>
#include <string.h>
#include <time.h>

/* One progress line every 64 blocks (~341 ms of output), never one per block. */
#define PS5_AUDIO_PROGRESS_BLOCKS 64
/* Idle backoff when the ring has nothing to stage; Output does the real pacing. */
#define PS5_AUDIO_IDLE_NS 2000000L

static struct {
	struct ps5_audio_config config;
	struct ps5_audio_ring ring;
	struct ps5_audio_sink sink;
	struct ps5_audio_stats stats;
	pthread_mutex_t lock;
	pthread_t worker;
	uint64_t consumed;
	uint64_t next_progress;
	uint32_t ratio_num; /* input_rate / gcd */
	uint32_t ratio_den; /* output_rate / gcd */
	int16_t stage[PS5_AUDIO_STAGE_FRAMES * PS5_AUDIO_CHANNELS];
	int16_t accum[PS5_AUDIO_ACCUM_FRAMES * PS5_AUDIO_CHANNELS];
	uint32_t accum_frames;
	int16_t resample_prev[PS5_AUDIO_CHANNELS];
	uint32_t resample_frac;
	int resample_primed;
	int in_underrun;
	int stop_requested;
	int worker_running;
	int lock_ready;
	int opened;
	int initialized;
	int finished;
} audio;

int PS5_AudioAdvanceSamplePos( int samplepos, int samples, uint32_t frames )
{
	if( samples <= 0 )
		return 0;
	samplepos += (int)( frames * PS5_AUDIO_CHANNELS );
	while( samplepos >= samples )
		samplepos -= samples;
	return samplepos;
}

uint32_t PS5_AudioRingFrames( int samplecount )
{
	uint32_t frames = 1;
	if( samplecount <= 0 )
		return 0;
	while( frames * 2u <= (uint32_t)samplecount )
		frames *= 2u;
	return frames;
}

int PS5_AudioIsRebase( int painted, int published )
{
	return painted < published;
}

static int16_t clamp16( int32_t value )
{
	if( value < -32768 ) return (int16_t)-32768;
	if( value > 32767 ) return (int16_t)32767;
	return (int16_t)value;
}

void PS5_AudioSetSink( const struct ps5_audio_sink *sink )
{
	if( sink ) audio.sink = *sink;
	else memset( &audio.sink, 0, sizeof( audio.sink ));
}

void PS5_AudioLock( void )
{
	if( audio.lock_ready )
		pthread_mutex_lock( &audio.lock );
}

void PS5_AudioUnlock( void )
{
	if( audio.lock_ready )
		pthread_mutex_unlock( &audio.lock );
}

void PS5_AudioPublish( uint64_t frames_produced )
{
	if( frames_produced > audio.stats.frames_produced )
		audio.stats.frames_produced = frames_produced;
}

void PS5_AudioRebase( uint64_t frames_produced )
{
	audio.stats.rebases++;
	audio.stats.discarded_frames += audio.stats.frames_produced - audio.consumed;
	audio.stats.frames_produced = frames_produced;
	audio.consumed = frames_produced;
	audio.in_underrun = 0;
	(void)ps5log_printf( PS5LOG_WARN,
		"XASH_AUDIO_REBASE schema=1 count=%llu base=%llu discarded=%llu",
		(unsigned long long)audio.stats.rebases,
		(unsigned long long)frames_produced,
		(unsigned long long)audio.stats.discarded_frames );
}

const struct ps5_audio_stats *PS5_AudioStats( void )
{
	return &audio.stats;
}

static int power_of_two( uint32_t value )
{
	return value != 0 && ( value & ( value - 1 )) == 0;
}

static uint32_t greatest_common_divisor( uint32_t a, uint32_t b )
{
	while( b )
	{
		const uint32_t remainder = a % b;
		a = b;
		b = remainder;
	}
	return a;
}

int PS5_AudioInit( const struct ps5_audio_config *config, const struct ps5_audio_ring *ring )
{
	int32_t volumes[PS5_AUDIO_VOLUME_CHANNELS];
	struct ps5_audio_sink sink = audio.sink;
	uint32_t divisor, ratio_num, ratio_den, worst_case_output;
	int index, result;

	if( !config || !ring || !ring->frames || !power_of_two( ring->capacity ))
		return -1;
	if( config->grain == 0 || config->grain > PS5_AUDIO_ACCUM_FRAMES )
		return -1;
	if( config->input_rate == 0 || config->output_rate == 0 )
		return -1;
	if( config->prime_frames >= ring->capacity )
		return -1;
	if( audio.initialized )
		return -1;

	/*
	The accumulator must absorb one staging chunk's output plus the sub-block
	remainder, otherwise a step would have to drop source frames. Reject such a
	configuration instead of losing audio at runtime.
	*/
	divisor = greatest_common_divisor( config->input_rate, config->output_rate );
	ratio_num = config->input_rate / divisor;
	ratio_den = config->output_rate / divisor;
	worst_case_output = ( PS5_AUDIO_STAGE_FRAMES * ratio_den + ratio_num - 1 ) / ratio_num;
	if( worst_case_output + config->grain - 1 > PS5_AUDIO_ACCUM_FRAMES )
		return -1;

	memset( &audio, 0, sizeof( audio ));
	audio.sink = sink;
	audio.config = *config;
	audio.ring = *ring;
	audio.ratio_num = ratio_num;
	audio.ratio_den = ratio_den;
	audio.stats.handle = -1;
	audio.stats.state = PS5_AUDIO_STATE_CLOSED;
	/* Both rolling hashes start at the FNV offset basis, not at the zero the
	   memset above leaves, or they cannot be compared with a recorded value. */
	audio.stats.source_hash = PS5_AUDIO_HASH_SEED;
	audio.stats.output_hash = PS5_AUDIO_HASH_SEED;
	audio.stats.ring_capacity = ring->capacity;
	audio.stats.prime_frames = config->prime_frames;
	audio.stats.grain = config->grain;
	audio.stats.input_rate = config->input_rate;
	audio.stats.output_rate = config->output_rate;
	audio.stats.format = config->format;
	audio.stats.user_id = config->user_id;
	audio.stats.type = config->type;
	audio.stats.index = config->index;

	if( pthread_mutex_init( &audio.lock, NULL ) != 0 )
		return -1;
	audio.lock_ready = 1;

	/*
	sceAudioOutInit is process wide and safe to repeat, and the reference
	lifecycle calls it on every open. Calling it unconditionally keeps the
	recorded init_result real evidence instead of a cached zero.
	*/
	audio.stats.init_result = sceAudioOutInit( );
	if( audio.stats.init_result < 0 )
	{
		result = audio.stats.init_result;
		goto rollback;
	}

	audio.stats.open_result = sceAudioOutOpen( config->user_id, config->type,
		config->index, config->grain, config->output_rate, config->format );
	if( audio.stats.open_result < 0 )
	{
		result = audio.stats.open_result;
		goto rollback;
	}
	audio.stats.handle = audio.stats.open_result;
	audio.opened = 1;

	for( index = 0; index < PS5_AUDIO_VOLUME_CHANNELS; index++ )
		volumes[index] = PS5_AUDIO_VOLUME_0DB;
	audio.stats.volume_result = sceAudioOutSetVolume( audio.stats.handle,
		PS5_AUDIO_VOLUME_FLAGS, volumes );
	if( audio.stats.volume_result < 0 )
	{
		result = audio.stats.volume_result;
		goto rollback;
	}

	audio.initialized = 1;
	audio.stats.state = PS5_AUDIO_STATE_PRIMING;
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_AUDIO_INIT schema=1 user=0x%x type=%d index=%d handle=%d "
		"init_rc=%d open_rc=%d volume_rc=%d volume_flags=%d volume_value=0x%x "
		"input_rate=%u output_rate=%u format=%u channels=%u grain=%u",
		(unsigned)config->user_id, config->type, config->index, audio.stats.handle,
		audio.stats.init_result, audio.stats.open_result, audio.stats.volume_result,
		PS5_AUDIO_VOLUME_FLAGS, (unsigned)PS5_AUDIO_VOLUME_0DB,
		config->input_rate, config->output_rate, config->format,
		PS5_AUDIO_CHANNELS, config->grain );
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_AUDIO_RING_READY schema=1 capacity_frames=%u prime_frames=%u "
		"stage_frames=%u accum_frames=%u ratio=%u/%u",
		audio.stats.ring_capacity, audio.stats.prime_frames,
		(unsigned)PS5_AUDIO_STAGE_FRAMES, (unsigned)PS5_AUDIO_ACCUM_FRAMES,
		audio.ratio_num, audio.ratio_den );
	return 0;

rollback:
	/* The worker does not exist yet, so closing here keeps handle ownership exact. */
	if( audio.opened )
	{
		audio.stats.close_result = sceAudioOutClose( audio.stats.handle );
		audio.stats.close_calls++;
		audio.opened = 0;
	}
	(void)ps5log_printf( PS5LOG_ERR,
		"XASH_AUDIO_INIT_FAILED schema=1 stage_rc=%d init_rc=%d open_rc=%d "
		"volume_rc=%d close_rc=%d",
		result, audio.stats.init_result, audio.stats.open_result,
		audio.stats.volume_result, audio.stats.close_result );
	audio.stats.state = PS5_AUDIO_STATE_FAILED;
	audio.stats.handle = -1;
	pthread_mutex_destroy( &audio.lock );
	audio.lock_ready = 0;
	return result < 0 ? result : -1;
}

/*
Continuous linear resampler. Output frame k sits at input position
k * num / den, so exactly num source frames are consumed per den output
frames. resample_prev and resample_frac persist across calls, which is what
keeps block boundaries free of discontinuities.
*/
static uint32_t resample_append( const int16_t *input, uint32_t frames )
{
	const uint32_t num = audio.ratio_num;
	const uint32_t den = audio.ratio_den;
	uint32_t index = 0;

	if( !audio.resample_primed )
	{
		if( frames == 0 )
			return 0;
		audio.resample_prev[0] = input[0];
		audio.resample_prev[1] = input[1];
		audio.resample_frac = 0;
		audio.resample_primed = 1;
		index = 1;
	}
	while( index < frames && audio.accum_frames < PS5_AUDIO_ACCUM_FRAMES )
	{
		const int32_t next_left = input[index * 2];
		const int32_t next_right = input[index * 2 + 1];
		const int32_t prev_left = audio.resample_prev[0];
		const int32_t prev_right = audio.resample_prev[1];
		int16_t *out = &audio.accum[audio.accum_frames * 2];

		out[0] = clamp16( prev_left + (int32_t)
			((int64_t)( next_left - prev_left ) * audio.resample_frac / den ));
		out[1] = clamp16( prev_right + (int32_t)
			((int64_t)( next_right - prev_right ) * audio.resample_frac / den ));
		audio.accum_frames++;

		audio.resample_frac += num;
		if( audio.resample_frac >= den )
		{
			audio.resample_frac -= den;
			audio.resample_prev[0] = (int16_t)next_left;
			audio.resample_prev[1] = (int16_t)next_right;
			index++;
		}
	}
	return index;
}

/* Submit one whole block and shift the remainder down. Never holds the lock. */
static int submit_block( void )
{
	int result = sceAudioOutOutput( audio.stats.handle, audio.accum );
	const uint32_t grain = audio.config.grain;

	audio.stats.last_output_result = result;
	if( result < 0 )
	{
		audio.stats.output_errors++;
		audio.stats.state = PS5_AUDIO_STATE_FAILED;
		(void)ps5log_printf( PS5LOG_ERR,
			"XASH_AUDIO_OUTPUT_ERROR schema=1 rc=%d blocks=%llu frames_sent=%llu",
			result, (unsigned long long)audio.stats.blocks_sent,
			(unsigned long long)audio.stats.frames_sent );
		return result;
	}
	audio.stats.output_hash = PS5_AudioHashFrames( audio.stats.output_hash,
		audio.accum, (size_t)grain * PS5_AUDIO_CHANNELS );
	audio.stats.blocks_sent++;
	audio.stats.frames_sent += grain;
	audio.accum_frames -= grain;
	if( audio.accum_frames )
	{
		memmove( audio.accum, &audio.accum[grain * PS5_AUDIO_CHANNELS],
			(size_t)audio.accum_frames * PS5_AUDIO_CHANNELS * sizeof( int16_t ));
	}
	if( audio.stats.blocks_sent >= audio.next_progress )
	{
		audio.next_progress = audio.stats.blocks_sent + PS5_AUDIO_PROGRESS_BLOCKS;
		(void)ps5log_printf( PS5LOG_INFO,
			"XASH_AUDIO_PROGRESS schema=1 produced=%llu consumed=%llu sent=%llu "
			"blocks=%llu high_water=%u wraps=%llu underruns=%llu silent=%llu",
			(unsigned long long)audio.stats.frames_produced,
			(unsigned long long)audio.stats.frames_consumed,
			(unsigned long long)audio.stats.frames_sent,
			(unsigned long long)audio.stats.blocks_sent,
			audio.stats.ring_high_water,
			(unsigned long long)audio.stats.ring_wraps,
			(unsigned long long)audio.stats.underruns,
			(unsigned long long)audio.stats.silent_source_frames );
	}
	return 0;
}

/*
Stage source frames under the lock, then resample and submit outside it. The
producer is excluded only for the copy, never for the blocking Output.
*/
int PS5_AudioWorkerStep( void )
{
	uint32_t want, pending, read_pos, first, frame;
	int blocks = 0;

	if( !audio.initialized || audio.stats.state == PS5_AUDIO_STATE_FAILED )
		return -1;

	PS5_AudioLock( );
	if( audio.stats.paused )
	{
		PS5_AudioUnlock( );
		return 0;
	}
	pending = (uint32_t)( audio.stats.frames_produced - audio.consumed );
	if( pending > audio.stats.ring_high_water )
		audio.stats.ring_high_water = pending;
	if( audio.stats.state == PS5_AUDIO_STATE_PRIMING )
	{
		/* Filling the initial buffer is not an underrun. */
		if( pending < audio.config.prime_frames )
		{
			PS5_AudioUnlock( );
			return 0;
		}
		audio.stats.state = PS5_AUDIO_STATE_PLAYING;
	}
	if( pending == 0 )
	{
		if( audio.stats.state == PS5_AUDIO_STATE_PLAYING && !audio.in_underrun )
		{
			audio.in_underrun = 1;
			audio.stats.underruns++;
			(void)ps5log_printf( PS5LOG_WARN,
				"XASH_AUDIO_UNDERRUN schema=1 episode=%llu produced=%llu consumed=%llu "
				"sent=%llu blocks=%llu",
				(unsigned long long)audio.stats.underruns,
				(unsigned long long)audio.stats.frames_produced,
				(unsigned long long)audio.stats.frames_consumed,
				(unsigned long long)audio.stats.frames_sent,
				(unsigned long long)audio.stats.blocks_sent );
		}
		PS5_AudioUnlock( );
		return 0;
	}
	audio.in_underrun = 0;

	want = pending < PS5_AUDIO_STAGE_FRAMES ? pending : PS5_AUDIO_STAGE_FRAMES;
	read_pos = (uint32_t)( audio.consumed & (uint64_t)( audio.ring.capacity - 1 ));
	first = audio.ring.capacity - read_pos;
	if( first > want ) first = want;
	memcpy( audio.stage, &audio.ring.frames[(size_t)read_pos * PS5_AUDIO_CHANNELS],
		(size_t)first * PS5_AUDIO_CHANNELS * sizeof( int16_t ));
	if( want > first )
	{
		memcpy( &audio.stage[(size_t)first * PS5_AUDIO_CHANNELS], audio.ring.frames,
			(size_t)( want - first ) * PS5_AUDIO_CHANNELS * sizeof( int16_t ));
	}
	/*
	Count how many times the read cursor passed the end of the ring, not how
	many copies happened to straddle it: an aligned chunk size never splits and
	would otherwise report zero wraps forever.
	*/
	audio.stats.ring_wraps += ( audio.consumed + want ) / audio.ring.capacity
		- audio.consumed / audio.ring.capacity;
	audio.consumed += want;
	audio.stats.frames_consumed += want;
	audio.stats.source_hash = PS5_AudioHashFrames( audio.stats.source_hash,
		audio.stage, (size_t)want * PS5_AUDIO_CHANNELS );
	for( frame = 0; frame < want; frame++ )
	{
		if( audio.stage[frame * 2] == 0 && audio.stage[frame * 2 + 1] == 0 )
			audio.stats.silent_source_frames++;
	}
	if( audio.sink.consumed )
		audio.sink.consumed( audio.sink.opaque, want );
	PS5_AudioUnlock( );

	/* The init contract sizes the accumulator so a whole chunk always fits. */
	if( resample_append( audio.stage, want ) != want )
	{
		audio.stats.state = PS5_AUDIO_STATE_FAILED;
		(void)ps5log_printf( PS5LOG_ERR,
			"XASH_AUDIO_RESAMPLE_OVERRUN schema=1 staged=%u accum=%u", want,
			audio.accum_frames );
		return -1;
	}
	while( audio.accum_frames >= audio.config.grain )
	{
		int result = submit_block( );
		if( result < 0 )
			return result;
		blocks++;
	}
	return blocks;
}

/*
Terminal sequence, all of it on the worker: consume the queue within a bound,
zero-fill the one partial block, drain once, close once.
*/
static int worker_finish( void )
{
	int steps = 0, result = 0;
	uint32_t pending;

	/* A run shorter than the prime level still plays out; it never primes. */
	PS5_AudioLock( );
	if( audio.stats.state == PS5_AUDIO_STATE_PRIMING )
		audio.stats.state = PS5_AUDIO_STATE_PLAYING;
	PS5_AudioUnlock( );

	while( steps++ < PS5_AUDIO_DRAIN_STEPS )
	{
		int stepped;
		PS5_AudioLock( );
		pending = (uint32_t)( audio.stats.frames_produced - audio.consumed );
		PS5_AudioUnlock( );
		if( pending == 0 )
			break;
		stepped = PS5_AudioWorkerStep( );
		if( stepped < 0 )
		{
			result = stepped;
			break;
		}
	}
	PS5_AudioLock( );
	pending = (uint32_t)( audio.stats.frames_produced - audio.consumed );
	audio.stats.discarded_frames += pending;
	audio.consumed += pending;
	PS5_AudioUnlock( );

	if( audio.stats.state != PS5_AUDIO_STATE_FAILED && audio.accum_frames )
	{
		const uint32_t pad = audio.config.grain - audio.accum_frames;
		memset( &audio.accum[audio.accum_frames * PS5_AUDIO_CHANNELS], 0,
			(size_t)pad * PS5_AUDIO_CHANNELS * sizeof( int16_t ));
		audio.accum_frames = audio.config.grain;
		audio.stats.padding_frames += pad;
		if( submit_block( ) < 0 && result == 0 )
			result = audio.stats.last_output_result;
	}
	if( audio.opened )
	{
		audio.stats.drain_result = sceAudioOutOutput( audio.stats.handle, NULL );
		audio.stats.drain_calls++;
		if( audio.stats.drain_result < 0 && result == 0 )
			result = audio.stats.drain_result;
		audio.stats.close_result = sceAudioOutClose( audio.stats.handle );
		audio.stats.close_calls++;
		if( audio.stats.close_result < 0 && result == 0 )
			result = audio.stats.close_result;
		audio.opened = 0;
	}
	if( audio.stats.state != PS5_AUDIO_STATE_FAILED )
		audio.stats.state = PS5_AUDIO_STATE_STOPPED;
	audio.finished = 1;
	return result;
}

static void *worker_main( void *opaque )
{
	(void)opaque;
	for( ;; )
	{
		int stop, blocks;
		PS5_AudioLock( );
		stop = audio.stop_requested;
		PS5_AudioUnlock( );
		if( stop )
			break;
		blocks = PS5_AudioWorkerStep( );
		if( blocks < 0 )
			break;
		if( blocks == 0 )
		{
			struct timespec idle;
			idle.tv_sec = 0;
			idle.tv_nsec = PS5_AUDIO_IDLE_NS;
			nanosleep( &idle, NULL );
		}
	}
	(void)worker_finish( );
	return NULL;
}

int PS5_AudioStartWorker( void )
{
	if( !audio.initialized || audio.worker_running )
		return -1;
	if( pthread_create( &audio.worker, NULL, worker_main, NULL ) != 0 )
		return -1;
	audio.worker_running = 1;
	return 0;
}

int PS5_AudioActivate( int active )
{
	if( !audio.initialized )
		return -1;
	PS5_AudioLock( );
	audio.stats.paused = !active;
	/* A pause is not an underrun; the next step re-arms the episode flag. */
	if( !active )
		audio.in_underrun = 0;
	PS5_AudioUnlock( );
	return 0;
}

int PS5_AudioShutdown( void )
{
	int result = 0;

	if( !audio.initialized )
		return 0;

	/* 1. no further publication, 2. ask the worker to finish. */
	PS5_AudioLock( );
	audio.stop_requested = 1;
	audio.stats.paused = 0;
	PS5_AudioUnlock( );

	if( audio.worker_running )
	{
		/* 3-5 happen on the worker; the handle is never closed from here. */
		pthread_join( audio.worker, NULL );
		audio.stats.join_calls++;
		audio.worker_running = 0;
	}
	else if( !audio.finished )
	{
		result = worker_finish( );
	}
	if( audio.stats.output_errors && result == 0 )
		result = audio.stats.last_output_result;
	if( audio.stats.close_result < 0 && result == 0 )
		result = audio.stats.close_result;

	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_AUDIO_SUMMARY schema=1 produced=%llu consumed=%llu sent=%llu "
		"blocks=%llu silent=%llu underruns=%llu padding=%llu discarded=%llu "
		"wraps=%llu rebases=%llu high_water=%u capacity=%u prime=%u grain=%u "
		"input_rate=%u output_rate=%u format=%u channels=%u "
		"source_hash=0x%016llx output_hash=0x%016llx output_errors=%d",
		(unsigned long long)audio.stats.frames_produced,
		(unsigned long long)audio.stats.frames_consumed,
		(unsigned long long)audio.stats.frames_sent,
		(unsigned long long)audio.stats.blocks_sent,
		(unsigned long long)audio.stats.silent_source_frames,
		(unsigned long long)audio.stats.underruns,
		(unsigned long long)audio.stats.padding_frames,
		(unsigned long long)audio.stats.discarded_frames,
		(unsigned long long)audio.stats.ring_wraps,
		(unsigned long long)audio.stats.rebases,
		audio.stats.ring_high_water, audio.stats.ring_capacity,
		audio.stats.prime_frames, audio.stats.grain,
		audio.stats.input_rate, audio.stats.output_rate, audio.stats.format,
		PS5_AUDIO_CHANNELS,
		(unsigned long long)audio.stats.source_hash,
		(unsigned long long)audio.stats.output_hash,
		audio.stats.output_errors );
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_AUDIO_TEARDOWN schema=1 handle=%d drain_rc=%d drain_calls=%d "
		"close_rc=%d close_calls=%d join_calls=%d owner=worker state=%d result=%d",
		audio.stats.handle, audio.stats.drain_result, audio.stats.drain_calls,
		audio.stats.close_result, audio.stats.close_calls, audio.stats.join_calls,
		audio.stats.state, result );

	if( audio.lock_ready )
	{
		pthread_mutex_destroy( &audio.lock );
		audio.lock_ready = 0;
	}
	audio.initialized = 0;
	memset( &audio.ring, 0, sizeof( audio.ring ));
	memset( &audio.sink, 0, sizeof( audio.sink ));
	audio.accum_frames = 0;
	audio.resample_primed = 0;
	return result;
}
