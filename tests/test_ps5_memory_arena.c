#include "../xash/platform_ps5/memory_arena_ps5.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

enum { ARENA_BYTES = 256 * 1024 };

static _Alignas( 16384 ) unsigned char memory[ARENA_BYTES];
static Ps5MemoryArena arena;

static void *stress_thread( void *opaque )
{
	const uintptr_t seed = (uintptr_t)opaque;
	for( size_t iteration = 0; iteration < 2000; ++iteration )
	{
		const size_t bytes = 17u + (( iteration * 37u + seed * 11u ) % 1024u );
		unsigned char *value = PS5_MemoryArenaAlloc( &arena, bytes, 16u );
		assert( value );
		memset( value, (int)( seed + iteration ), bytes );
		assert( PS5_MemoryArenaFree( &arena, value ) == PS5_MEMORY_OK );
	}
	return NULL;
}

int main( void )
{
	assert( PS5_MemoryArenaInit( NULL, memory, sizeof( memory )) ==
		PS5_MEMORY_PRECONDITION );
	assert( PS5_MemoryArenaInit( &arena, memory + 1, sizeof( memory ) - 1 ) ==
		PS5_MEMORY_PRECONDITION );
	assert( PS5_MemoryArenaInit( &arena, memory, sizeof( memory )) == PS5_MEMORY_OK );

	unsigned char *small = PS5_MemoryArenaAlloc( &arena, 31u, 16u );
	unsigned char *aligned = PS5_MemoryArenaAlloc( &arena, 4096u, 4096u );
	unsigned char *zeroed = PS5_MemoryArenaCalloc( &arena, 64u, 7u );
	assert( small && aligned && zeroed );
	assert(((uintptr_t)small & 15u ) == 0u );
	assert(((uintptr_t)aligned & 4095u ) == 0u );
	for( size_t index = 0; index < 448u; ++index )
		assert( zeroed[index] == 0 );
	memset( small, 0x5a, 31u );
	small = PS5_MemoryArenaRealloc( &arena, small, 8192u );
	assert( small );
	for( size_t index = 0; index < 31u; ++index )
		assert( small[index] == 0x5a );
	small = PS5_MemoryArenaRealloc( &arena, small, 19u );
	assert( small );
	for( size_t index = 0; index < 19u; ++index )
		assert( small[index] == 0x5a );
	assert( PS5_MemoryArenaFree( &arena, aligned ) == PS5_MEMORY_OK );
	assert( PS5_MemoryArenaFree( &arena, zeroed ) == PS5_MEMORY_OK );
	assert( PS5_MemoryArenaFree( &arena, small ) == PS5_MEMORY_OK );

	Ps5GpuAllocation first = {0};
	Ps5GpuAllocation second = {0};
	assert( PS5_MemoryGpuAllocate( &arena, 32768u, 256u, &first ) ==
		PS5_MEMORY_OK );
	assert( first.pointer && ((uintptr_t)first.pointer & 255u ) == 0u );
	memset( first.pointer, 0xa5, first.bytes );
	assert( PS5_MemoryGpuRetire( &arena, &first, 77u ) == PS5_MEMORY_OK );
	assert( PS5_MemoryGpuReclaim( &arena, &first, 77u, 0 ) ==
		PS5_MEMORY_COMPLETION_REQUIRED );
	assert( PS5_MemoryGpuReclaim( &arena, &first, 76u, 1 ) ==
		PS5_MEMORY_TOKEN_MISMATCH );
	assert( PS5_MemoryGpuPointer( &arena, &first ) == first.pointer );
	assert( PS5_MemoryGpuReclaim( &arena, &first, 77u, 1 ) == PS5_MEMORY_OK );
	assert( PS5_MemoryGpuPointer( &arena, &first ) == NULL );
	assert( PS5_MemoryGpuAllocate( &arena, 32768u, 256u, &second ) ==
		PS5_MEMORY_OK );
	assert( second.block_offset == first.block_offset );
	assert( second.generation != first.generation );
	assert( PS5_MemoryGpuRetire( &arena, &first, 88u ) == PS5_MEMORY_STALE );
	assert( PS5_MemoryGpuRetire( &arena, &second, 88u ) == PS5_MEMORY_OK );
	assert( PS5_MemoryGpuReclaim( &arena, &second, 88u, 1 ) == PS5_MEMORY_OK );
	Ps5GpuAllocation unsubmitted = {0};
	assert( PS5_MemoryGpuAllocate( &arena, 2048u, 256u, &unsubmitted ) ==
		PS5_MEMORY_OK );
	assert( PS5_MemoryGpuReleaseUnsubmitted( &arena, &unsubmitted ) ==
		PS5_MEMORY_OK );

	unsigned char *guarded = PS5_MemoryArenaAlloc( &arena, 32u, 16u );
	assert( guarded );
	guarded[32] ^= 1u;
	assert( PS5_MemoryArenaValidate( &arena ) == PS5_MEMORY_CORRUPT );
	assert( PS5_MemoryArenaFree( &arena, guarded ) == PS5_MEMORY_OK );
	assert( PS5_MemoryArenaValidate( &arena ) == PS5_MEMORY_OK );
	assert( PS5_MemoryArenaAlloc( &arena, sizeof( memory ), 16u ) == NULL );

	pthread_t threads[4];
	for( uintptr_t index = 0; index < 4; ++index )
		assert( pthread_create( &threads[index], NULL, stress_thread,
			(void *)( index + 1u )) == 0 );
	for( size_t index = 0; index < 4; ++index )
		assert( pthread_join( threads[index], NULL ) == 0 );
	void *lifetime_a = PS5_MemoryArenaAlloc( &arena, 73u, 16u );
	void *lifetime_b = PS5_MemoryArenaAlloc( &arena, 4097u, 64u );
	assert( lifetime_a && lifetime_b );
	assert( PS5_MemoryArenaReleaseProcessLifetime( &arena ) ==
		PS5_MEMORY_OK );

	Ps5MemoryStats stats;
	PS5_MemoryArenaStats( &arena, &stats );
	assert( stats.arena_bytes == sizeof( memory ));
	assert( stats.live_bytes == 0u );
	assert( stats.live_cpu == 0u );
	assert( stats.live_gpu == 0u );
	assert( stats.retiring_gpu == 0u );
	assert( stats.peak_bytes >= 32768u );
	assert( stats.alloc_calls >= 8008u );
	assert( stats.free_calls >= 8005u );
	assert( stats.realloc_calls == 2u );
	assert( stats.failures == 1u );
	assert( stats.guard_failures >= 1u );
	assert( stats.retire_calls == 2u );
	assert( stats.reclaim_calls == 2u );
	assert( stats.lifetime_reclaims == 2u );
	assert( stats.lifetime_bytes == 4170u );
	return 0;
}
