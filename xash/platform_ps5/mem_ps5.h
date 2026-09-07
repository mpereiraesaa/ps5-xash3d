#ifndef XASH_MEM_PS5_H
#define XASH_MEM_PS5_H

#include "memory_arena_ps5.h"

typedef struct Ps5MemoryRootStats
{
	size_t bytes;
	int64_t offset;
	int reserve_rc;
	int allocate_rc;
	int map_rc;
	int unmap_rc;
	int release_rc;
	uint32_t reserve_calls;
	uint32_t allocate_calls;
	uint32_t map_calls;
	uint32_t unmap_calls;
	uint32_t release_calls;
	uint32_t initialized;
	uint32_t mapped;
	uint32_t allocated;
	uint64_t foreign_calls;
	uint64_t foreign_bytes;
} Ps5MemoryRootStats;

void PS5_MemStats( Ps5MemoryStats *arena, Ps5MemoryRootStats *root );
int PS5_MemValidate( void );
int PS5_MemShutdown( void );

int PS5_MemGpuAllocate( size_t bytes, size_t alignment,
	Ps5GpuAllocation *allocation );
void *PS5_MemGpuPointer( const Ps5GpuAllocation *allocation );
int PS5_MemGpuReleaseUnsubmitted( const Ps5GpuAllocation *allocation );
int PS5_MemGpuRetire( const Ps5GpuAllocation *allocation,
	uint64_t retire_token );
int PS5_MemGpuReclaim( const Ps5GpuAllocation *allocation,
	uint64_t completed_token, int completion_proven );

int PS5_MemoryGateRun( void );

#endif
