#ifndef XASH_MEMORY_ARENA_PS5_H
#define XASH_MEMORY_ARENA_PS5_H

#include <stddef.h>
#include <stdint.h>

typedef struct Ps5MemoryStats
{
	size_t arena_bytes;
	size_t live_bytes;
	size_t peak_bytes;
	uint64_t alloc_calls;
	uint64_t free_calls;
	uint64_t realloc_calls;
	uint64_t failures;
	uint64_t guard_failures;
	uint64_t stale_errors;
	uint64_t retire_calls;
	uint64_t reclaim_calls;
	uint64_t lifetime_reclaims;
	uint64_t lifetime_bytes;
	uint32_t live_cpu;
	uint32_t live_gpu;
	uint32_t retiring_gpu;
} Ps5MemoryStats;

typedef struct Ps5GpuAllocation
{
	void *pointer;
	size_t block_offset;
	size_t bytes;
	uint32_t generation;
} Ps5GpuAllocation;

typedef struct Ps5MemoryArena
{
	unsigned char *base;
	size_t bytes;
	uint32_t next_generation;
	volatile unsigned char lock;
	Ps5MemoryStats stats;
} Ps5MemoryArena;

enum ps5_memory_result
{
	PS5_MEMORY_OK = 0,
	PS5_MEMORY_PRECONDITION = -1,
	PS5_MEMORY_EXHAUSTED = -2,
	PS5_MEMORY_STALE = -3,
	PS5_MEMORY_STATE = -4,
	PS5_MEMORY_COMPLETION_REQUIRED = -5,
	PS5_MEMORY_TOKEN_MISMATCH = -6,
	PS5_MEMORY_CORRUPT = -7,
};

int PS5_MemoryArenaInit( Ps5MemoryArena *arena, void *base, size_t bytes );
void *PS5_MemoryArenaAlloc( Ps5MemoryArena *arena, size_t bytes, size_t alignment );
void *PS5_MemoryArenaCalloc( Ps5MemoryArena *arena, size_t count, size_t bytes );
void *PS5_MemoryArenaRealloc( Ps5MemoryArena *arena, void *pointer, size_t bytes );
int PS5_MemoryArenaFree( Ps5MemoryArena *arena, void *pointer );
int PS5_MemoryArenaOwns( const Ps5MemoryArena *arena, const void *pointer );

int PS5_MemoryGpuAllocate( Ps5MemoryArena *arena, size_t bytes, size_t alignment,
	Ps5GpuAllocation *allocation );
void *PS5_MemoryGpuPointer( Ps5MemoryArena *arena,
	const Ps5GpuAllocation *allocation );
int PS5_MemoryGpuReleaseUnsubmitted( Ps5MemoryArena *arena,
	const Ps5GpuAllocation *allocation );
int PS5_MemoryGpuRetire( Ps5MemoryArena *arena,
	const Ps5GpuAllocation *allocation, uint64_t retire_token );
int PS5_MemoryGpuReclaim( Ps5MemoryArena *arena,
	const Ps5GpuAllocation *allocation, uint64_t completed_token,
	int completion_proven );

int PS5_MemoryArenaValidate( Ps5MemoryArena *arena );
int PS5_MemoryArenaReleaseProcessLifetime( Ps5MemoryArena *arena );
void PS5_MemoryArenaStats( Ps5MemoryArena *arena, Ps5MemoryStats *stats );

#endif
