#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 200809L

#include "thread_time_ps5.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* POSIX.1-2008 hides the obsolete declaration, but the focused gate executes
 * the still-exported function to classify the engine's historical surface. */
extern int usleep( unsigned int usec );

#define NS_PER_SECOND 1000000000ull
#define NS_PER_US 1000ull
#define DETACHED_WAIT_NS (5ull * NS_PER_SECOND)
#define MAX_SLEEP_NS (500ull * 1000ull * 1000ull)
#define EARLY_TOLERANCE_NS (50ull * NS_PER_US)

typedef struct ThreadState
{
	pthread_mutex_t mutex;
	pthread_t parent;
	atomic_uint completions;
	atomic_uint distinct;
	atomic_uint mutex_errors;
	atomic_uint clock_errors;
	atomic_uint clock_regressions;
	uint64_t counter;
} ThreadState;

static int monotonic_ns( uint64_t *value )
{
	struct timespec ts;
	int rc = clock_gettime( CLOCK_MONOTONIC, &ts );
	if( rc != 0 )
		return rc;
	*value = (uint64_t)ts.tv_sec * NS_PER_SECOND + (uint64_t)ts.tv_nsec;
	return 0;
}

static void *worker_main( void *opaque )
{
	ThreadState *state = (ThreadState *)opaque;
	uint64_t previous = 0;
	uint32_t index;

	if( !pthread_equal( pthread_self( ), state->parent ))
		atomic_fetch_add_explicit( &state->distinct, 1u, memory_order_relaxed );

	for( index = 0; index < PS5_THREAD_TIME_ITERATIONS; index++ )
	{
		uint64_t now;
		if( pthread_mutex_lock( &state->mutex ) != 0 )
		{
			atomic_fetch_add_explicit( &state->mutex_errors, 1u, memory_order_relaxed );
			break;
		}
		state->counter++;
		if( pthread_mutex_unlock( &state->mutex ) != 0 )
		{
			atomic_fetch_add_explicit( &state->mutex_errors, 1u, memory_order_relaxed );
			break;
		}
		if(( index & 7u ) != 0u )
			continue;
		if( monotonic_ns( &now ) != 0 )
		{
			atomic_fetch_add_explicit( &state->clock_errors, 1u, memory_order_relaxed );
			continue;
		}
		if( previous != 0 && now < previous )
			atomic_fetch_add_explicit( &state->clock_regressions, 1u, memory_order_relaxed );
		previous = now;
	}
	atomic_fetch_add_explicit( &state->completions, 1u, memory_order_release );
	return NULL;
}

static int sleep_once( Ps5SleepApi api, uint32_t requested_us )
{
	if( api == PS5_SLEEP_API_USLEEP )
		return usleep( requested_us );
	else
	{
		struct timespec request;
		struct timespec remaining;
		request.tv_sec = requested_us / 1000000u;
		request.tv_nsec = (long)( requested_us % 1000000u ) * 1000l;
		while( nanosleep( &request, &remaining ) != 0 )
		{
			if( errno != EINTR )
				return -1;
			request = remaining;
		}
	}
	return 0;
}

static void sort_samples( uint64_t *samples, uint32_t count )
{
	uint32_t i;
	for( i = 1; i < count; i++ )
	{
		uint64_t value = samples[i];
		uint32_t j = i;
		while( j > 0 && samples[j - 1] > value )
		{
			samples[j] = samples[j - 1];
			j--;
		}
		samples[j] = value;
	}
}

static void measure_sleep( Ps5SleepResult *result, Ps5SleepApi api,
	uint32_t requested_us )
{
	uint64_t samples[PS5_THREAD_TIME_SLEEP_SAMPLES];
	uint64_t sum = 0;
	uint32_t index;
	memset( result, 0, sizeof( *result ));
	result->api = api;
	result->requested_us = requested_us;
	result->samples = PS5_THREAD_TIME_SLEEP_SAMPLES;
	result->min_ns = UINT64_MAX;

	for( index = 0; index < PS5_THREAD_TIME_SLEEP_SAMPLES; index++ )
	{
		uint64_t before = 0, after = 0, elapsed = 0;
		if( monotonic_ns( &before ) != 0 || sleep_once( api, requested_us ) != 0 ||
			monotonic_ns( &after ) != 0 || after < before )
		{
			result->errors++;
		}
		else
		{
			elapsed = after - before;
			if( elapsed + EARLY_TOLERANCE_NS < (uint64_t)requested_us * NS_PER_US )
				result->early++;
		}
		samples[index] = elapsed;
		if( elapsed < result->min_ns ) result->min_ns = elapsed;
		if( elapsed > result->max_ns ) result->max_ns = elapsed;
		sum += elapsed;
	}
	if( result->min_ns == UINT64_MAX ) result->min_ns = 0;
	result->average_ns = sum / PS5_THREAD_TIME_SLEEP_SAMPLES;
	sort_samples( samples, PS5_THREAD_TIME_SLEEP_SAMPLES );
	result->p95_ns = samples[( PS5_THREAD_TIME_SLEEP_SAMPLES * 95u + 99u ) / 100u - 1u];
}

static void measure_clock( Ps5ThreadTimeReport *report )
{
	uint64_t first = 0, previous = 0;
	uint32_t index;
	report->clock_min_step_ns = UINT64_MAX;
	for( index = 0; index < PS5_THREAD_TIME_CLOCK_SAMPLES; index++ )
	{
		uint64_t now;
		report->clock_reads++;
		if( monotonic_ns( &now ) != 0 )
		{
			report->clock_errors++;
			continue;
		}
		if( first == 0 ) first = now;
		if( previous != 0 )
		{
			if( now < previous ) report->clock_regressions++;
			else if( now > previous )
			{
				uint64_t step = now - previous;
				report->clock_advances++;
				if( step < report->clock_min_step_ns ) report->clock_min_step_ns = step;
			}
		}
		previous = now;
	}
	if( first != 0 && previous >= first ) report->clock_span_ns = previous - first;
	if( report->clock_min_step_ns == UINT64_MAX ) report->clock_min_step_ns = 0;
}

int PS5_ThreadTimeRun( Ps5ThreadTimeReport *report )
{
	static const uint32_t requested_us[] = { 1000u, 2000u, 5000u, 10000u };
	ThreadState state;
	pthread_t joined = {0};
	pthread_t detached = {0};
	uint64_t wait_started = 0, now = 0;
	uint32_t index;
	int detached_must_join = 0;

	if( report == NULL ) return -1;
	memset( report, 0, sizeof( *report ));
	memset( &state, 0, sizeof( state ));
	state.parent = pthread_self( );
	report->expected_counter =
		(uint64_t)PS5_THREAD_TIME_WORKERS * PS5_THREAD_TIME_ITERATIONS;
	report->mutex_init_rc = pthread_mutex_init( &state.mutex, NULL );
	if( report->mutex_init_rc != 0 ) return -1;

	report->create_calls++;
	report->create_join_rc = pthread_create( &joined, NULL, worker_main, &state );
	report->create_calls++;
	report->create_detach_rc = pthread_create( &detached, NULL, worker_main, &state );
	if( report->create_detach_rc == 0 )
	{
		report->detach_calls++;
		report->detach_rc = pthread_detach( detached );
		detached_must_join = report->detach_rc != 0;
	}
	else report->detach_rc = -1;

	if( report->create_join_rc == 0 )
	{
		report->join_calls++;
		report->join_rc = pthread_join( joined, NULL );
	}
	else report->join_rc = -1;
	if( detached_must_join )
	{
		report->join_calls++;
		(void)pthread_join( detached, NULL );
	}

	(void)monotonic_ns( &wait_started );
	while( atomic_load_explicit( &state.completions, memory_order_acquire ) <
		PS5_THREAD_TIME_WORKERS )
	{
		struct timespec pause = { 0, 1000000l };
		(void)nanosleep( &pause, NULL );
		if( monotonic_ns( &now ) != 0 || now - wait_started > DETACHED_WAIT_NS )
			break;
	}
	report->worker_completions = atomic_load_explicit( &state.completions,
		memory_order_acquire );
	report->detached_completion = report->worker_completions == PS5_THREAD_TIME_WORKERS;
	report->distinct_workers = atomic_load_explicit( &state.distinct, memory_order_relaxed );
	report->mutex_errors = atomic_load_explicit( &state.mutex_errors, memory_order_relaxed );
	report->clock_errors = atomic_load_explicit( &state.clock_errors, memory_order_relaxed );
	report->clock_regressions = atomic_load_explicit( &state.clock_regressions,
		memory_order_relaxed );
	report->counter = state.counter;
	report->mutex_destroy_rc = pthread_mutex_destroy( &state.mutex );

	measure_clock( report );
	for( index = 0; index < 4u; index++ )
	{
		measure_sleep( &report->sleeps[index], PS5_SLEEP_API_NANOSLEEP,
			requested_us[index] );
		measure_sleep( &report->sleeps[index + 4u], PS5_SLEEP_API_USLEEP,
			requested_us[index] );
	}
	for( index = 0; index < PS5_THREAD_TIME_SLEEP_BUCKETS; index++ )
	{
		report->sleep_errors += report->sleeps[index].errors;
		report->sleep_early += report->sleeps[index].early;
		if( report->sleeps[index].max_ns > MAX_SLEEP_NS ) report->sleep_errors++;
	}

	report->pass = report->mutex_init_rc == 0 && report->create_join_rc == 0 &&
		report->create_detach_rc == 0 && report->detach_rc == 0 &&
		report->join_rc == 0 && report->mutex_destroy_rc == 0 &&
		report->create_calls == 2u && report->join_calls == 1u &&
		report->detach_calls == 1u && report->worker_completions == 2u &&
		report->detached_completion == 1u && report->distinct_workers == 2u &&
		report->counter == report->expected_counter && report->mutex_errors == 0u &&
		report->clock_errors == 0u && report->clock_regressions == 0u &&
		report->clock_advances > 0u && report->clock_span_ns > 0u &&
		report->sleep_errors == 0u && report->sleep_early == 0u;
	return report->pass ? 0 : -1;
}
