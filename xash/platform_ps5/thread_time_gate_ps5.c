#include "thread_time_ps5.h"
#include "ps5log.h"

int PS5_ThreadTimeGateRun( void )
{
	Ps5ThreadTimeReport report;
	uint32_t index;
	int rc;

	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_THREAD_TIME_BEGIN schema=1 workers=%u iterations=%u "
		"clock_samples=%u sleep_samples=%u sleep_buckets=%u",
		PS5_THREAD_TIME_WORKERS, PS5_THREAD_TIME_ITERATIONS,
		PS5_THREAD_TIME_CLOCK_SAMPLES, PS5_THREAD_TIME_SLEEP_SAMPLES,
		PS5_THREAD_TIME_SLEEP_BUCKETS );
	rc = PS5_ThreadTimeRun( &report );
	(void)ps5log_printf( rc == 0 ? PS5LOG_MARK : PS5LOG_ERR,
		"XASH_THREAD_RESULT schema=1 create_calls=%u create_join_rc=%d "
		"create_detach_rc=%d join_calls=%u join_rc=%d detach_calls=%u "
		"detach_rc=%d completions=%u detached_complete=%u distinct=%u "
		"mutex_init_rc=%d mutex_destroy_rc=%d mutex_errors=%u "
		"counter=%llu expected=%llu ownership=exact pass=%d",
		report.create_calls, report.create_join_rc, report.create_detach_rc,
		report.join_calls, report.join_rc, report.detach_calls, report.detach_rc,
		report.worker_completions, report.detached_completion,
		report.distinct_workers, report.mutex_init_rc, report.mutex_destroy_rc,
		report.mutex_errors, (unsigned long long)report.counter,
		(unsigned long long)report.expected_counter, report.pass );
	(void)ps5log_printf( report.clock_errors == 0u &&
		report.clock_regressions == 0u ? PS5LOG_MARK : PS5LOG_ERR,
		"XASH_CLOCK_RESULT schema=1 clock=monotonic reads=%llu advances=%llu "
		"min_step_ns=%llu span_ns=%llu errors=%u regressions=%u pass=%d",
		(unsigned long long)report.clock_reads,
		(unsigned long long)report.clock_advances,
		(unsigned long long)report.clock_min_step_ns,
		(unsigned long long)report.clock_span_ns, report.clock_errors,
		report.clock_regressions, report.clock_errors == 0u &&
		report.clock_regressions == 0u && report.clock_advances > 0u );
	for( index = 0; index < PS5_THREAD_TIME_SLEEP_BUCKETS; index++ )
	{
		const Ps5SleepResult *sleep = &report.sleeps[index];
		(void)ps5log_printf( sleep->errors == 0u && sleep->early == 0u
			? PS5LOG_MARK : PS5LOG_ERR,
			"XASH_SLEEP_RESULT schema=1 api=%s requested_us=%u samples=%u "
			"min_ns=%llu average_ns=%llu p95_ns=%llu max_ns=%llu "
			"errors=%u early=%u pass=%d",
			sleep->api == PS5_SLEEP_API_NANOSLEEP ? "nanosleep" : "usleep",
			sleep->requested_us, sleep->samples,
			(unsigned long long)sleep->min_ns,
			(unsigned long long)sleep->average_ns,
			(unsigned long long)sleep->p95_ns,
			(unsigned long long)sleep->max_ns, sleep->errors, sleep->early,
			sleep->errors == 0u && sleep->early == 0u );
	}
	(void)ps5log_printf( rc == 0 ? PS5LOG_MARK : PS5LOG_ERR,
		"XASH_THREAD_TIME_COMPLETE schema=1 create=2 join=1 detach=1 "
		"workers=%u counter=%llu clock_regressions=%u sleep_errors=%u "
		"sleep_early=%u ownership=exact pass=%d",
		report.worker_completions, (unsigned long long)report.counter,
		report.clock_regressions, report.sleep_errors, report.sleep_early,
		report.pass );
	return rc;
}
