#ifndef XASH_THREAD_TIME_PS5_H
#define XASH_THREAD_TIME_PS5_H

#include <stdint.h>

#define PS5_THREAD_TIME_WORKERS 2u
#define PS5_THREAD_TIME_ITERATIONS 16384u
#define PS5_THREAD_TIME_CLOCK_SAMPLES 8192u
#define PS5_THREAD_TIME_SLEEP_SAMPLES 16u
#define PS5_THREAD_TIME_SLEEP_BUCKETS 8u

typedef enum Ps5SleepApi
{
	PS5_SLEEP_API_NANOSLEEP = 0,
	PS5_SLEEP_API_USLEEP = 1,
} Ps5SleepApi;

typedef struct Ps5SleepResult
{
	Ps5SleepApi api;
	uint32_t requested_us;
	uint32_t samples;
	uint32_t errors;
	uint32_t early;
	uint64_t min_ns;
	uint64_t average_ns;
	uint64_t p95_ns;
	uint64_t max_ns;
} Ps5SleepResult;

typedef struct Ps5ThreadTimeReport
{
	int mutex_init_rc;
	int create_join_rc;
	int create_detach_rc;
	int detach_rc;
	int join_rc;
	int mutex_destroy_rc;
	uint32_t create_calls;
	uint32_t join_calls;
	uint32_t detach_calls;
	uint32_t worker_completions;
	uint32_t detached_completion;
	uint32_t distinct_workers;
	uint32_t mutex_errors;
	uint64_t counter;
	uint64_t expected_counter;
	uint64_t clock_reads;
	uint64_t clock_advances;
	uint64_t clock_min_step_ns;
	uint64_t clock_span_ns;
	uint32_t clock_errors;
	uint32_t clock_regressions;
	uint32_t sleep_errors;
	uint32_t sleep_early;
	Ps5SleepResult sleeps[PS5_THREAD_TIME_SLEEP_BUCKETS];
	int pass;
} Ps5ThreadTimeReport;

int PS5_ThreadTimeRun( Ps5ThreadTimeReport *report );

#endif
