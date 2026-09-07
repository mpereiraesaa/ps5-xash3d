/*
audio_pattern_ps5.c - deterministic PCM pattern for the PS5 SceAudioOut gate
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

A triangle wave keeps the sequence tonal and clearly audible while staying
integer only: no floating point means the host and the console agree on every
sample, so a single expected source hash covers both.
*/

#include "audio_pattern_ps5.h"
#include "audio_ps5.h"

/* 0.600 s low tone, 0.300 s silence, 0.600 s high tone at 44100 Hz. */
static const struct ps5_audio_pattern_segment pattern[] = {
	{ 26460, 220, 12000 },
	{ 13230, 0, 0 },
	{ 26460, 880, 12000 },
};

#define PATTERN_SEGMENTS ( sizeof( pattern ) / sizeof( pattern[0] ))

uint32_t PS5_AudioPatternSegmentCount( void )
{
	return (uint32_t)PATTERN_SEGMENTS;
}

const struct ps5_audio_pattern_segment *PS5_AudioPatternSegment( uint32_t index )
{
	if( index >= PATTERN_SEGMENTS )
		return NULL;
	return &pattern[index];
}

uint32_t PS5_AudioPatternFrames( void )
{
	uint32_t total = 0, index;
	for( index = 0; index < PATTERN_SEGMENTS; index++ )
		total += pattern[index].frames;
	return total;
}

uint32_t PS5_AudioPatternSilentFrames( void )
{
	uint32_t total = 0, index;
	for( index = 0; index < PATTERN_SEGMENTS; index++ )
	{
		if( pattern[index].frequency == 0 || pattern[index].amplitude == 0 )
			total += pattern[index].frames;
	}
	return total;
}

/* Q16 phase; the integer increment is what makes the sequence reproducible. */
static int16_t triangle( uint32_t phase, int16_t amplitude )
{
	const uint32_t q = phase & 0xffffu;
	int32_t value;

	if( q < 0x8000u )
		value = -32768 + (int32_t)( q * 2u );
	else
		value = 32767 - (int32_t)(( q - 0x8000u ) * 2u );
	return (int16_t)(( value * (int32_t)amplitude ) / 32768 );
}

uint32_t PS5_AudioPatternFill( uint64_t offset, int16_t *frames, uint32_t count )
{
	const uint32_t total = PS5_AudioPatternFrames( );
	uint32_t written = 0;

	if( !frames || offset >= total )
		return 0;
	if( count > total - (uint32_t)offset )
		count = total - (uint32_t)offset;

	while( written < count )
	{
		const uint32_t absolute = (uint32_t)offset + written;
		uint32_t segment = 0, base = 0, local, increment;
		int16_t sample;

		while( segment < PATTERN_SEGMENTS && absolute >= base + pattern[segment].frames )
		{
			base += pattern[segment].frames;
			segment++;
		}
		local = absolute - base;
		if( pattern[segment].frequency == 0 )
		{
			sample = 0;
		}
		else
		{
			increment = ( pattern[segment].frequency << 16 ) / PS5_AUDIO_INPUT_RATE;
			sample = triangle( local * increment, pattern[segment].amplitude );
		}
		frames[written * 2] = sample;
		frames[written * 2 + 1] = sample;
		written++;
	}
	return written;
}

uint64_t PS5_AudioPatternHashPrefix( uint32_t frames )
{
	uint64_t hash = PS5_AUDIO_HASH_SEED;
	const uint32_t available = PS5_AudioPatternFrames( );
	const uint32_t total = frames < available ? frames : available;
	int16_t chunk[256 * 2];
	uint32_t offset = 0;

	while( offset < total )
	{
		const uint32_t want = total - offset < 256 ? total - offset : 256;
		const uint32_t written = PS5_AudioPatternFill( offset, chunk, want );
		if( written == 0 )
			break;
		hash = PS5_AudioHashFrames( hash, chunk, (size_t)written * 2 );
		offset += written;
	}
	return hash;
}

uint64_t PS5_AudioPatternHash( void )
{
	return PS5_AudioPatternHashPrefix( PS5_AudioPatternFrames( ));
}
