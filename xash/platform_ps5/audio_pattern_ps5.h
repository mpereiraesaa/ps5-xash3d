/*
audio_pattern_ps5.h - deterministic PCM pattern for the PS5 SceAudioOut gate
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

An unmistakable low tone / silence / high tone sequence at the Xash3D mix rate.
Generation is integer only and addressable by absolute frame index, so the
source hash is bit exact on the host and on the console, and the operator can
recognise the sequence by ear.
*/

#ifndef XASH_PLATFORM_PS5_AUDIO_PATTERN_PS5_H
#define XASH_PLATFORM_PS5_AUDIO_PATTERN_PS5_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ps5_audio_pattern_segment {
	uint32_t frames;
	uint32_t frequency; /* 0 marks deliberate silence */
	int16_t amplitude;
};

uint32_t PS5_AudioPatternSegmentCount( void );
const struct ps5_audio_pattern_segment *PS5_AudioPatternSegment( uint32_t index );
uint32_t PS5_AudioPatternFrames( void );
uint32_t PS5_AudioPatternSilentFrames( void );

/*
Writes min( count, remaining ) interleaved stereo frames starting at the
absolute frame `offset` and returns how many it wrote.
*/
uint32_t PS5_AudioPatternFill( uint64_t offset, int16_t *frames, uint32_t count );

/* FNV-1a 64 over the whole pattern, in PS5_AudioHashFrames form. */
uint64_t PS5_AudioPatternHash( void );
/* Same, over the first `frames` frames, for a bounded smoke run. */
uint64_t PS5_AudioPatternHashPrefix( uint32_t frames );

#ifdef __cplusplus
}
#endif

#endif
