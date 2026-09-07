#ifndef PS5_XASH3D_GPU_FLIP_TIMING_H
#define PS5_XASH3D_GPU_FLIP_TIMING_H

#include <stdint.h>

#define PS5_GPU_TIMESTAMP_PENDING UINT64_MAX

typedef struct Ps5GpuFlipTimingSample {
    uint64_t frame;
    uint64_t token;
    uint32_t slot;
    uint64_t cpu_submit_ns;
    uint64_t cpu_gpu_observed_ns;
    uint64_t cpu_flip_observed_ns;
    uint64_t gpu_eop_ticks;
    uint64_t gpu_eop_delta_ticks;
    uint64_t submit_to_gpu_ns;
    uint64_t submit_to_flip_ns;
    uint64_t gpu_to_flip_ns;
} Ps5GpuFlipTimingSample;

typedef struct Ps5GpuFlipTimingAccumulator {
    uint64_t expected_frames;
    uint64_t frames;
    uint64_t first_gpu_eop_ticks;
    uint64_t last_gpu_eop_ticks;
    uint64_t gpu_timestamp_changes;
    uint64_t submit_to_gpu_total_ns;
    uint64_t submit_to_gpu_min_ns;
    uint64_t submit_to_gpu_max_ns;
    uint64_t submit_to_flip_total_ns;
    uint64_t submit_to_flip_min_ns;
    uint64_t submit_to_flip_max_ns;
    uint64_t gpu_to_flip_total_ns;
    uint64_t gpu_to_flip_min_ns;
    uint64_t gpu_to_flip_max_ns;
} Ps5GpuFlipTimingAccumulator;

typedef struct Ps5GpuFlipTimingSummary {
    uint64_t frames;
    uint64_t gpu_timestamp_writes;
    uint64_t gpu_timestamp_changes;
    uint64_t first_gpu_eop_ticks;
    uint64_t last_gpu_eop_ticks;
    uint64_t submit_to_gpu_min_ns;
    uint64_t submit_to_gpu_avg_ns;
    uint64_t submit_to_gpu_max_ns;
    uint64_t submit_to_flip_min_ns;
    uint64_t submit_to_flip_avg_ns;
    uint64_t submit_to_flip_max_ns;
    uint64_t gpu_to_flip_min_ns;
    uint64_t gpu_to_flip_avg_ns;
    uint64_t gpu_to_flip_max_ns;
} Ps5GpuFlipTimingSummary;

enum Ps5GpuFlipTimingResult {
    PS5_GPU_FLIP_TIMING_OK = 0,
    PS5_GPU_FLIP_TIMING_INVALID = -1,
    PS5_GPU_FLIP_TIMING_GAP = -2,
    PS5_GPU_FLIP_TIMING_CPU_ORDER = -3,
    PS5_GPU_FLIP_TIMING_GPU_TIMESTAMP = -4,
    PS5_GPU_FLIP_TIMING_OVERFLOW = -5,
    PS5_GPU_FLIP_TIMING_INCOMPLETE = -6,
};

int ps5_gpu_flip_timing_init(Ps5GpuFlipTimingAccumulator *timing,
                             uint64_t expected_frames);

int ps5_gpu_flip_timing_record(Ps5GpuFlipTimingAccumulator *timing,
                               uint64_t frame, uint32_t slot,
                               uint64_t token, uint64_t cpu_submit_ns,
                               uint64_t cpu_gpu_observed_ns,
                               uint64_t cpu_flip_observed_ns,
                               uint64_t gpu_eop_ticks,
                               Ps5GpuFlipTimingSample *sample);

int ps5_gpu_flip_timing_finish(const Ps5GpuFlipTimingAccumulator *timing,
                               Ps5GpuFlipTimingSummary *summary);

#endif
