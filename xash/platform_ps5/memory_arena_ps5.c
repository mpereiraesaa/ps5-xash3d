/*
memory_arena_ps5.c - bounded direct-memory allocator core for Xash3D on PS5
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.
*/

#include "memory_arena_ps5.h"

#include <limits.h>
#include <string.h>

#define PS5_MEMORY_BLOCK_MAGIC UINT64_C( 0x5053354d454d424c )
#define PS5_MEMORY_COOKIE_MAGIC UINT64_C( 0x5053354d454d434b )
#define PS5_MEMORY_GUARD_A UINT64_C( 0xd1e3c7a55a7c3e1d )
#define PS5_MEMORY_GUARD_B UINT64_C( 0x96b4f20cc02f4b69 )
#define PS5_MEMORY_GRANULARITY 16u

enum ps5_memory_block_state
{
	PS5_MEMORY_BLOCK_FREE = 0,
	PS5_MEMORY_BLOCK_CPU = 1,
	PS5_MEMORY_BLOCK_GPU = 2,
	PS5_MEMORY_BLOCK_GPU_RETIRING = 3,
};

typedef struct Ps5MemoryBlock
{
	uint64_t magic;
	size_t span;
	size_t previous_span;
	size_t requested;
	size_t payload_offset;
	uint64_t retire_token;
	uint32_t generation;
	uint32_t state;
	uint64_t reserved;
} Ps5MemoryBlock;

typedef struct Ps5MemoryCookie
{
	uint64_t magic;
	size_t block_offset;
	uint32_t generation;
	uint32_t reserved;
} Ps5MemoryCookie;

_Static_assert( sizeof( Ps5MemoryBlock ) % PS5_MEMORY_GRANULARITY == 0,
	"memory block alignment" );
_Static_assert( sizeof( Ps5MemoryCookie ) % 8u == 0,
	"memory cookie alignment" );

static int power_of_two( size_t value )
{
	return value && ( value & ( value - 1u )) == 0u;
}

static int add_size( size_t a, size_t b, size_t *result )
{
	if( !result || b > SIZE_MAX - a )
		return -1;
	*result = a + b;
	return 0;
}

static int align_address( uintptr_t value, size_t alignment, uintptr_t *result )
{
	if( !result || !power_of_two( alignment ) || value > UINTPTR_MAX - ( alignment - 1u ))
		return -1;
	*result = ( value + alignment - 1u ) & ~(uintptr_t)( alignment - 1u );
	return 0;
}

static int align_size( size_t value, size_t alignment, size_t *result )
{
	if( !result || !power_of_two( alignment ) || value > SIZE_MAX - ( alignment - 1u ))
		return -1;
	*result = ( value + alignment - 1u ) & ~( alignment - 1u );
	return 0;
}

static size_t minimum_span( void )
{
	return sizeof( Ps5MemoryBlock ) + sizeof( Ps5MemoryCookie ) +
		2u * sizeof( uint64_t ) + PS5_MEMORY_GRANULARITY;
}

static void arena_lock( Ps5MemoryArena *arena )
{
	while( __atomic_test_and_set( &arena->lock, __ATOMIC_ACQUIRE ))
		;
}

static void arena_unlock( Ps5MemoryArena *arena )
{
	__atomic_clear( &arena->lock, __ATOMIC_RELEASE );
}

static Ps5MemoryBlock *block_at( Ps5MemoryArena *arena, size_t offset )
{
	if( !arena || !arena->base || offset > arena->bytes ||
		sizeof( Ps5MemoryBlock ) > arena->bytes - offset )
		return NULL;
	return (Ps5MemoryBlock *)( arena->base + offset );
}

static void update_next_previous( Ps5MemoryArena *arena, size_t offset,
	const Ps5MemoryBlock *block )
{
	const size_t next_offset = offset + block->span;
	Ps5MemoryBlock *next = block_at( arena, next_offset );
	if( next_offset < arena->bytes && next )
		next->previous_span = block->span;
}

static void write_guard( Ps5MemoryArena *arena, Ps5MemoryBlock *block )
{
	const uint64_t guard[2] = {
		PS5_MEMORY_GUARD_A ^ block->generation ^ block->requested,
		PS5_MEMORY_GUARD_B ^ block->generation ^ block->payload_offset,
	};
	memcpy( arena->base + block->payload_offset + block->requested,
		guard, sizeof( guard ));
}

static int guard_valid( const Ps5MemoryArena *arena, const Ps5MemoryBlock *block )
{
	uint64_t guard[2];
	memcpy( guard, arena->base + block->payload_offset + block->requested,
		sizeof( guard ));
	return guard[0] == ( PS5_MEMORY_GUARD_A ^ block->generation ^ block->requested ) &&
		guard[1] == ( PS5_MEMORY_GUARD_B ^ block->generation ^ block->payload_offset );
}

static Ps5MemoryBlock *resolve_pointer_locked( Ps5MemoryArena *arena,
	const void *pointer, size_t *block_offset )
{
	const unsigned char *bytes = (const unsigned char *)pointer;
	if( !pointer || !PS5_MemoryArenaOwns( arena, pointer ) ||
		bytes < arena->base + sizeof( Ps5MemoryCookie ))
		return NULL;
	const Ps5MemoryCookie *cookie = (const Ps5MemoryCookie *)( bytes - sizeof( *cookie ));
	if( cookie->magic != PS5_MEMORY_COOKIE_MAGIC )
		return NULL;
	Ps5MemoryBlock *block = block_at( arena, cookie->block_offset );
	if( !block || block->magic != PS5_MEMORY_BLOCK_MAGIC ||
		block->generation != cookie->generation ||
		block->payload_offset >= arena->bytes ||
		arena->base + block->payload_offset != bytes ||
		block->state == PS5_MEMORY_BLOCK_FREE )
		return NULL;
	if( block_offset )
		*block_offset = cookie->block_offset;
	return block;
}

static Ps5MemoryBlock *resolve_gpu_locked( Ps5MemoryArena *arena,
	const Ps5GpuAllocation *allocation )
{
	Ps5MemoryBlock *block;
	if( !allocation || allocation->block_offset >= arena->bytes )
		return NULL;
	block = block_at( arena, allocation->block_offset );
	if( !block || block->magic != PS5_MEMORY_BLOCK_MAGIC ||
		block->generation != allocation->generation ||
		block->requested != allocation->bytes ||
		block->payload_offset >= arena->bytes ||
		arena->base + block->payload_offset != allocation->pointer ||
		( block->state != PS5_MEMORY_BLOCK_GPU &&
		  block->state != PS5_MEMORY_BLOCK_GPU_RETIRING ))
		return NULL;
	return block;
}

static void coalesce_locked( Ps5MemoryArena *arena, size_t offset )
{
	Ps5MemoryBlock *block = block_at( arena, offset );
	if( !block || block->state != PS5_MEMORY_BLOCK_FREE )
		return;
	for( ;; )
	{
		const size_t next_offset = offset + block->span;
		Ps5MemoryBlock *next = block_at( arena, next_offset );
		if( next_offset >= arena->bytes || !next ||
			next->magic != PS5_MEMORY_BLOCK_MAGIC ||
			next->state != PS5_MEMORY_BLOCK_FREE )
			break;
		block->span += next->span;
		update_next_previous( arena, offset, block );
	}
	if( block->previous_span && block->previous_span <= offset )
	{
		const size_t previous_offset = offset - block->previous_span;
		Ps5MemoryBlock *previous = block_at( arena, previous_offset );
		if( previous && previous->magic == PS5_MEMORY_BLOCK_MAGIC &&
			previous->state == PS5_MEMORY_BLOCK_FREE &&
			previous->span == block->previous_span )
		{
			previous->span += block->span;
			update_next_previous( arena, previous_offset, previous );
		}
	}
}

static void release_block_locked( Ps5MemoryArena *arena, size_t offset,
	Ps5MemoryBlock *block )
{
	if( !guard_valid( arena, block ))
		arena->stats.guard_failures++;
	arena->stats.live_bytes -= block->requested;
	if( block->state == PS5_MEMORY_BLOCK_CPU )
	{
		arena->stats.live_cpu--;
		arena->stats.free_calls++;
	}
	else
	{
		if( block->state == PS5_MEMORY_BLOCK_GPU_RETIRING )
			arena->stats.retiring_gpu--;
		arena->stats.live_gpu--;
	}
	block->requested = 0;
	block->payload_offset = 0;
	block->retire_token = 0;
	block->state = PS5_MEMORY_BLOCK_FREE;
	coalesce_locked( arena, offset );
}

static void *allocate_locked( Ps5MemoryArena *arena, size_t requested,
	size_t alignment, uint32_t state, size_t *block_offset_out )
{
	size_t offset = 0;
	if( !requested || !power_of_two( alignment ) || alignment < sizeof( void * ))
		return NULL;
	while( offset < arena->bytes )
	{
		Ps5MemoryBlock *block = block_at( arena, offset );
		uintptr_t payload_address;
		size_t end_offset, required;
		if( !block || block->magic != PS5_MEMORY_BLOCK_MAGIC || !block->span ||
			block->span > arena->bytes - offset )
			return NULL;
		if( block->state == PS5_MEMORY_BLOCK_FREE &&
			align_address((uintptr_t)( arena->base + offset + sizeof( *block ) +
				sizeof( Ps5MemoryCookie )), alignment, &payload_address ) == 0 &&
			payload_address >= (uintptr_t)arena->base &&
			payload_address - (uintptr_t)arena->base <= SIZE_MAX )
		{
			const size_t payload_offset = (size_t)( payload_address - (uintptr_t)arena->base );
			if( add_size( payload_offset, requested, &end_offset ) == 0 &&
				add_size( end_offset, 2u * sizeof( uint64_t ), &end_offset ) == 0 &&
				end_offset >= offset &&
				align_size( end_offset - offset, PS5_MEMORY_GRANULARITY,
					&required ) == 0 && required <= block->span )
			{
				const size_t old_span = block->span;
				const size_t remainder = old_span - required;
				if( remainder >= minimum_span( ))
				{
					Ps5MemoryBlock *split = (Ps5MemoryBlock *)( arena->base + offset + required );
					*split = (Ps5MemoryBlock){
						.magic = PS5_MEMORY_BLOCK_MAGIC,
						.span = remainder,
						.previous_span = required,
						.state = PS5_MEMORY_BLOCK_FREE,
					};
					block->span = required;
					update_next_previous( arena, offset + required, split );
				}
				uint32_t generation = arena->next_generation++;
				if( generation == 0 )
					generation = arena->next_generation++;
				block->requested = requested;
				block->payload_offset = payload_offset;
				block->retire_token = 0;
				block->generation = generation;
				block->state = state;
				Ps5MemoryCookie *cookie = (Ps5MemoryCookie *)( arena->base + payload_offset - sizeof( *cookie ));
				*cookie = (Ps5MemoryCookie){
					.magic = PS5_MEMORY_COOKIE_MAGIC,
					.block_offset = offset,
					.generation = generation,
				};
				write_guard( arena, block );
				arena->stats.alloc_calls++;
				arena->stats.live_bytes += requested;
				if( arena->stats.live_bytes > arena->stats.peak_bytes )
					arena->stats.peak_bytes = arena->stats.live_bytes;
				if( state == PS5_MEMORY_BLOCK_CPU )
					arena->stats.live_cpu++;
				else
					arena->stats.live_gpu++;
				if( block_offset_out )
					*block_offset_out = offset;
				return arena->base + payload_offset;
			}
		}
		offset += block->span;
	}
	return NULL;
}

int PS5_MemoryArenaInit( Ps5MemoryArena *arena, void *base, size_t bytes )
{
	if( !arena || !base || bytes < minimum_span( ) ||
		((uintptr_t)base & ( PS5_MEMORY_GRANULARITY - 1u )) ||
		( bytes & ( PS5_MEMORY_GRANULARITY - 1u )))
		return PS5_MEMORY_PRECONDITION;
	memset( arena, 0, sizeof( *arena ));
	arena->base = (unsigned char *)base;
	arena->bytes = bytes;
	arena->next_generation = 1;
	arena->stats.arena_bytes = bytes;
	Ps5MemoryBlock *first = (Ps5MemoryBlock *)base;
	*first = (Ps5MemoryBlock){
		.magic = PS5_MEMORY_BLOCK_MAGIC,
		.span = bytes,
		.state = PS5_MEMORY_BLOCK_FREE,
	};
	return PS5_MEMORY_OK;
}

void *PS5_MemoryArenaAlloc( Ps5MemoryArena *arena, size_t bytes, size_t alignment )
{
	void *result;
	if( !arena || !arena->base || !bytes || !power_of_two( alignment ))
		return NULL;
	arena_lock( arena );
	result = allocate_locked( arena, bytes, alignment, PS5_MEMORY_BLOCK_CPU, NULL );
	if( !result )
		arena->stats.failures++;
	arena_unlock( arena );
	return result;
}

void *PS5_MemoryArenaCalloc( Ps5MemoryArena *arena, size_t count, size_t bytes )
{
	void *result;
	if( count && bytes > SIZE_MAX / count )
		return NULL;
	result = PS5_MemoryArenaAlloc( arena, count * bytes, PS5_MEMORY_GRANULARITY );
	if( result )
		memset( result, 0, count * bytes );
	return result;
}

int PS5_MemoryArenaFree( Ps5MemoryArena *arena, void *pointer )
{
	size_t offset;
	Ps5MemoryBlock *block;
	if( !arena || !arena->base || !pointer )
		return pointer ? PS5_MEMORY_PRECONDITION : PS5_MEMORY_OK;
	arena_lock( arena );
	block = resolve_pointer_locked( arena, pointer, &offset );
	if( !block || block->state != PS5_MEMORY_BLOCK_CPU )
	{
		arena->stats.stale_errors++;
		arena_unlock( arena );
		return block ? PS5_MEMORY_STATE : PS5_MEMORY_STALE;
	}
	release_block_locked( arena, offset, block );
	arena_unlock( arena );
	return PS5_MEMORY_OK;
}

void *PS5_MemoryArenaRealloc( Ps5MemoryArena *arena, void *pointer, size_t bytes )
{
	Ps5MemoryBlock *block;
	size_t old_size;
	void *result;
	if( !pointer )
		return PS5_MemoryArenaAlloc( arena, bytes, PS5_MEMORY_GRANULARITY );
	if( !bytes )
	{
		(void)PS5_MemoryArenaFree( arena, pointer );
		return NULL;
	}
	if( !arena || !arena->base )
		return NULL;
	arena_lock( arena );
	block = resolve_pointer_locked( arena, pointer, NULL );
	if( !block || block->state != PS5_MEMORY_BLOCK_CPU )
	{
		arena->stats.stale_errors++;
		arena_unlock( arena );
		return NULL;
	}
	old_size = block->requested;
	arena->stats.realloc_calls++;
	if( bytes <= old_size )
	{
		arena->stats.live_bytes -= old_size - bytes;
		block->requested = bytes;
		write_guard( arena, block );
		arena_unlock( arena );
		return pointer;
	}
	arena_unlock( arena );
	result = PS5_MemoryArenaAlloc( arena, bytes, PS5_MEMORY_GRANULARITY );
	if( !result )
		return NULL;
	memcpy( result, pointer, old_size );
	if( PS5_MemoryArenaFree( arena, pointer ) != PS5_MEMORY_OK )
	{
		(void)PS5_MemoryArenaFree( arena, result );
		return NULL;
	}
	return result;
}

int PS5_MemoryArenaOwns( const Ps5MemoryArena *arena, const void *pointer )
{
	const uintptr_t address = (uintptr_t)pointer;
	const uintptr_t base = arena ? (uintptr_t)arena->base : 0u;
	return arena && arena->base && pointer && address >= base &&
		address - base < arena->bytes;
}

int PS5_MemoryGpuAllocate( Ps5MemoryArena *arena, size_t bytes, size_t alignment,
	Ps5GpuAllocation *allocation )
{
	size_t block_offset = 0;
	void *pointer;
	if( !arena || !allocation )
		return PS5_MEMORY_PRECONDITION;
	arena_lock( arena );
	pointer = allocate_locked( arena, bytes, alignment, PS5_MEMORY_BLOCK_GPU,
		&block_offset );
	if( !pointer )
	{
		arena->stats.failures++;
		arena_unlock( arena );
		return PS5_MEMORY_EXHAUSTED;
	}
	Ps5MemoryBlock *block = block_at( arena, block_offset );
	*allocation = (Ps5GpuAllocation){
		.pointer = pointer,
		.block_offset = block_offset,
		.bytes = bytes,
		.generation = block->generation,
	};
	arena_unlock( arena );
	return PS5_MEMORY_OK;
}

void *PS5_MemoryGpuPointer( Ps5MemoryArena *arena,
	const Ps5GpuAllocation *allocation )
{
	void *pointer = NULL;
	if( !arena )
		return NULL;
	arena_lock( arena );
	Ps5MemoryBlock *block = resolve_gpu_locked( arena, allocation );
	if( block )
		pointer = arena->base + block->payload_offset;
	else
		arena->stats.stale_errors++;
	arena_unlock( arena );
	return pointer;
}

int PS5_MemoryGpuReleaseUnsubmitted( Ps5MemoryArena *arena,
	const Ps5GpuAllocation *allocation )
{
	Ps5MemoryBlock *block;
	if( !arena )
		return PS5_MEMORY_PRECONDITION;
	arena_lock( arena );
	block = resolve_gpu_locked( arena, allocation );
	if( !block )
	{
		arena->stats.stale_errors++;
		arena_unlock( arena );
		return PS5_MEMORY_STALE;
	}
	if( block->state != PS5_MEMORY_BLOCK_GPU )
	{
		arena_unlock( arena );
		return PS5_MEMORY_STATE;
	}
	release_block_locked( arena, allocation->block_offset, block );
	arena_unlock( arena );
	return PS5_MEMORY_OK;
}

int PS5_MemoryGpuRetire( Ps5MemoryArena *arena,
	const Ps5GpuAllocation *allocation, uint64_t retire_token )
{
	Ps5MemoryBlock *block;
	if( !arena || !retire_token )
		return PS5_MEMORY_PRECONDITION;
	arena_lock( arena );
	block = resolve_gpu_locked( arena, allocation );
	if( !block )
	{
		arena->stats.stale_errors++;
		arena_unlock( arena );
		return PS5_MEMORY_STALE;
	}
	if( block->state != PS5_MEMORY_BLOCK_GPU )
	{
		arena_unlock( arena );
		return PS5_MEMORY_STATE;
	}
	block->state = PS5_MEMORY_BLOCK_GPU_RETIRING;
	block->retire_token = retire_token;
	arena->stats.retire_calls++;
	arena->stats.retiring_gpu++;
	arena_unlock( arena );
	return PS5_MEMORY_OK;
}

int PS5_MemoryGpuReclaim( Ps5MemoryArena *arena,
	const Ps5GpuAllocation *allocation, uint64_t completed_token,
	int completion_proven )
{
	Ps5MemoryBlock *block;
	if( !arena || !completed_token )
		return PS5_MEMORY_PRECONDITION;
	arena_lock( arena );
	block = resolve_gpu_locked( arena, allocation );
	if( !block )
	{
		arena->stats.stale_errors++;
		arena_unlock( arena );
		return PS5_MEMORY_STALE;
	}
	if( block->state != PS5_MEMORY_BLOCK_GPU_RETIRING )
	{
		arena_unlock( arena );
		return PS5_MEMORY_STATE;
	}
	if( !completion_proven )
	{
		arena_unlock( arena );
		return PS5_MEMORY_COMPLETION_REQUIRED;
	}
	if( block->retire_token != completed_token )
	{
		arena_unlock( arena );
		return PS5_MEMORY_TOKEN_MISMATCH;
	}
	const size_t block_offset = allocation->block_offset;
	release_block_locked( arena, block_offset, block );
	arena->stats.reclaim_calls++;
	arena_unlock( arena );
	return PS5_MEMORY_OK;
}

int PS5_MemoryArenaValidate( Ps5MemoryArena *arena )
{
	size_t offset = 0;
	size_t previous_span = 0;
	int result = PS5_MEMORY_OK;
	if( !arena || !arena->base )
		return PS5_MEMORY_PRECONDITION;
	arena_lock( arena );
	while( offset < arena->bytes )
	{
		Ps5MemoryBlock *block = block_at( arena, offset );
		if( !block || block->magic != PS5_MEMORY_BLOCK_MAGIC ||
			block->previous_span != previous_span || !block->span ||
			( block->span & ( PS5_MEMORY_GRANULARITY - 1u )) ||
			block->span > arena->bytes - offset )
		{
			result = PS5_MEMORY_CORRUPT;
			break;
		}
		if( block->state != PS5_MEMORY_BLOCK_FREE )
		{
			if( block->payload_offset < offset + sizeof( *block ) ||
				block->payload_offset > arena->bytes ||
				block->requested > arena->bytes - block->payload_offset ||
				2u * sizeof( uint64_t ) > arena->bytes -
					block->payload_offset - block->requested ||
				!guard_valid( arena, block ))
			{
				arena->stats.guard_failures++;
				result = PS5_MEMORY_CORRUPT;
				break;
			}
		}
		previous_span = block->span;
		offset += block->span;
	}
	if( offset != arena->bytes )
		result = PS5_MEMORY_CORRUPT;
	arena_unlock( arena );
	return result;
}

int PS5_MemoryArenaReleaseProcessLifetime( Ps5MemoryArena *arena )
{
	size_t offset = 0;
	if( !arena || !arena->base )
		return PS5_MEMORY_PRECONDITION;
	arena_lock( arena );
	if( arena->stats.live_gpu || arena->stats.retiring_gpu )
	{
		arena_unlock( arena );
		return PS5_MEMORY_STATE;
	}
	while( offset < arena->bytes )
	{
		Ps5MemoryBlock *block = block_at( arena, offset );
		if( !block || block->magic != PS5_MEMORY_BLOCK_MAGIC || !block->span ||
			block->span > arena->bytes - offset )
		{
			arena_unlock( arena );
			return PS5_MEMORY_CORRUPT;
		}
		if( block->state == PS5_MEMORY_BLOCK_CPU )
		{
			if( !guard_valid( arena, block ))
			{
				arena->stats.guard_failures++;
				arena_unlock( arena );
				return PS5_MEMORY_CORRUPT;
			}
			arena->stats.lifetime_reclaims++;
			arena->stats.lifetime_bytes += block->requested;
			release_block_locked( arena, offset, block );
			offset = 0;
			continue;
		}
		offset += block->span;
	}
	arena_unlock( arena );
	return PS5_MEMORY_OK;
}

void PS5_MemoryArenaStats( Ps5MemoryArena *arena, Ps5MemoryStats *stats )
{
	if( !arena || !stats )
		return;
	arena_lock( arena );
	*stats = arena->stats;
	arena_unlock( arena );
}
