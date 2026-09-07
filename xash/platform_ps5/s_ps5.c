/*
s_ps5.c - Xash3D SNDDMA binding for the native PS5 SceAudioOut backend
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

A thin binding, not a second implementation: the ring is Xash3D's own DMA
buffer and everything else lives in xash/platform_ps5/audio_ps5.c.

  snd.samples    mono samples in snd.buffer, so snd.samples >> 1 stereo frames
  snd.samplepos  read cursor in mono samples; S_GetSoundtime derives soundtime
                 from it, so the consumer must advance it by frames * 2
  snd.paintedtime  stereo frames the mixer has produced, and the exact ring
                 index the engine writes at (S_TransferPaintBuffer masks it
                 with (snd.samples >> 1) - 1), which is the same masking the
                 core applies to its consumer cursor

BeginPainting takes the core lock so the worker cannot read frames the mixer is
still writing; Submit publishes snd.paintedtime and releases it. The worker
copies under that lock and calls the blocking sceAudioOutOutput only after
dropping it.
*/

#include "common.h"
#include "platform.h"
#include "sound.h"
#include "voice.h"

#include "audio_ps5.h"

/* 32768 stereo frames, ~0.74 s at 44.1 kHz: the engine default, already a power of two. */
#define PS5_SOUND_DEFAULT_SAMPLECOUNT 0x8000
#define PS5_SOUND_PRIME_FRAMES 2048

static const char *const ps5_backend_name = "SceAudioOut (main, s16 stereo 48 kHz)";
static int ps5_sound_painting;
static int ps5_sound_published;

static void ps5_sound_consumed( void *opaque, uint32_t frames )
{
	(void)opaque;
	/* Called with the core lock held, so the mixer is excluded. */
	snd.samplepos = PS5_AudioAdvanceSamplePos( snd.samplepos, snd.samples, frames );
}

static int ps5_sound_frames( void )
{
	int samplecount = (int)s_samplecount.value;
	int frames;

	if( samplecount <= 0 )
		samplecount = PS5_SOUND_DEFAULT_SAMPLECOUNT;
	frames = (int)PS5_AudioRingFrames( samplecount );
	if( frames != samplecount )
	{
		Con_Printf( S_NOTE "%s: s_samplecount %d is not a power of two, using %d\n",
			__func__, samplecount, frames );
	}
	return frames;
}

qboolean SNDDMA_Init( void )
{
	struct ps5_audio_config config;
	struct ps5_audio_ring ring;
	struct ps5_audio_sink sink;
	const int frames = ps5_sound_frames( );
	int result;

	memset( &config, 0, sizeof( config ));
	config.user_id = PS5_AUDIO_USER_SYSTEM;
	config.type = PS5_AUDIO_PORT_TYPE_MAIN;
	config.index = PS5_AUDIO_PORT_INDEX;
	config.grain = PS5_AUDIO_GRAIN;
	config.input_rate = PS5_AUDIO_INPUT_RATE;
	config.output_rate = PS5_AUDIO_OUTPUT_RATE;
	config.format = PS5_AUDIO_FORMAT_S16_STEREO;
	config.prime_frames = PS5_SOUND_PRIME_FRAMES;

	/*
	The engine keeps mixing at SOUND_DMA_SPEED because mixahead, streaming and
	raw-sample timing read that macro directly; audio_ps5.c converts 44.1 -> 48
	kHz on the way out instead.
	*/
	snd.format.speed = SOUND_DMA_SPEED;
	snd.format.channels = PS5_AUDIO_CHANNELS;
	snd.format.width = 2;
	snd.samples = frames * PS5_AUDIO_CHANNELS;
	snd.samplepos = 0;
	snd.buffer = Mem_Calloc( sndpool, snd.samples * 2 );

	ring.frames = (int16_t *)snd.buffer;
	ring.capacity = (uint32_t)frames;
	sink.consumed = ps5_sound_consumed;
	sink.opaque = NULL;
	PS5_AudioSetSink( &sink );

	result = PS5_AudioInit( &config, &ring );
	if( result != 0 )
	{
		Con_Printf( S_ERROR "%s: SceAudioOut init failed (%d)\n", __func__, result );
		PS5_AudioSetSink( NULL );
		Mem_Free( snd.buffer );
		snd.buffer = NULL;
		return false;
	}
	result = PS5_AudioStartWorker( );
	if( result != 0 )
	{
		Con_Printf( S_ERROR "%s: SceAudioOut worker failed (%d)\n", __func__, result );
		(void)PS5_AudioShutdown( );
		PS5_AudioSetSink( NULL );
		Mem_Free( snd.buffer );
		snd.buffer = NULL;
		return false;
	}

	ps5_sound_published = 0;
	snd.backend_name = ps5_backend_name;
	snd.initialized = true;
	Con_Printf( "Using audio driver: %s, %d frames, mix %d Hz\n",
		ps5_backend_name, frames, SOUND_DMA_SPEED );
	return true;
}

void SNDDMA_BeginPainting( void )
{
	if( !snd.initialized )
		return;
	PS5_AudioLock( );
	ps5_sound_painting = 1;
}

void SNDDMA_Submit( void )
{
	if( !snd.initialized || !ps5_sound_painting )
		return;
	/*
	Xash3D chops paintedtime back to one buffer length past 0x40000000, so a
	decrease is a deliberate restart rather than a lost update.
	*/
	if( PS5_AudioIsRebase( snd.paintedtime, ps5_sound_published ))
		PS5_AudioRebase( (uint64_t)snd.paintedtime );
	else
		PS5_AudioPublish( (uint64_t)snd.paintedtime );
	ps5_sound_published = snd.paintedtime;
	ps5_sound_painting = 0;
	PS5_AudioUnlock( );
}

void SNDDMA_Activate( qboolean active )
{
	if( !snd.initialized )
		return;
	(void)PS5_AudioActivate( active ? 1 : 0 );
}

void SNDDMA_Shutdown( void )
{
	int result;

	Con_Printf( "Shutting down audio.\n" );
	snd.initialized = false;
	ps5_sound_painting = 0;

	/* Joins the worker before the ring memory goes away. */
	result = PS5_AudioShutdown( );
	if( result != 0 )
		Con_Printf( S_ERROR "%s: SceAudioOut teardown returned %d\n", __func__, result );
	PS5_AudioSetSink( NULL );

	if( snd.buffer )
	{
		Mem_Free( snd.buffer );
		snd.buffer = NULL;
	}
}

/* AudioIn and the microphone are outside this gate. */
qboolean VoiceCapture_Init( void )
{
	return false;
}

qboolean VoiceCapture_Activate( qboolean activate )
{
	(void)activate;
	return false;
}

qboolean VoiceCapture_Lock( qboolean lock )
{
	(void)lock;
	return false;
}

void VoiceCapture_Shutdown( void )
{
}
