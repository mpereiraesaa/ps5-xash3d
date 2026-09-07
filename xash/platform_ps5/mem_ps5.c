/*
mem_ps5.c - direct-memory allocator adapter for Xash3D on PlayStation 5
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

All allocation calls made by the statically linked engine and its modules are
wrapped at final link. A single direct-memory mapping owns the arena; allocations
inside it carry generation cookies and tail guards. Pointers originating in a
shared system library remain owned by libc and are returned to libc unchanged.
*/

#include "mem_ps5.h"
#include "ps5_platform.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef PS5_ENGINE_HEAP_BYTES
#define PS5_ENGINE_HEAP_BYTES ( 128u * 1024u * 1024u )
#endif
#define PS5_ENGINE_HEAP_ALIGNMENT 65536u
#define PS5_ENGINE_MEMORY_TYPE 0x0c
#define PS5_ENGINE_PROTECTION 0xf2
#define PS5_ENGINE_MAP_FIXED 0x10
#define PS5_ENGINE_CPU_ALIGNMENT 16u

enum ps5_root_state
{
	PS5_ROOT_UNINITIALIZED = 0,
	PS5_ROOT_INITIALIZING = 1,
	PS5_ROOT_READY = 2,
	PS5_ROOT_FAILED = 3,
	PS5_ROOT_SHUTDOWN = 4,
};

extern void *__real_malloc( size_t size );
extern void __real_free( void *pointer );
extern void *__real_realloc( void *pointer, size_t size );

static Ps5MemoryArena ps5_arena;
static Ps5MemoryStats ps5_final_stats;
static Ps5MemoryRootStats ps5_root = {
	.offset = -1,
	.reserve_rc = -1,
	.allocate_rc = -1,
	.map_rc = -1,
	.unmap_rc = -1,
	.release_rc = -1,
};
static void *ps5_root_address;
static volatile int ps5_root_state;

static void foreign_call( size_t bytes )
{
	__atomic_add_fetch( &ps5_root.foreign_calls, 1u, __ATOMIC_RELAXED );
	__atomic_add_fetch( &ps5_root.foreign_bytes, bytes, __ATOMIC_RELAXED );
}

static int initialize_root( void )
{
	int expected = PS5_ROOT_UNINITIALIZED;
	if( __atomic_compare_exchange_n( &ps5_root_state, &expected,
		PS5_ROOT_INITIALIZING, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE ))
	{
		void *address = NULL;
		ps5_root.bytes = PS5_ENGINE_HEAP_BYTES;
		ps5_root.reserve_calls = 1;
		ps5_root.reserve_rc = sceKernelReserveVirtualRange( &address,
			PS5_ENGINE_HEAP_BYTES, 0, PS5_ENGINE_HEAP_ALIGNMENT );
		if( ps5_root.reserve_rc != 0 || !address )
			goto failed;
		ps5_root_address = address;
		ps5_root.allocate_calls = 1;
		ps5_root.allocate_rc = sceKernelAllocateMainDirectMemory(
			PS5_ENGINE_HEAP_BYTES, PS5_ENGINE_HEAP_ALIGNMENT,
			PS5_ENGINE_MEMORY_TYPE, &ps5_root.offset );
		if( ps5_root.allocate_rc != 0 || ps5_root.offset < 0 )
			goto failed;
		ps5_root.allocated = 1;
		ps5_root.map_calls = 1;
		ps5_root.map_rc = sceKernelMapDirectMemory( &address,
			PS5_ENGINE_HEAP_BYTES, PS5_ENGINE_PROTECTION,
			PS5_ENGINE_MAP_FIXED, ps5_root.offset, 0 );
		if( ps5_root.map_rc != 0 || address != ps5_root_address )
			goto failed;
		ps5_root.mapped = 1;
		if( PS5_MemoryArenaInit( &ps5_arena, address,
			PS5_ENGINE_HEAP_BYTES ) != PS5_MEMORY_OK )
			goto failed;
		ps5_root.initialized = 1;
		__atomic_store_n( &ps5_root_state, PS5_ROOT_READY, __ATOMIC_RELEASE );
		return 0;

failed:
		if( ps5_root.mapped || ps5_root_address )
		{
			ps5_root.unmap_calls++;
			ps5_root.unmap_rc = sceKernelMunmap( ps5_root_address,
				PS5_ENGINE_HEAP_BYTES );
			if( ps5_root.unmap_rc == 0 )
				ps5_root.mapped = 0;
		}
		if( ps5_root.allocated )
		{
			ps5_root.release_calls++;
			ps5_root.release_rc = sceKernelReleaseDirectMemory(
				ps5_root.offset, PS5_ENGINE_HEAP_BYTES );
			if( ps5_root.release_rc == 0 )
				ps5_root.allocated = 0;
		}
		__atomic_store_n( &ps5_root_state, PS5_ROOT_FAILED, __ATOMIC_RELEASE );
		return -1;
	}

	while( expected == PS5_ROOT_INITIALIZING )
		expected = __atomic_load_n( &ps5_root_state, __ATOMIC_ACQUIRE );
	return expected == PS5_ROOT_READY ? 0 : -1;
}

static int ready( void )
{
	const int state = __atomic_load_n( &ps5_root_state, __ATOMIC_ACQUIRE );
	if( state == PS5_ROOT_READY )
		return 1;
	if( state == PS5_ROOT_FAILED || state == PS5_ROOT_SHUTDOWN )
		return 0;
	return initialize_root( ) == 0;
}

void *__wrap_malloc( size_t size )
{
	void *pointer;
	if( !size )
		size = 1;
	if( !ready( ))
	{
		errno = ENOMEM;
		return NULL;
	}
	pointer = PS5_MemoryArenaAlloc( &ps5_arena, size,
		PS5_ENGINE_CPU_ALIGNMENT );
	if( !pointer )
		errno = ENOMEM;
	return pointer;
}

void __wrap_free( void *pointer )
{
	if( !pointer )
		return;
	if( PS5_MemoryArenaOwns( &ps5_arena, pointer ))
	{
		(void)PS5_MemoryArenaFree( &ps5_arena, pointer );
		return;
	}
	foreign_call( 0 );
	__real_free( pointer );
}

void *__wrap_calloc( size_t count, size_t size )
{
	if( count && size > SIZE_MAX / count )
	{
		errno = ENOMEM;
		return NULL;
	}
	if( !count || !size )
		return __wrap_malloc( 1 );
	if( !ready( ))
	{
		errno = ENOMEM;
		return NULL;
	}
	void *pointer = PS5_MemoryArenaCalloc( &ps5_arena, count, size );
	if( !pointer )
		errno = ENOMEM;
	return pointer;
}

void *__wrap_realloc( void *pointer, size_t size )
{
	if( !pointer )
		return __wrap_malloc( size );
	if( PS5_MemoryArenaOwns( &ps5_arena, pointer ))
	{
		void *result = PS5_MemoryArenaRealloc( &ps5_arena, pointer, size );
		if( size && !result )
			errno = ENOMEM;
		return result;
	}
	foreign_call( size );
	return __real_realloc( pointer, size );
}

void *__wrap_memalign( size_t alignment, size_t size )
{
	if( !alignment || ( alignment & ( alignment - 1u )) ||
		alignment < sizeof( void * ))
	{
		errno = EINVAL;
		return NULL;
	}
	if( !size )
		size = 1;
	if( !ready( ))
	{
		errno = ENOMEM;
		return NULL;
	}
	void *pointer = PS5_MemoryArenaAlloc( &ps5_arena, size, alignment );
	if( !pointer )
		errno = ENOMEM;
	return pointer;
}

void *__wrap_aligned_alloc( size_t alignment, size_t size )
{
	if( !alignment || size % alignment )
	{
		errno = EINVAL;
		return NULL;
	}
	return __wrap_memalign( alignment, size );
}

int __wrap_posix_memalign( void **result, size_t alignment, size_t size )
{
	void *pointer;
	if( !result || alignment < sizeof( void * ) ||
		( alignment & ( alignment - 1u )))
		return EINVAL;
	pointer = __wrap_memalign( alignment, size );
	if( !pointer )
		return ENOMEM;
	*result = pointer;
	return 0;
}

void PS5_MemStats( Ps5MemoryStats *arena, Ps5MemoryRootStats *root )
{
	if( arena )
	{
		if( __atomic_load_n( &ps5_root_state, __ATOMIC_ACQUIRE ) ==
			PS5_ROOT_READY )
			PS5_MemoryArenaStats( &ps5_arena, arena );
		else
			*arena = ps5_final_stats;
	}
	if( root )
		*root = ps5_root;
}

int PS5_MemValidate( void )
{
	return __atomic_load_n( &ps5_root_state, __ATOMIC_ACQUIRE ) ==
		PS5_ROOT_READY ? PS5_MemoryArenaValidate( &ps5_arena ) :
		PS5_MEMORY_PRECONDITION;
}

int PS5_MemGpuAllocate( size_t bytes, size_t alignment,
	Ps5GpuAllocation *allocation )
{
	if( !ready( ))
		return PS5_MEMORY_EXHAUSTED;
	return PS5_MemoryGpuAllocate( &ps5_arena, bytes, alignment, allocation );
}

void *PS5_MemGpuPointer( const Ps5GpuAllocation *allocation )
{
	return __atomic_load_n( &ps5_root_state, __ATOMIC_ACQUIRE ) ==
		PS5_ROOT_READY ?
		PS5_MemoryGpuPointer( &ps5_arena, allocation ) : NULL;
}

int PS5_MemGpuReleaseUnsubmitted( const Ps5GpuAllocation *allocation )
{
	return __atomic_load_n( &ps5_root_state, __ATOMIC_ACQUIRE ) ==
		PS5_ROOT_READY ? PS5_MemoryGpuReleaseUnsubmitted( &ps5_arena,
		allocation ) : PS5_MEMORY_PRECONDITION;
}

int PS5_MemGpuRetire( const Ps5GpuAllocation *allocation,
	uint64_t retire_token )
{
	return __atomic_load_n( &ps5_root_state, __ATOMIC_ACQUIRE ) ==
		PS5_ROOT_READY ? PS5_MemoryGpuRetire( &ps5_arena,
		allocation, retire_token ) : PS5_MEMORY_PRECONDITION;
}

int PS5_MemGpuReclaim( const Ps5GpuAllocation *allocation,
	uint64_t completed_token, int completion_proven )
{
	return __atomic_load_n( &ps5_root_state, __ATOMIC_ACQUIRE ) ==
		PS5_ROOT_READY ? PS5_MemoryGpuReclaim( &ps5_arena,
		allocation, completed_token, completion_proven ) :
		PS5_MEMORY_PRECONDITION;
}

int PS5_MemShutdown( void )
{
	Ps5MemoryStats stats;
	if( __atomic_load_n( &ps5_root_state, __ATOMIC_ACQUIRE ) != PS5_ROOT_READY )
		return PS5_MEMORY_STATE;
	if( PS5_MemoryArenaValidate( &ps5_arena ) != PS5_MEMORY_OK )
		return PS5_MEMORY_CORRUPT;
	PS5_MemoryArenaStats( &ps5_arena, &stats );
	if( stats.live_gpu || stats.retiring_gpu ||
		stats.guard_failures )
		return PS5_MEMORY_STATE;
	if( PS5_MemoryArenaReleaseProcessLifetime( &ps5_arena ) !=
		PS5_MEMORY_OK )
		return PS5_MEMORY_STATE;
	if( PS5_MemoryArenaValidate( &ps5_arena ) != PS5_MEMORY_OK )
		return PS5_MEMORY_CORRUPT;
	PS5_MemoryArenaStats( &ps5_arena, &stats );
	if( stats.live_bytes || stats.live_cpu || stats.live_gpu ||
		stats.retiring_gpu )
		return PS5_MEMORY_STATE;
	ps5_final_stats = stats;
	__atomic_store_n( &ps5_root_state, PS5_ROOT_SHUTDOWN, __ATOMIC_RELEASE );
	ps5_root.unmap_calls++;
	ps5_root.unmap_rc = sceKernelMunmap( ps5_root_address, ps5_root.bytes );
	if( ps5_root.unmap_rc != 0 )
		return PS5_MEMORY_STATE;
	ps5_root.mapped = 0;
	ps5_root.release_calls++;
	ps5_root.release_rc = sceKernelReleaseDirectMemory( ps5_root.offset,
		ps5_root.bytes );
	if( ps5_root.release_rc != 0 )
		return PS5_MEMORY_STATE;
	ps5_root.allocated = 0;
	return PS5_MEMORY_OK;
}
