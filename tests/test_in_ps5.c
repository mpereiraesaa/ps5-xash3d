#include "../include/ps5_platform.h"
#include "../xash/platform_ps5/in_ps5.h"

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BUTTON_L1 UINT32_C(0x00000400)
#define BUTTON_R1 UINT32_C(0x00000800)
#define BUTTON_CIRCLE UINT32_C(0x00002000)
#define BUTTON_CROSS UINT32_C(0x00004000)

struct observed_event {
	int kind;
	int control;
	int value;
};

static struct ps5_pad_data fixture[64];
static int fixture_count;
static int initialize_result;
static int initial_user_result;
static int pad_init_result;
static int pad_open_result;
static int read_result;
static int close_result;
static int terminate_result;
static int initialize_calls;
static int initial_user_calls;
static int pad_init_calls;
static int pad_open_calls;
static int read_calls;
static int close_calls;
static int terminate_calls;
static int read_capacity;
static struct observed_event events[256];
static size_t event_count;

static void reset_fixture( void )
{
	memset( fixture, 0, sizeof( fixture ));
	fixture_count = 0;
	initialize_result = 0;
	initial_user_result = 0;
	pad_init_result = 0;
	pad_open_result = 7;
	read_result = 0;
	close_result = 0;
	terminate_result = 0;
	initialize_calls = 0;
	initial_user_calls = 0;
	pad_init_calls = 0;
	pad_open_calls = 0;
	read_calls = 0;
	close_calls = 0;
	terminate_calls = 0;
	read_capacity = 0;
	event_count = 0;
}

static struct ps5_pad_data neutral_sample( uint64_t timestamp, uint8_t generation )
{
	struct ps5_pad_data sample;
	memset( &sample, 0, sizeof( sample ));
	sample.left_stick.x = 128;
	sample.left_stick.y = 128;
	sample.right_stick.x = 128;
	sample.right_stick.y = 128;
	sample.connected = 1;
	sample.timestamp = timestamp;
	sample.connected_count = generation;
	return sample;
}

int ps5log_printf( const char *level, const char *format, ... )
{
	(void)level;
	(void)format;
	return 0;
}

int sceUserServiceInitialize( const void *params )
{
	(void)params;
	initialize_calls++;
	return initialize_result;
}

int sceUserServiceGetForegroundUser( int32_t *user_id )
{
	initial_user_calls++;
	if( initial_user_result == 0 ) *user_id = 42;
	return initial_user_result;
}

int sceUserServiceTerminate( void )
{
	terminate_calls++;
	return terminate_result;
}

int scePadInit( void )
{
	pad_init_calls++;
	return pad_init_result;
}

int scePadOpen( int32_t user_id, int32_t type, int32_t index, const void *params )
{
	assert( user_id == 42 );
	assert( type == 0 );
	assert( index == 0 );
	assert( params == NULL );
	pad_open_calls++;
	return pad_open_result;
}

int scePadRead( int32_t handle, struct ps5_pad_data *states, int32_t count )
{
	assert( handle == pad_open_result );
	read_calls++;
	read_capacity = count;
	if( read_result < 0 ) return read_result;
	assert( fixture_count <= count );
	memcpy( states, fixture, (size_t)fixture_count * sizeof( fixture[0] ));
	return fixture_count;
}

int scePadClose( int32_t handle )
{
	assert( handle == pad_open_result );
	close_calls++;
	return close_result;
}

static void observe_axis( void *opaque, enum ps5_xash_pad_axis axis, int16_t value )
{
	(void)opaque;
	assert( event_count < sizeof( events ) / sizeof( events[0] ));
	events[event_count++] = (struct observed_event){ 1, axis, value };
}

static void observe_button( void *opaque, enum ps5_xash_pad_button button, int down )
{
	(void)opaque;
	assert( event_count < sizeof( events ) / sizeof( events[0] ));
	events[event_count++] = (struct observed_event){ 2, button, down };
}

static int saw_event( int kind, int control, int value )
{
	size_t index;
	for( index = 0; index < event_count; index++ )
		if( events[index].kind == kind && events[index].control == control &&
			events[index].value == value )
			return 1;
	return 0;
}

static void test_complete_chronological_batch( void )
{
	const struct ps5_xash_pad_sink sink = { observe_axis, observe_button, NULL };
	const struct ps5_xash_pad_stats *stats;
	reset_fixture( );
	PS5_PadInputSetSink( &sink );
	assert( PS5_PadInputInit( ) == 0 );
	assert( initialize_calls == 1 && initial_user_calls == 1 );
	assert( pad_init_calls == 1 && pad_open_calls == 1 );

	fixture[0] = neutral_sample( 100, 1 );
	fixture[1] = neutral_sample( 200, 1 );
	fixture[1].left_stick.x = 255;
	fixture[1].left_stick.y = 0;
	fixture[1].right_stick.x = 255;
	fixture[1].right_stick.y = 0;
	fixture[1].l2 = 17;
	fixture[1].r2 = 231;
	fixture[1].buttons = BUTTON_CROSS | BUTTON_CIRCLE | BUTTON_L1 | BUTTON_R1;
	fixture[2] = fixture[1];
	fixture[2].timestamp = 300;
	fixture[2].buttons |= UINT32_C(0x00000004); /* R3 joins L1: still one crouch. */
	fixture[3] = fixture[2];
	fixture[3].timestamp = 400;
	fixture[3].buttons &= ~BUTTON_L1; /* R3 holds crouch across this edge. */
	fixture[4] = neutral_sample( 500, 1 );
	fixture_count = 5;

	assert( PS5_PadInputPoll( ) == 5 );
	assert( read_calls == 1 && read_capacity == 64 );
	assert( saw_event( 1, PS5_XASH_AXIS_SIDE, 32512 ));
	assert( saw_event( 1, PS5_XASH_AXIS_FORWARD, -32768 ));
	assert( saw_event( 1, PS5_XASH_AXIS_PITCH, -32768 ));
	assert( saw_event( 1, PS5_XASH_AXIS_YAW, 32512 ));
	assert( saw_event( 2, PS5_XASH_BUTTON_A, 1 ));
	assert( saw_event( 2, PS5_XASH_BUTTON_A, 0 ));
	assert( saw_event( 2, PS5_XASH_BUTTON_B, 1 ));
	assert( saw_event( 2, PS5_XASH_BUTTON_L1, 1 ));
	assert( saw_event( 2, PS5_XASH_BUTTON_R1, 1 ));
	assert( PS5_PadInputGatePassed( ));
	stats = PS5_PadInputStats( );
	assert( stats->samples == 5 && stats->max_batch == 5 );
	assert( stats->movement_samples == 3 && stats->look_samples == 3 );
	assert( stats->jump_presses == 1 && stats->jump_releases == 1 );
	assert( stats->crouch_presses == 1 && stats->crouch_releases == 1 );
	assert( stats->use_presses == 1 && stats->use_releases == 1 );
	assert( stats->fire_presses == 1 && stats->fire_releases == 1 );
	assert( PS5_PadInputShutdown( ) == 0 );
	assert( close_calls == 1 && terminate_calls == 1 );
}

static void test_generation_change_and_interception_release_state( void )
{
	const struct ps5_xash_pad_sink sink = { observe_axis, observe_button, NULL };
	const struct ps5_xash_pad_stats *stats;
	reset_fixture( );
	PS5_PadInputSetSink( &sink );
	assert( PS5_PadInputInit( ) == 0 );
	fixture[0] = neutral_sample( 100, 1 );
	fixture[0].buttons = BUTTON_CROSS;
	fixture[1] = neutral_sample( 200, 2 );
	fixture[1].buttons = UINT32_C(0x80000000) | BUTTON_CROSS;
	fixture_count = 2;
	assert( PS5_PadInputPoll( ) == 2 );
	stats = PS5_PadInputStats( );
	assert( stats->generation_changes == 2 );
	assert( stats->intercepted_samples == 1 );
	assert( stats->jump_presses == 1 && stats->jump_releases == 1 );
	assert( saw_event( 2, PS5_XASH_BUTTON_A, 0 ));
	assert( PS5_PadInputShutdown( ) == 0 );
}

static void test_non_owner_and_read_failure( void )
{
	const struct ps5_xash_pad_stats *stats;
	reset_fixture( );
	PS5_PadInputSetSink( NULL );
	initialize_result = -17; /* Already initialized by another component. */
	assert( PS5_PadInputInit( ) == 0 );
	read_result = -22;
	assert( PS5_PadInputPoll( ) == -22 );
	stats = PS5_PadInputStats( );
	assert( stats->read_errors == 1 );
	assert( !PS5_PadInputGatePassed( ));
	assert( PS5_PadInputShutdown( ) == 0 );
	assert( close_calls == 1 );
	assert( terminate_calls == 0 );
}

static void test_open_failure_releases_owned_user_service( void )
{
	reset_fixture( );
	pad_open_result = -33;
	assert( PS5_PadInputInit( ) == -33 );
	assert( close_calls == 0 );
	assert( terminate_calls == 1 );
}

static void test_runtime_input_without_gate_autoquit( void )
{
	reset_fixture( );
	PS5_PadInputSetSink( NULL );
	fixture[0] = neutral_sample( 100, 1 );
	fixture_count = 1;
	assert( PS5_PadInputRuntimePoll( ) == 1 );
	assert( PS5_PadInputRuntimePoll( ) == 1 );
	assert( pad_open_calls == 1 && read_calls == 2 );
	assert( !PS5_PadInputGatePassed( ));
	assert( PS5_PadInputRuntimeShutdown( ) == 0 );
	assert( close_calls == 1 && terminate_calls == 1 );
	assert( PS5_PadInputRuntimeShutdown( ) == 0 );
	assert( close_calls == 1 );
}

int main( void )
{
	test_complete_chronological_batch( );
	test_generation_change_and_interception_release_state( );
	test_non_owner_and_read_failure( );
	test_open_failure_releases_owned_user_service( );
	test_runtime_input_without_gate_autoquit( );
	return 0;
}
