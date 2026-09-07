/*
Host coverage for the PS5 SceAudioOut core: ABI and constants, acquisition
rollback, ring wrap-around, producer/consumer exclusion, the exact 147/160
resampler and its continuity, whole-block submission, terminal-only zero-fill,
prime without underrun, one underrun per episode, pause/resume, a propagated
Output error, and exact drain/close/join with an idempotent shutdown.
*/

#include "../include/ps5_platform.h"
#include "../xash/platform_ps5/audio_ps5.h"

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RING_FRAMES 4096u
#define GRAIN PS5_AUDIO_GRAIN

static int16_t ring[RING_FRAMES * 2];

static int init_result;
static int open_result;
static int volume_result;
static int output_result;
static int close_result;
static int init_calls;
static int open_calls;
static int volume_calls;
static int output_calls;
static int drain_calls;
static int close_calls;
static int32_t observed_volumes[PS5_AUDIO_VOLUME_CHANNELS];
static int observed_volume_flags;
static int16_t captured[4096 * 2];
static size_t captured_frames;
static int fail_output_after;

static void reset_fixture( void )
{
	memset( ring, 0, sizeof( ring ));
	init_result = 0;
	open_result = 11;
	volume_result = 0;
	output_result = 0;
	close_result = 0;
	init_calls = 0;
	open_calls = 0;
	volume_calls = 0;
	output_calls = 0;
	drain_calls = 0;
	close_calls = 0;
	captured_frames = 0;
	fail_output_after = -1;
	observed_volume_flags = -1;
	memset( observed_volumes, 0, sizeof( observed_volumes ));
}

int ps5log_printf( const char *level, const char *format, ... )
{
	(void)level;
	(void)format;
	return 0;
}

int sceAudioOutInit( void )
{
	init_calls++;
	return init_result;
}

int sceAudioOutOpen( int32_t user_id, int32_t type, int32_t index,
	uint32_t grain, uint32_t frequency, uint32_t format )
{
	open_calls++;
	/* The port contract this gate implements, asserted on every open. */
	assert( type == PS5_AUDIO_PORT_TYPE_MAIN );
	assert( index == PS5_AUDIO_PORT_INDEX );
	assert( grain == GRAIN );
	assert( grain % 256u == 0u && grain >= 256u && grain <= 2048u );
	assert( frequency == PS5_AUDIO_OUTPUT_RATE );
	assert( frequency == 48000u || frequency == 192000u );
	assert( format == PS5_AUDIO_FORMAT_S16_STEREO );
	assert( user_id == PS5_AUDIO_USER_SYSTEM || user_id >= 0 );
	return open_result;
}

int sceAudioOutSetVolume( int32_t handle, int32_t flags, const int32_t *volumes )
{
	volume_calls++;
	assert( handle == open_result );
	observed_volume_flags = flags;
	memcpy( observed_volumes, volumes, sizeof( observed_volumes ));
	return volume_result;
}

int sceAudioOutOutput( int32_t handle, const void *samples )
{
	assert( handle == open_result );
	if( samples == NULL )
	{
		drain_calls++;
		return output_result;
	}
	output_calls++;
	if( fail_output_after >= 0 && output_calls > fail_output_after )
		return -7;
	if( captured_frames + GRAIN <= sizeof( captured ) / sizeof( captured[0] ) / 2 )
	{
		memcpy( &captured[captured_frames * 2], samples, GRAIN * 2 * sizeof( int16_t ));
		captured_frames += GRAIN;
	}
	return output_result;
}

int sceAudioOutClose( int32_t handle )
{
	close_calls++;
	assert( handle == open_result );
	return close_result;
}

static struct ps5_audio_config base_config( uint32_t prime )
{
	struct ps5_audio_config config;
	memset( &config, 0, sizeof( config ));
	config.user_id = PS5_AUDIO_USER_SYSTEM;
	config.type = PS5_AUDIO_PORT_TYPE_MAIN;
	config.index = PS5_AUDIO_PORT_INDEX;
	config.grain = GRAIN;
	config.input_rate = PS5_AUDIO_INPUT_RATE;
	config.output_rate = PS5_AUDIO_OUTPUT_RATE;
	config.format = PS5_AUDIO_FORMAT_S16_STEREO;
	config.prime_frames = prime;
	return config;
}

static struct ps5_audio_ring base_ring( void )
{
	struct ps5_audio_ring value;
	value.frames = ring;
	value.capacity = RING_FRAMES;
	return value;
}

/* Writes `count` frames at the absolute producer cursor, splitting on the wrap. */
static void write_ring( uint64_t produced, const int16_t *frames, uint32_t count )
{
	uint32_t index;
	for( index = 0; index < count; index++ )
	{
		const uint32_t slot = (uint32_t)(( produced + index ) & ( RING_FRAMES - 1 ));
		ring[slot * 2] = frames[index * 2];
		ring[slot * 2 + 1] = frames[index * 2 + 1];
	}
}

static void fill_ramp( int16_t *frames, uint32_t count, int16_t start )
{
	uint32_t index;
	for( index = 0; index < count; index++ )
	{
		frames[index * 2] = (int16_t)( start + (int16_t)index );
		frames[index * 2 + 1] = (int16_t)-( start + (int16_t)index );
	}
}

static void test_constants( void )
{
	assert( PS5_AUDIO_INPUT_RATE == 44100u );
	assert( PS5_AUDIO_OUTPUT_RATE == 48000u );
	assert( PS5_AUDIO_RATIO_NUM == 147u );
	assert( PS5_AUDIO_RATIO_DEN == 160u );
	/* 44100/48000 must reduce to exactly 147/160. */
	assert( PS5_AUDIO_INPUT_RATE * PS5_AUDIO_RATIO_DEN ==
		PS5_AUDIO_OUTPUT_RATE * PS5_AUDIO_RATIO_NUM );
	assert( PS5_AUDIO_GRAIN == 256u );
	assert( PS5_AUDIO_BLOCK_SHORTS == 512u );
	assert( PS5_AUDIO_CHANNELS == 2u );
	assert( PS5_AUDIO_PORT_TYPE_MAIN == 0 );
	assert( PS5_AUDIO_PORT_INDEX == 0 );
	assert( PS5_AUDIO_USER_SYSTEM == 0xff );
	assert( PS5_AUDIO_FORMAT_S16_STEREO == 1u );
	assert( PS5_AUDIO_VOLUME_FLAGS == 3 );
	assert( PS5_AUDIO_VOLUME_0DB == 0x8000 );
	assert( PS5_AUDIO_VOLUME_CHANNELS == 8 );
}

static void test_open_and_volume( void )
{
	const struct ps5_audio_config config = base_config( 512 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;
	int index;

	reset_fixture( );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	stats = PS5_AudioStats( );
	assert( init_calls == 1 && open_calls == 1 && volume_calls == 1 );
	assert( stats->handle == open_result );
	assert( stats->state == PS5_AUDIO_STATE_PRIMING );
	assert( observed_volume_flags == PS5_AUDIO_VOLUME_FLAGS );
	for( index = 0; index < PS5_AUDIO_VOLUME_CHANNELS; index++ )
		assert( observed_volumes[index] == PS5_AUDIO_VOLUME_0DB );
	assert( PS5_AudioShutdown( ) == 0 );
}

static void test_rejects_bad_configuration( void )
{
	struct ps5_audio_config config = base_config( 512 );
	struct ps5_audio_ring value = base_ring( );

	reset_fixture( );
	assert( PS5_AudioInit( NULL, &value ) == -1 );
	assert( PS5_AudioInit( &config, NULL ) == -1 );
	value.capacity = 3000; /* not a power of two */
	assert( PS5_AudioInit( &config, &value ) == -1 );
	value = base_ring( );
	value.frames = NULL;
	assert( PS5_AudioInit( &config, &value ) == -1 );
	value = base_ring( );
	config.grain = 0;
	assert( PS5_AudioInit( &config, &value ) == -1 );
	config = base_config( RING_FRAMES );
	assert( PS5_AudioInit( &config, &value ) == -1 ); /* prime >= capacity */
	config = base_config( 512 );
	config.input_rate = 0;
	assert( PS5_AudioInit( &config, &value ) == -1 );
	/* A rate pair whose output cannot fit the accumulator is refused up front. */
	config = base_config( 512 );
	config.input_rate = 8000;
	config.output_rate = 48000;
	assert( PS5_AudioInit( &config, &value ) == -1 );
	assert( open_calls == 0 );
}

static void test_acquisition_rollback( void )
{
	const struct ps5_audio_config config = base_config( 512 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;

	/* Init fails: nothing opened, nothing closed. */
	reset_fixture( );
	init_result = -3;
	assert( PS5_AudioInit( &config, &value ) == -3 );
	assert( open_calls == 0 && close_calls == 0 );
	assert( PS5_AudioStats( )->state == PS5_AUDIO_STATE_FAILED );
	/* A second Init call must not be blocked by the failed one. */
	reset_fixture( );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	assert( PS5_AudioShutdown( ) == 0 );

	/* Open fails: no volume call, no close of an unopened handle. */
	reset_fixture( );
	open_result = -5;
	assert( PS5_AudioInit( &config, &value ) == -5 );
	assert( volume_calls == 0 && close_calls == 0 );

	/* Volume fails: the handle is closed exactly once, on this thread. */
	reset_fixture( );
	volume_result = -9;
	assert( PS5_AudioInit( &config, &value ) == -9 );
	stats = PS5_AudioStats( );
	assert( close_calls == 1 && stats->close_calls == 1 );
	assert( stats->state == PS5_AUDIO_STATE_FAILED );
	assert( stats->handle == -1 );
	/* Shutdown after a rolled-back init is a no-op, never a double close. */
	assert( PS5_AudioShutdown( ) == 0 );
	assert( close_calls == 1 );
}

static void test_prime_and_blocks( void )
{
	const struct ps5_audio_config config = base_config( 1024 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;
	int16_t chunk[1024 * 2];

	reset_fixture( );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	stats = PS5_AudioStats( );

	/* Below the prime level the worker submits nothing and counts no underrun. */
	fill_ramp( chunk, 512, 100 );
	write_ring( 0, chunk, 512 );
	PS5_AudioPublish( 512 );
	assert( PS5_AudioWorkerStep( ) == 0 );
	assert( output_calls == 0 );
	assert( stats->underruns == 0 );
	assert( stats->state == PS5_AUDIO_STATE_PRIMING );
	assert( stats->frames_consumed == 0 );

	/* At the prime level it starts playing and only submits whole blocks. */
	fill_ramp( chunk, 512, 700 );
	write_ring( 512, chunk, 512 );
	PS5_AudioPublish( 1024 );
	assert( PS5_AudioWorkerStep( ) > 0 );
	assert( stats->state == PS5_AUDIO_STATE_PLAYING );
	assert( stats->underruns == 0 );
	assert( stats->frames_consumed == 1024 );
	assert( stats->blocks_sent == (uint64_t)output_calls );
	assert( stats->frames_sent == stats->blocks_sent * GRAIN );
	assert( stats->padding_frames == 0 );
	/* 1023 source frames after priming -> ceil(1023*160/147) = 1114 output frames. */
	assert( stats->blocks_sent == 1114 / GRAIN );
	assert( PS5_AudioShutdown( ) == 0 );
}

static void test_underrun_once_per_episode( void )
{
	const struct ps5_audio_config config = base_config( 256 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;
	int16_t chunk[512 * 2];

	reset_fixture( );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	stats = PS5_AudioStats( );
	fill_ramp( chunk, 512, 1 );
	write_ring( 0, chunk, 512 );
	PS5_AudioPublish( 512 );
	assert( PS5_AudioWorkerStep( ) >= 0 );
	assert( stats->underruns == 0 );

	/* Starved: one episode however many times the worker spins. */
	assert( PS5_AudioWorkerStep( ) == 0 );
	assert( PS5_AudioWorkerStep( ) == 0 );
	assert( PS5_AudioWorkerStep( ) == 0 );
	assert( stats->underruns == 1 );

	/* Fed again, then starved again: a second, separate episode. */
	fill_ramp( chunk, 512, 900 );
	write_ring( 512, chunk, 512 );
	PS5_AudioPublish( 1024 );
	assert( PS5_AudioWorkerStep( ) >= 0 );
	assert( stats->underruns == 1 );
	assert( PS5_AudioWorkerStep( ) == 0 );
	assert( PS5_AudioWorkerStep( ) == 0 );
	assert( stats->underruns == 2 );
	/* An underrun never fabricates a silent block; zero-fill is terminal only. */
	assert( stats->padding_frames == 0 );
	assert( PS5_AudioShutdown( ) == 0 );
}

static void test_ring_wraparound( void )
{
	const struct ps5_audio_config config = base_config( 256 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;
	int16_t chunk[1024 * 2];
	uint64_t produced = 0;
	int rounds;

	reset_fixture( );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	stats = PS5_AudioStats( );
	/* Six passes over a 4096-frame ring in 1024-frame chunks. */
	for( rounds = 0; rounds < 24; rounds++ )
	{
		fill_ramp( chunk, 1024, (int16_t)( rounds * 37 ));
		write_ring( produced, chunk, 1024 );
		produced += 1024;
		PS5_AudioPublish( produced );
		assert( PS5_AudioWorkerStep( ) >= 0 );
		/* The consumer keeps up, so the producer never overwrites unread data. */
		assert( stats->frames_consumed == produced );
	}
	assert( stats->ring_wraps > 0 );
	assert( stats->ring_high_water <= RING_FRAMES );
	assert( stats->underruns == 0 );
	assert( PS5_AudioShutdown( ) == 0 );
}

/* The sink is the exclusion point: it only ever runs while the lock is held. */
static int sink_calls;
static uint64_t sink_frames;
static int sink_samplepos;

static void count_consumed( void *opaque, uint32_t frames )
{
	assert( opaque == &sink_calls );
	sink_calls++;
	sink_frames += frames;
	sink_samplepos = PS5_AudioAdvanceSamplePos( sink_samplepos,
		(int)( RING_FRAMES * 2 ), frames );
}

static void test_consumer_notifies_producer( void )
{
	const struct ps5_audio_config config = base_config( 256 );
	const struct ps5_audio_ring value = base_ring( );
	struct ps5_audio_sink sink;
	int16_t chunk[1024 * 2];

	reset_fixture( );
	sink_calls = 0;
	sink_frames = 0;
	sink_samplepos = 0;
	sink.consumed = count_consumed;
	sink.opaque = &sink_calls;
	PS5_AudioSetSink( &sink );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	fill_ramp( chunk, 1024, 5 );
	write_ring( 0, chunk, 1024 );
	PS5_AudioPublish( 1024 );
	assert( PS5_AudioWorkerStep( ) >= 0 );
	assert( sink_calls == 1 );
	assert( sink_frames == 1024 );
	assert( sink_samplepos == 2048 );
	assert( PS5_AudioShutdown( ) == 0 );
	PS5_AudioSetSink( NULL );
}

/* Runs steps until the worker has taken everything published. */
static void drain_steps( const struct ps5_audio_stats *stats, uint64_t produced )
{
	int guard = 0;
	while( stats->frames_consumed < produced )
	{
		assert( PS5_AudioWorkerStep( ) >= 0 );
		assert( ++guard < 64 );
	}
}

static void publish_ramp( const struct ps5_audio_stats *stats, uint64_t *produced,
	uint32_t count, int16_t start )
{
	static int16_t chunk[2048 * 2];
	assert( count <= 2048 );
	fill_ramp( chunk, count, start );
	write_ring( *produced, chunk, count );
	*produced += count;
	PS5_AudioPublish( *produced );
	drain_steps( stats, *produced );
}

static void test_resampler_ratio_and_continuity( void )
{
	const struct ps5_audio_config config = base_config( 0 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;
	uint64_t produced = 0;

	reset_fixture( );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	stats = PS5_AudioStats( );
	/*
	Priming consumes one frame; 147 source frames then yield exactly 160 output
	frames. 8 such cycles are 1176 in and 1280 out, which is exactly 5 blocks of
	256, so nothing is left in the accumulator to blur the ratio.
	*/
	publish_ramp( stats, &produced, 1 + 147 * 8, 0 );
	assert( stats->frames_consumed == 1177 );
	assert( stats->frames_sent == 1280 );
	assert( stats->blocks_sent == 5 );
	assert( stats->padding_frames == 0 );

	/* Continuity: each further cycle adds exactly the same amount, no drift. */
	publish_ramp( stats, &produced, 147 * 8, 50 );
	assert( stats->frames_consumed == 1177 + 1176 );
	assert( stats->frames_sent == 2560 );
	assert( stats->blocks_sent == 10 );
	publish_ramp( stats, &produced, 147 * 8, 90 );
	assert( stats->frames_sent == 3840 );
	assert( stats->blocks_sent == 15 );
	assert( stats->padding_frames == 0 );
	assert( stats->underruns == 0 );
	assert( PS5_AudioShutdown( ) == 0 );
}

static void test_resampler_saturation_and_channels( void )
{
	const struct ps5_audio_config config = base_config( 0 );
	const struct ps5_audio_ring value = base_ring( );
	int16_t chunk[512 * 2];
	size_t frame;

	reset_fixture( );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	/* Full-scale, opposite-signed channels: no wrap-around and no channel swap. */
	for( frame = 0; frame < 512; frame++ )
	{
		chunk[frame * 2] = INT16_MAX;
		chunk[frame * 2 + 1] = INT16_MIN;
	}
	write_ring( 0, chunk, 512 );
	PS5_AudioPublish( 512 );
	assert( PS5_AudioWorkerStep( ) == 2 );
	assert( captured_frames == 2 * GRAIN );
	for( frame = 0; frame < captured_frames; frame++ )
	{
		assert( captured[frame * 2] == INT16_MAX );
		assert( captured[frame * 2 + 1] == INT16_MIN );
	}
	assert( PS5_AudioShutdown( ) == 0 );
}

static void test_terminal_zero_fill( void )
{
	const struct ps5_audio_config config = base_config( 0 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;
	int16_t chunk[300 * 2];
	size_t index;
	uint64_t expected_resampled;

	reset_fixture( );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	stats = PS5_AudioStats( );
	/* 300 source frames -> 326 output frames: one whole block plus a partial tail. */
	fill_ramp( chunk, 300, 1 );
	write_ring( 0, chunk, 300 );
	PS5_AudioPublish( 300 );
	assert( PS5_AudioWorkerStep( ) == 1 );
	assert( stats->padding_frames == 0 );

	assert( PS5_AudioShutdown( ) == 0 );
	expected_resampled = ( 299u * PS5_AUDIO_RATIO_DEN + PS5_AUDIO_RATIO_NUM - 1 )
		/ PS5_AUDIO_RATIO_NUM;
	assert( expected_resampled == 326 );
	assert( stats->blocks_sent == 2 );
	assert( stats->frames_sent == 2 * GRAIN );
	assert( stats->padding_frames == 2 * GRAIN - expected_resampled );
	assert( stats->padding_frames < GRAIN );
	assert( stats->discarded_frames == 0 );
	/* Only the tail is zero: the padded region sits at the end of the last block. */
	assert( captured_frames == 2 * GRAIN );
	for( index = expected_resampled; index < captured_frames; index++ )
	{
		assert( captured[index * 2] == 0 );
		assert( captured[index * 2 + 1] == 0 );
	}
	assert( captured[( expected_resampled - 1 ) * 2] != 0 );
	assert( drain_calls == 1 && close_calls == 1 );
}

static void test_pause_and_resume( void )
{
	const struct ps5_audio_config config = base_config( 0 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;
	int16_t chunk[512 * 2];
	uint64_t sent_before;

	reset_fixture( );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	stats = PS5_AudioStats( );
	fill_ramp( chunk, 512, 3 );
	write_ring( 0, chunk, 512 );
	PS5_AudioPublish( 512 );
	assert( PS5_AudioWorkerStep( ) > 0 );
	sent_before = stats->frames_sent;

	assert( PS5_AudioActivate( 0 ) == 0 );
	assert( stats->paused == 1 );
	fill_ramp( chunk, 512, 400 );
	write_ring( 512, chunk, 512 );
	PS5_AudioPublish( 1024 );
	assert( PS5_AudioWorkerStep( ) == 0 );
	assert( PS5_AudioWorkerStep( ) == 0 );
	assert( stats->frames_sent == sent_before );
	/* A pause is not a starved consumer. */
	assert( stats->underruns == 0 );

	assert( PS5_AudioActivate( 1 ) == 0 );
	assert( stats->paused == 0 );
	assert( PS5_AudioWorkerStep( ) > 0 );
	assert( stats->frames_sent > sent_before );
	assert( stats->frames_consumed == 1024 );
	assert( stats->underruns == 0 );
	assert( PS5_AudioShutdown( ) == 0 );
}

static void test_output_error_is_terminal( void )
{
	const struct ps5_audio_config config = base_config( 0 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;
	int16_t chunk[1024 * 2];

	reset_fixture( );
	fail_output_after = 1;
	assert( PS5_AudioInit( &config, &value ) == 0 );
	stats = PS5_AudioStats( );
	fill_ramp( chunk, 1024, 7 );
	write_ring( 0, chunk, 1024 );
	PS5_AudioPublish( 1024 );
	assert( PS5_AudioWorkerStep( ) == -7 );
	assert( stats->state == PS5_AUDIO_STATE_FAILED );
	assert( stats->output_errors == 1 );
	assert( stats->last_output_result == -7 );
	/* Further steps stay refused rather than resubmitting into a dead port. */
	assert( PS5_AudioWorkerStep( ) == -1 );
	/* The failure reaches the caller, and the handle still closes exactly once. */
	assert( PS5_AudioShutdown( ) == -7 );
	assert( drain_calls == 1 && close_calls == 1 );
	assert( stats->state == PS5_AUDIO_STATE_FAILED );
	/* No terminal padding is invented after a failed port. */
	assert( stats->padding_frames == 0 );
}

static void test_shutdown_is_idempotent( void )
{
	const struct ps5_audio_config config = base_config( 0 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;
	int16_t chunk[256 * 2];

	reset_fixture( );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	stats = PS5_AudioStats( );
	fill_ramp( chunk, 256, 2 );
	write_ring( 0, chunk, 256 );
	PS5_AudioPublish( 256 );
	assert( PS5_AudioWorkerStep( ) >= 0 );
	assert( PS5_AudioShutdown( ) == 0 );
	assert( drain_calls == 1 && close_calls == 1 );
	assert( stats->drain_calls == 1 && stats->close_calls == 1 );
	assert( stats->state == PS5_AUDIO_STATE_STOPPED );

	/* Repeat shutdown: no second drain, close or free. */
	assert( PS5_AudioShutdown( ) == 0 );
	assert( PS5_AudioShutdown( ) == 0 );
	assert( drain_calls == 1 && close_calls == 1 );
	assert( stats->drain_calls == 1 && stats->close_calls == 1 );
}

static void test_worker_thread_drain_close_join( void )
{
	const struct ps5_audio_config config = base_config( 512 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;
	int16_t chunk[512 * 2];
	uint64_t produced = 0;
	int rounds;

	reset_fixture( );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	stats = PS5_AudioStats( );
	assert( PS5_AudioStartWorker( ) == 0 );
	/* A second worker is refused: one owner of the handle. */
	assert( PS5_AudioStartWorker( ) == -1 );

	for( rounds = 0; rounds < 8; rounds++ )
	{
		uint32_t space;
		fill_ramp( chunk, 512, (int16_t)( rounds * 11 + 1 ));
		for( ;; )
		{
			PS5_AudioLock( );
			space = RING_FRAMES - (uint32_t)( produced - stats->frames_consumed );
			PS5_AudioUnlock( );
			if( space >= 512 )
				break;
		}
		write_ring( produced, chunk, 512 );
		produced += 512;
		PS5_AudioLock( );
		PS5_AudioPublish( produced );
		PS5_AudioUnlock( );
	}

	assert( PS5_AudioShutdown( ) == 0 );
	assert( stats->join_calls == 1 );
	assert( stats->drain_calls == 1 && stats->close_calls == 1 );
	assert( drain_calls == 1 && close_calls == 1 );
	assert( stats->frames_consumed == produced );
	assert( stats->discarded_frames == 0 );
	assert( stats->frames_sent == stats->blocks_sent * GRAIN );
	assert( stats->state == PS5_AUDIO_STATE_STOPPED );
	/* Idempotent after a joined worker too. */
	assert( PS5_AudioShutdown( ) == 0 );
	assert( close_calls == 1 );
}

/*
The core's reported source hash must equal an independently computed FNV over
the very frames the producer published. Nothing compared the two before, which
is how a hash left at zero by the init memset reached FW 12.02.
*/
static void test_source_hash_matches_published_pcm( void )
{
	const struct ps5_audio_config config = base_config( 256 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;
	int16_t chunk[512 * 2];
	uint64_t produced = 0, expected = PS5_AUDIO_HASH_SEED;
	int rounds;

	reset_fixture( );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	stats = PS5_AudioStats( );
	/* Seeded before a single frame moves, so an empty run is comparable too. */
	assert( stats->source_hash == PS5_AUDIO_HASH_SEED );
	assert( stats->output_hash == PS5_AUDIO_HASH_SEED );

	for( rounds = 0; rounds < 20; rounds++ )
	{
		fill_ramp( chunk, 512, (int16_t)( rounds * 101 + 7 ));
		expected = PS5_AudioHashFrames( expected, chunk, 512 * 2 );
		write_ring( produced, chunk, 512 );
		produced += 512;
		PS5_AudioPublish( produced );
		drain_steps( stats, produced );
	}
	assert( stats->frames_consumed == produced );
	assert( stats->source_hash == expected );
	/* The post-resampler hash is distinct and actually accumulated. */
	assert( stats->output_hash != PS5_AUDIO_HASH_SEED );
	assert( stats->output_hash != stats->source_hash );
	assert( PS5_AudioShutdown( ) == 0 );
}

/*
A non-negative Output result is success: FW 12.02 returns the number of frames
accepted (256 at this grain) from both Output and the NULL drain, so treating a
positive return as a failure would reject a healthy port.
*/
static void test_positive_output_result_is_success( void )
{
	const struct ps5_audio_config config = base_config( 0 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;
	int16_t chunk[512 * 2];

	reset_fixture( );
	output_result = (int)GRAIN;
	assert( PS5_AudioInit( &config, &value ) == 0 );
	stats = PS5_AudioStats( );
	fill_ramp( chunk, 512, 11 );
	write_ring( 0, chunk, 512 );
	PS5_AudioPublish( 512 );
	assert( PS5_AudioWorkerStep( ) == 2 );
	assert( stats->output_errors == 0 );
	assert( stats->last_output_result == (int)GRAIN );
	assert( PS5_AudioShutdown( ) == 0 );
	assert( stats->drain_result == (int)GRAIN );
	assert( stats->state == PS5_AUDIO_STATE_STOPPED );
	assert( drain_calls == 1 && close_calls == 1 );
}

static void test_dma_cursor_helpers( void )
{
	/* snd.samples is mono samples: N source frames advance the cursor by N*2. */
	assert( PS5_AudioAdvanceSamplePos( 0, 65536, 1024 ) == 2048 );
	assert( PS5_AudioAdvanceSamplePos( 65534, 65536, 1 ) == 0 );
	assert( PS5_AudioAdvanceSamplePos( 65532, 65536, 4 ) == 4 );
	assert( PS5_AudioAdvanceSamplePos( 0, 0, 16 ) == 0 );

	assert( PS5_AudioRingFrames( 0x8000 ) == 0x8000u );
	assert( PS5_AudioRingFrames( 1000 ) == 512u );
	assert( PS5_AudioRingFrames( 1 ) == 1u );
	assert( PS5_AudioRingFrames( 0 ) == 0u );

	assert( !PS5_AudioIsRebase( 100, 100 ));
	assert( !PS5_AudioIsRebase( 101, 100 ));
	assert( PS5_AudioIsRebase( 32768, 0x40000001 ));
}

static void test_rebase_recovers_from_paintedtime_chop( void )
{
	const struct ps5_audio_config config = base_config( 0 );
	const struct ps5_audio_ring value = base_ring( );
	const struct ps5_audio_stats *stats;
	int16_t chunk[512 * 2];

	reset_fixture( );
	assert( PS5_AudioInit( &config, &value ) == 0 );
	stats = PS5_AudioStats( );
	fill_ramp( chunk, 512, 1 );
	write_ring( 0, chunk, 512 );
	PS5_AudioPublish( 512 );
	assert( PS5_AudioWorkerStep( ) > 0 );

	/* Publishing a lower total is ignored, which is why the chop needs a rebase. */
	PS5_AudioPublish( 8 );
	assert( stats->frames_produced == 512 );

	PS5_AudioRebase( 4096 );
	assert( stats->rebases == 1 );
	assert( stats->frames_produced == 4096 );
	fill_ramp( chunk, 512, 300 );
	write_ring( 4096, chunk, 512 );
	PS5_AudioPublish( 4096 + 512 );
	assert( PS5_AudioWorkerStep( ) > 0 );
	assert( stats->frames_consumed == 512 + 512 );
	assert( PS5_AudioShutdown( ) == 0 );
}

int main( void )
{
	test_constants( );
	test_open_and_volume( );
	test_rejects_bad_configuration( );
	test_acquisition_rollback( );
	test_prime_and_blocks( );
	test_underrun_once_per_episode( );
	test_ring_wraparound( );
	test_consumer_notifies_producer( );
	test_resampler_ratio_and_continuity( );
	test_resampler_saturation_and_channels( );
	test_terminal_zero_fill( );
	test_pause_and_resume( );
	test_output_error_is_terminal( );
	test_shutdown_is_idempotent( );
	test_worker_thread_drain_close_join( );
	test_source_hash_matches_published_pcm( );
	test_positive_output_result_is_success( );
	test_dma_cursor_helpers( );
	test_rebase_recovers_from_paintedtime_chop( );
	return 0;
}
