#include "../src/ps5_gpu_flip_timing.h"

#include <assert.h>

int main(void)
{
    Ps5GpuFlipTimingAccumulator timing;
    Ps5GpuFlipTimingSample sample;
    Ps5GpuFlipTimingSummary summary;

    assert(ps5_gpu_flip_timing_init(&timing, 3u) == 0);
    assert(ps5_gpu_flip_timing_record(
        &timing, 0u, 0u, 10u, 100u, 150u, 180u, 1000u, &sample) == 0);
    assert(sample.gpu_eop_delta_ticks == 0u &&
           sample.submit_to_gpu_ns == 50u &&
           sample.submit_to_flip_ns == 80u && sample.gpu_to_flip_ns == 30u);
    assert(ps5_gpu_flip_timing_record(
        &timing, 1u, 1u, 11u, 200u, 270u, 290u, 1120u, &sample) == 0);
    assert(sample.gpu_eop_delta_ticks == 120u);
    assert(ps5_gpu_flip_timing_record(
        &timing, 2u, 0u, 12u, 300u, 390u, 400u, 1300u, &sample) == 0);
    assert(ps5_gpu_flip_timing_finish(&timing, &summary) == 0);
    assert(summary.frames == 3u && summary.gpu_timestamp_writes == 3u &&
           summary.gpu_timestamp_changes == 2u);
    assert(summary.submit_to_gpu_min_ns == 50u &&
           summary.submit_to_gpu_avg_ns == 70u &&
           summary.submit_to_gpu_max_ns == 90u);
    assert(summary.submit_to_flip_min_ns == 80u &&
           summary.submit_to_flip_avg_ns == 90u &&
           summary.submit_to_flip_max_ns == 100u);
    assert(summary.gpu_to_flip_min_ns == 10u &&
           summary.gpu_to_flip_avg_ns == 20u &&
           summary.gpu_to_flip_max_ns == 30u);

    assert(ps5_gpu_flip_timing_init(&timing, 2u) == 0);
    assert(ps5_gpu_flip_timing_record(
        &timing, 1u, 1u, 10u, 100u, 150u, 180u, 1000u, &sample) ==
        PS5_GPU_FLIP_TIMING_GAP);
    assert(ps5_gpu_flip_timing_record(
        &timing, 0u, 0u, 10u, 100u, 90u, 180u, 1000u, &sample) ==
        PS5_GPU_FLIP_TIMING_CPU_ORDER);
    assert(ps5_gpu_flip_timing_record(
        &timing, 0u, 0u, 10u, 100u, 150u, 180u,
        PS5_GPU_TIMESTAMP_PENDING, &sample) ==
        PS5_GPU_FLIP_TIMING_GPU_TIMESTAMP);
    assert(ps5_gpu_flip_timing_record(
        &timing, 0u, 0u, 10u, 100u, 150u, 180u, 1000u, &sample) == 0);
    assert(ps5_gpu_flip_timing_record(
        &timing, 1u, 1u, 11u, 200u, 250u, 280u, 1000u, &sample) ==
        PS5_GPU_FLIP_TIMING_GPU_TIMESTAMP);
    assert(ps5_gpu_flip_timing_finish(&timing, &summary) ==
           PS5_GPU_FLIP_TIMING_INCOMPLETE);
    return 0;
}
