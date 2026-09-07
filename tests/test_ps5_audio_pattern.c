/*
Host coverage for the deterministic SceAudioOut gate pattern: the segment
layout, offset-addressable generation, the silence segment, and the expected
source hash the console run has to reproduce exactly.
*/

#include "../xash/platform_ps5/audio_pattern_ps5.h"
#include "../xash/platform_ps5/audio_ps5.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/*
Recorded expectation. Generation is integer only, so this value must hold on
the host and on FW 12.02; XASH_AUDIO_PATTERN reports it from the console and
XASH_AUDIO_COMPLETE compares the consumed source hash against it.
*/
#define EXPECTED_PATTERN_FRAMES 66150u
#define EXPECTED_SILENT_FRAMES 13230u
#define EXPECTED_SOURCE_HASH UINT64_C(0x9fd6b8c32bb54595)
/* ceil((66150 - 1) * 160 / 147) output frames, then 282 whole 256-frame blocks. */
#define EXPECTED_RESAMPLED_FRAMES 71999u
#define EXPECTED_BLOCKS 282u
#define EXPECTED_PADDING_FRAMES 193u

static void test_layout( void )
{
	const struct ps5_audio_pattern_segment *low, *quiet, *high;

	assert( PS5_AudioPatternSegmentCount( ) == 3 );
	low = PS5_AudioPatternSegment( 0 );
	quiet = PS5_AudioPatternSegment( 1 );
	high = PS5_AudioPatternSegment( 2 );
	assert( low && quiet && high );
	assert( PS5_AudioPatternSegment( 3 ) == NULL );

	/* Low tone, silence, high tone: unmistakable by ear. */
	assert( low->frequency == 220 && low->amplitude > 0 );
	assert( quiet->frequency == 0 && quiet->amplitude == 0 );
	assert( high->frequency == 880 && high->amplitude > 0 );
	assert( high->frequency > low->frequency * 2 );

	/* Whole tenths of a second at the Xash mix rate. */
	assert( low->frames == PS5_AUDIO_INPUT_RATE * 6 / 10 );
	assert( quiet->frames == PS5_AUDIO_INPUT_RATE * 3 / 10 );
	assert( high->frames == PS5_AUDIO_INPUT_RATE * 6 / 10 );

	assert( PS5_AudioPatternFrames( ) == EXPECTED_PATTERN_FRAMES );
	assert( PS5_AudioPatternSilentFrames( ) == EXPECTED_SILENT_FRAMES );
	/* Exactly 1.500 s of source; the console run must report the same totals. */
	assert( PS5_AudioPatternFrames( ) == PS5_AUDIO_INPUT_RATE * 3 / 2 );
}

/* The counts XASH_AUDIO_COMPLETE has to report for this pattern. */
static void test_expected_output_accounting( void )
{
	const uint64_t source = PS5_AudioPatternFrames( );
	const uint64_t resampled = (( source - 1 ) * PS5_AUDIO_RATIO_DEN
		+ PS5_AUDIO_RATIO_NUM - 1 ) / PS5_AUDIO_RATIO_NUM;
	const uint64_t blocks = ( resampled + PS5_AUDIO_GRAIN - 1 ) / PS5_AUDIO_GRAIN;

	assert( resampled == EXPECTED_RESAMPLED_FRAMES );
	assert( blocks == EXPECTED_BLOCKS );
	assert( blocks * PS5_AUDIO_GRAIN - resampled == EXPECTED_PADDING_FRAMES );
	assert( EXPECTED_PADDING_FRAMES < PS5_AUDIO_GRAIN );
}

static void test_segments_are_audible_and_silent_where_expected( void )
{
	int16_t frames[512 * 2];
	uint32_t index;
	int nonzero = 0;

	/* The low tone actually swings. */
	assert( PS5_AudioPatternFill( 0, frames, 512 ) == 512 );
	for( index = 0; index < 512; index++ )
	{
		/* Mono duplicated into both channels. */
		assert( frames[index * 2] == frames[index * 2 + 1] );
		if( frames[index * 2] != 0 )
			nonzero++;
	}
	assert( nonzero > 256 );

	/* The silence segment is exactly zero on both channels. */
	assert( PS5_AudioPatternFill( 30000, frames, 512 ) == 512 );
	for( index = 0; index < 512 * 2; index++ )
		assert( frames[index] == 0 );

	/* The high tone swings again. */
	nonzero = 0;
	assert( PS5_AudioPatternFill( 45000, frames, 512 ) == 512 );
	for( index = 0; index < 512; index++ )
	{
		if( frames[index * 2] != 0 )
			nonzero++;
	}
	assert( nonzero > 256 );
}

/* The gate writes in ring-sized pieces, so any offset must match one long fill. */
static void test_offset_addressable( void )
{
	int16_t whole[900 * 2];
	int16_t piece[300 * 2];
	uint32_t offset;

	assert( PS5_AudioPatternFill( 26000, whole, 900 ) == 900 );
	for( offset = 0; offset < 900; offset += 300 )
	{
		assert( PS5_AudioPatternFill( 26000 + offset, piece, 300 ) == 300 );
		assert( memcmp( piece, &whole[offset * 2],
			300 * 2 * sizeof( int16_t )) == 0 );
	}
}

static void test_bounds( void )
{
	const uint32_t total = PS5_AudioPatternFrames( );
	int16_t frames[64 * 2];

	assert( PS5_AudioPatternFill( total, frames, 64 ) == 0 );
	assert( PS5_AudioPatternFill( total + 1000, frames, 64 ) == 0 );
	assert( PS5_AudioPatternFill( 0, NULL, 64 ) == 0 );
	/* A request past the end is clamped, never truncated silently mid-frame. */
	assert( PS5_AudioPatternFill( total - 10, frames, 64 ) == 10 );
}

static void test_hash_is_stable_and_chunk_independent( void )
{
	const uint32_t total = PS5_AudioPatternFrames( );
	const uint64_t expected = PS5_AudioPatternHash( );
	int16_t chunk[1000 * 2];
	uint64_t rolling = PS5_AUDIO_HASH_SEED;
	uint32_t offset = 0;

	/* Same value however the pattern is sliced: the gate stages 1024 at a time. */
	while( offset < total )
	{
		const uint32_t written = PS5_AudioPatternFill( offset, chunk, 1000 );
		assert( written > 0 );
		rolling = PS5_AudioHashFrames( rolling, chunk, (size_t)written * 2 );
		offset += written;
	}
	assert( rolling == expected );
	assert( expected == EXPECTED_SOURCE_HASH );
	/* Recomputing is deterministic. */
	assert( PS5_AudioPatternHash( ) == expected );
	/* And the hash actually depends on the data. */
	assert( PS5_AudioHashFrames( PS5_AUDIO_HASH_SEED, chunk, 2 ) != expected );
}

int main( void )
{
	test_layout( );
	test_expected_output_accounting( );
	test_segments_are_audible_and_silent_where_expected( );
	test_offset_addressable( );
	test_bounds( );
	test_hash_is_stable_and_chunk_independent( );
	return 0;
}
