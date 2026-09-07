/*
audio_ps5.h - native SceAudioOut PCM backend contract for Xash3D on PS5
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The core below is deliberately independent of the Xash3D client: it owns a
producer/consumer PCM ring, a continuous 44.1 -> 48 kHz resampler, a dedicated
worker (sceAudioOutOutput blocks) and exact accounting. xash/platform_ps5/s_ps5.c
is a thin SNDDMA binding on top of it, and the XASH_AUDIO_GATE driver feeds the
same core a deterministic pattern.
*/

#ifndef XASH_PLATFORM_PS5_AUDIO_PS5_H
#define XASH_PLATFORM_PS5_AUDIO_PS5_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
Xash3D mixes at SOUND_DMA_SPEED (44100) and several engine paths compute
mixahead, streaming and raw-sample timing from that macro directly, so the
engine rate is never reassigned. AudioOut accepts 48000 or 192000 Hz, hence the
exact 147/160 rational conversion in this layer.
*/
#define PS5_AUDIO_INPUT_RATE 44100u
#define PS5_AUDIO_OUTPUT_RATE 48000u
#define PS5_AUDIO_RATIO_NUM 147u /* 44100 / 300 */
#define PS5_AUDIO_RATIO_DEN 160u /* 48000 / 300 */
#define PS5_AUDIO_CHANNELS 2u
#define PS5_AUDIO_GRAIN 256u
#define PS5_AUDIO_BLOCK_SHORTS ( PS5_AUDIO_GRAIN * PS5_AUDIO_CHANNELS )

/* Port selection and data format accepted by the main stereo signed-16 port. */
#define PS5_AUDIO_PORT_TYPE_MAIN 0
#define PS5_AUDIO_PORT_INDEX 0
#define PS5_AUDIO_USER_SYSTEM 0xff
#define PS5_AUDIO_FORMAT_S16_STEREO 1u
#define PS5_AUDIO_VOLUME_FLAGS 3
#define PS5_AUDIO_VOLUME_0DB 0x8000
#define PS5_AUDIO_VOLUME_CHANNELS 8

/* Source frames copied out of the ring per worker step. */
#define PS5_AUDIO_STAGE_FRAMES 1024u
/*
Worst case output for PS5_AUDIO_STAGE_FRAMES source frames is
ceil(1024 * 160 / 147) = 1115, and a step never leaves a whole block behind,
so the accumulator holds at most 1115 + (grain - 1) frames.
*/
#define PS5_AUDIO_ACCUM_FRAMES 1536u

/* Bounded terminal consumption: any frame still pending afterwards is counted
   as discarded rather than silently dropped. */
#define PS5_AUDIO_DRAIN_STEPS 512

enum ps5_audio_state {
	PS5_AUDIO_STATE_CLOSED = 0,
	PS5_AUDIO_STATE_PRIMING,
	PS5_AUDIO_STATE_PLAYING,
	PS5_AUDIO_STATE_STOPPED,
	PS5_AUDIO_STATE_FAILED
};

/*
Interleaved stereo signed-16 ring owned by the producer. The Xash3D binding
passes snd.buffer directly; the gate driver passes its own allocation. capacity
is in stereo frames and must be a power of two.
*/
struct ps5_audio_ring {
	int16_t *frames;
	uint32_t capacity;
};

struct ps5_audio_config {
	int32_t user_id;
	int32_t type;
	int32_t index;
	uint32_t grain;
	uint32_t input_rate;
	uint32_t output_rate;
	uint32_t format;
	uint32_t prime_frames; /* source frames required before the first Output */
};

/*
Called with the ring lock held once the worker has copied source frames out,
so the producer can release that space (the Xash3D binding advances
snd.samplepos by frames * 2 mono samples).
*/
struct ps5_audio_sink {
	void ( *consumed )( void *opaque, uint32_t frames );
	void *opaque;
};

struct ps5_audio_stats {
	uint64_t frames_produced;      /* source frames published by the producer */
	uint64_t frames_consumed;      /* source frames taken by the worker */
	uint64_t frames_sent;          /* 48 kHz frames handed to AudioOut */
	uint64_t blocks_sent;          /* Output calls the port accepted */
	uint64_t silent_source_frames; /* deliberate silence carried through */
	uint64_t underruns;            /* episodes, counted once each */
	uint64_t padding_frames;       /* terminal zero-fill only */
	uint64_t discarded_frames;     /* pending frames left by the drain bound */
	uint64_t ring_wraps;
	uint64_t rebases;              /* producer counter restarts (see PS5_AudioRebase) */
	uint32_t ring_high_water;      /* peak pending source frames */
	uint64_t source_hash;          /* FNV-1a 64 over consumed 44.1 kHz PCM */
	uint64_t output_hash;          /* FNV-1a 64 over sent 48 kHz PCM */
	uint32_t ring_capacity;
	uint32_t prime_frames;
	uint32_t grain;
	uint32_t input_rate;
	uint32_t output_rate;
	uint32_t format;
	int32_t user_id;
	int32_t type;
	int32_t index;
	int init_result;
	int open_result;
	int volume_result;
	int handle;
	int output_errors;
	int last_output_result;
	int drain_result;
	int close_result;
	int drain_calls;
	int close_calls;
	int join_calls;
	int paused;
	int state;
};

/*
Xash3D DMA cursor arithmetic. It lives here, not in s_ps5.c, so the host tests
cover it without the engine header set; the binding is then a trivial adapter.
*/
/* Advances snd.samplepos (mono samples) by `frames` stereo frames, wrapping at `samples`. */
int PS5_AudioAdvanceSamplePos( int samplepos, int samples, uint32_t frames );
/* Largest power of two not greater than `samplecount`; the ring index is masked. */
uint32_t PS5_AudioRingFrames( int samplecount );
/* True when snd.paintedtime moved backwards, i.e. the engine chopped it. */
int PS5_AudioIsRebase( int painted, int published );

/*
FNV-1a 64 over little-endian signed-16 samples; the gate records the hash before
and after the resampler. Inline and dependency free so the pattern unit can be
tested without linking the AudioOut core.
*/
#define PS5_AUDIO_HASH_SEED UINT64_C(0xcbf29ce484222325)
#define PS5_AUDIO_HASH_PRIME UINT64_C(0x100000001b3)

static inline uint64_t PS5_AudioHashFrames( uint64_t seed, const int16_t *samples,
	size_t count )
{
	size_t index;
	for( index = 0; index < count; index++ )
	{
		const uint16_t value = (uint16_t)samples[index];
		seed ^= (uint64_t)( value & 0xffu );
		seed *= PS5_AUDIO_HASH_PRIME;
		seed ^= (uint64_t)(( value >> 8 ) & 0xffu );
		seed *= PS5_AUDIO_HASH_PRIME;
	}
	return seed;
}

void PS5_AudioSetSink( const struct ps5_audio_sink *sink );
/*
Runs sceAudioOutInit, sceAudioOutOpen and sceAudioOutSetVolume on the caller's
thread so a failure is reported synchronously, then hands the handle to the
worker. Any failed acquisition rolls the whole sequence back.
*/
int PS5_AudioInit( const struct ps5_audio_config *config, const struct ps5_audio_ring *ring );
int PS5_AudioStartWorker( void );
void PS5_AudioLock( void );
void PS5_AudioUnlock( void );
/* Monotonic total of source frames the producer has written. Call under the lock. */
void PS5_AudioPublish( uint64_t frames_produced );
/*
Restarts both cursors at frames_produced, dropping whatever was still pending.
Xash3D chops snd.paintedtime back to one buffer length once it passes
0x40000000, which makes the producer counter jump backwards; without this the
consumer would starve forever after that. Call under the lock.
*/
void PS5_AudioRebase( uint64_t frames_produced );
int PS5_AudioActivate( int active );
/* Idempotent: stops publication, joins the worker exactly once, releases state. */
int PS5_AudioShutdown( void );
const struct ps5_audio_stats *PS5_AudioStats( void );

/*
Test seam. One worker iteration: stages source frames under the lock, releases
the lock, then resamples and submits whole blocks. Returns the number of blocks
submitted, or the negative Output result. The worker thread only loops on this.
*/
int PS5_AudioWorkerStep( void );

/*
XASH_AUDIO_GATE driver (xash/platform_ps5/audio_gate_ps5.c): pushes the
deterministic pattern through this same core - ring, resampler, worker and
AudioOut - and returns 0 only when every acceptance counter holds.
*/
int PS5_AudioGateRun( int32_t user_id );

#ifdef __cplusplus
}
#endif

#endif
