#include "ps5_gpu_flip_timing.h"

#include <limits.h>
#include <string.h>

static int add_u64(uint64_t *total, uint64_t value)
{
    if (!total || *total > UINT64_MAX - value)
        return -1;
    *total += value;
    return 0;
}

static void record_range(uint64_t value, uint64_t *minimum,
                         uint64_t *maximum)
{
    if (value < *minimum)
        *minimum = value;
    if (value > *maximum)
        *maximum = value;
}

int ps5_gpu_flip_timing_init(Ps5GpuFlipTimingAccumulator *timing,
                             uint64_t expected_frames)
{
    if (!timing || expected_frames == 0u)
        return PS5_GPU_FLIP_TIMING_INVALID;
    memset(timing, 0, sizeof(*timing));
    timing->expected_frames = expected_frames;
    timing->submit_to_gpu_min_ns = UINT64_MAX;
    timing->submit_to_flip_min_ns = UINT64_MAX;
    timing->gpu_to_flip_min_ns = UINT64_MAX;
    return PS5_GPU_FLIP_TIMING_OK;
}

int ps5_gpu_flip_timing_record(Ps5GpuFlipTimingAccumulator *timing,
                               uint64_t frame, uint32_t slot,
                               uint64_t token, uint64_t cpu_submit_ns,
                               uint64_t cpu_gpu_observed_ns,
                               uint64_t cpu_flip_observed_ns,
                               uint64_t gpu_eop_ticks,
                               Ps5GpuFlipTimingSample *sample)
{
    if (!timing || !sample || timing->expected_frames == 0u || token == 0u ||
        slot >= 2u)
        return PS5_GPU_FLIP_TIMING_INVALID;
    if (frame != timing->frames || slot != (uint32_t)(frame & 1u) ||
        timing->frames >= timing->expected_frames)
        return PS5_GPU_FLIP_TIMING_GAP;
    if (cpu_submit_ns == 0u || cpu_submit_ns > cpu_gpu_observed_ns ||
        cpu_gpu_observed_ns > cpu_flip_observed_ns)
        return PS5_GPU_FLIP_TIMING_CPU_ORDER;
    if (gpu_eop_ticks == PS5_GPU_TIMESTAMP_PENDING ||
        (timing->frames != 0u && gpu_eop_ticks <= timing->last_gpu_eop_ticks))
        return PS5_GPU_FLIP_TIMING_GPU_TIMESTAMP;

    const uint64_t submit_to_gpu = cpu_gpu_observed_ns - cpu_submit_ns;
    const uint64_t submit_to_flip = cpu_flip_observed_ns - cpu_submit_ns;
    const uint64_t gpu_to_flip = cpu_flip_observed_ns - cpu_gpu_observed_ns;
    if (add_u64(&timing->submit_to_gpu_total_ns, submit_to_gpu) != 0 ||
        add_u64(&timing->submit_to_flip_total_ns, submit_to_flip) != 0 ||
        add_u64(&timing->gpu_to_flip_total_ns, gpu_to_flip) != 0)
        return PS5_GPU_FLIP_TIMING_OVERFLOW;

    memset(sample, 0, sizeof(*sample));
    sample->frame = frame;
    sample->token = token;
    sample->slot = slot;
    sample->cpu_submit_ns = cpu_submit_ns;
    sample->cpu_gpu_observed_ns = cpu_gpu_observed_ns;
    sample->cpu_flip_observed_ns = cpu_flip_observed_ns;
    sample->gpu_eop_ticks = gpu_eop_ticks;
    sample->gpu_eop_delta_ticks = timing->frames == 0u
        ? 0u : gpu_eop_ticks - timing->last_gpu_eop_ticks;
    sample->submit_to_gpu_ns = submit_to_gpu;
    sample->submit_to_flip_ns = submit_to_flip;
    sample->gpu_to_flip_ns = gpu_to_flip;

    if (timing->frames == 0u)
        timing->first_gpu_eop_ticks = gpu_eop_ticks;
    else
        ++timing->gpu_timestamp_changes;
    timing->last_gpu_eop_ticks = gpu_eop_ticks;
    record_range(submit_to_gpu, &timing->submit_to_gpu_min_ns,
                 &timing->submit_to_gpu_max_ns);
    record_range(submit_to_flip, &timing->submit_to_flip_min_ns,
                 &timing->submit_to_flip_max_ns);
    record_range(gpu_to_flip, &timing->gpu_to_flip_min_ns,
                 &timing->gpu_to_flip_max_ns);
    ++timing->frames;
    return PS5_GPU_FLIP_TIMING_OK;
}

int ps5_gpu_flip_timing_finish(const Ps5GpuFlipTimingAccumulator *timing,
                               Ps5GpuFlipTimingSummary *summary)
{
    if (!timing || !summary || timing->expected_frames == 0u)
        return PS5_GPU_FLIP_TIMING_INVALID;
    if (timing->frames != timing->expected_frames || timing->frames == 0u ||
        timing->gpu_timestamp_changes + 1u != timing->frames)
        return PS5_GPU_FLIP_TIMING_INCOMPLETE;
    memset(summary, 0, sizeof(*summary));
    summary->frames = timing->frames;
    summary->gpu_timestamp_writes = timing->frames;
    summary->gpu_timestamp_changes = timing->gpu_timestamp_changes;
    summary->first_gpu_eop_ticks = timing->first_gpu_eop_ticks;
    summary->last_gpu_eop_ticks = timing->last_gpu_eop_ticks;
    summary->submit_to_gpu_min_ns = timing->submit_to_gpu_min_ns;
    summary->submit_to_gpu_avg_ns =
        timing->submit_to_gpu_total_ns / timing->frames;
    summary->submit_to_gpu_max_ns = timing->submit_to_gpu_max_ns;
    summary->submit_to_flip_min_ns = timing->submit_to_flip_min_ns;
    summary->submit_to_flip_avg_ns =
        timing->submit_to_flip_total_ns / timing->frames;
    summary->submit_to_flip_max_ns = timing->submit_to_flip_max_ns;
    summary->gpu_to_flip_min_ns = timing->gpu_to_flip_min_ns;
    summary->gpu_to_flip_avg_ns =
        timing->gpu_to_flip_total_ns / timing->frames;
    summary->gpu_to_flip_max_ns = timing->gpu_to_flip_max_ns;
    return PS5_GPU_FLIP_TIMING_OK;
}
