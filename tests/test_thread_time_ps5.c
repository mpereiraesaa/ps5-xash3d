#include "thread_time_ps5.h"

#include <assert.h>
#include <stdio.h>

int main( void )
{
	Ps5ThreadTimeReport report;
	uint32_t index;
	assert( PS5_ThreadTimeRun( &report ) == 0 );
	assert( report.pass == 1 );
	assert( report.create_calls == 2u );
	assert( report.join_calls == 1u );
	assert( report.detach_calls == 1u );
	assert( report.worker_completions == 2u );
	assert( report.detached_completion == 1u );
	assert( report.distinct_workers == 2u );
	assert( report.counter == report.expected_counter );
	assert( report.clock_reads == PS5_THREAD_TIME_CLOCK_SAMPLES );
	assert( report.clock_advances > 0u );
	assert( report.clock_regressions == 0u );
	assert( report.sleep_errors == 0u );
	assert( report.sleep_early == 0u );
	for( index = 0; index < PS5_THREAD_TIME_SLEEP_BUCKETS; index++ )
	{
		assert( report.sleeps[index].samples == PS5_THREAD_TIME_SLEEP_SAMPLES );
		assert( report.sleeps[index].min_ns > 0u );
		assert( report.sleeps[index].p95_ns >= report.sleeps[index].min_ns );
		assert( report.sleeps[index].max_ns >= report.sleeps[index].p95_ns );
	}
	puts( "PS5 thread/time gate passed" );
	return 0;
}
