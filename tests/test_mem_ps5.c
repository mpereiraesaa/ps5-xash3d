#include "../xash/platform_ps5/mem_ps5.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { ROOT_BYTES = 1024 * 1024 };
static _Alignas( 65536 ) unsigned char root_memory[ROOT_BYTES];
static int reserve_calls;
static int allocate_calls;
static int map_calls;
static int unmap_calls;
static int release_calls;

void *__real_malloc( size_t size ) { return malloc( size ); }
void __real_free( void *pointer ) { free( pointer ); }
void *__real_realloc( void *pointer, size_t size ) { return realloc( pointer, size ); }

int sceKernelReserveVirtualRange( void **address, size_t bytes,
	int flags, size_t alignment )
{
	assert( ++reserve_calls == 1 );
	assert( bytes == ROOT_BYTES && flags == 0 && alignment == 65536u );
	*address = root_memory;
	return 0;
}

int sceKernelAllocateMainDirectMemory( size_t bytes, size_t alignment,
	int memory_type, int64_t *offset )
{
	assert( ++allocate_calls == 1 );
	assert( bytes == ROOT_BYTES && alignment == 65536u && memory_type == 0x0c );
	*offset = 0x12340000;
	return 0;
}

int sceKernelMapDirectMemory( void **address, size_t bytes, int protection,
	int flags, int64_t offset, size_t alignment )
{
	assert( ++map_calls == 1 );
	assert( *address == root_memory && bytes == ROOT_BYTES );
	assert( protection == 0xf2 && flags == 0x10 && offset == 0x12340000 );
	assert( alignment == 0u );
	return 0;
}

int sceKernelMunmap( void *address, size_t bytes )
{
	assert( ++unmap_calls == 1 );
	assert( address == root_memory && bytes == ROOT_BYTES );
	return 0;
}

int sceKernelReleaseDirectMemory( int64_t offset, size_t bytes )
{
	assert( ++release_calls == 1 );
	assert( offset == 0x12340000 && bytes == ROOT_BYTES );
	return 0;
}

void *__wrap_malloc( size_t size );
void __wrap_free( void *pointer );
void *__wrap_calloc( size_t count, size_t size );
void *__wrap_realloc( void *pointer, size_t size );
void *__wrap_memalign( size_t alignment, size_t size );
void *__wrap_aligned_alloc( size_t alignment, size_t size );
int __wrap_posix_memalign( void **result, size_t alignment, size_t size );

int main( void )
{
	unsigned char *small = __wrap_malloc( 37u );
	unsigned char *zeroed = __wrap_calloc( 31u, 9u );
	unsigned char *aligned = __wrap_memalign( 4096u, 8192u );
	assert( small && zeroed && aligned );
	assert(((uintptr_t)small & 15u ) == 0u );
	assert(((uintptr_t)aligned & 4095u ) == 0u );
	for( size_t index = 0; index < 279u; ++index )
		assert( zeroed[index] == 0 );
	memset( small, 0x6b, 37u );
	small = __wrap_realloc( small, 65536u );
	assert( small );
	for( size_t index = 0; index < 37u; ++index )
		assert( small[index] == 0x6b );
	void *c11_aligned = __wrap_aligned_alloc( 256u, 1024u );
	assert( c11_aligned );
	assert( __wrap_aligned_alloc( 256u, 1000u ) == NULL );
	void *posix = NULL;
	assert( __wrap_posix_memalign( &posix, 512u, 2048u ) == 0 );
	assert( posix && ((uintptr_t)posix & 511u ) == 0u );

	Ps5GpuAllocation gpu = {0};
	assert( PS5_MemGpuAllocate( 131072u, 65536u, &gpu ) == PS5_MEMORY_OK );
	assert( PS5_MemGpuPointer( &gpu ) == gpu.pointer );
	assert( PS5_MemGpuRetire( &gpu, 42u ) == PS5_MEMORY_OK );
	assert( PS5_MemGpuReclaim( &gpu, 42u, 1 ) == PS5_MEMORY_OK );

	Ps5MemoryStats arena;
	Ps5MemoryRootStats root;
	PS5_MemStats( &arena, &root );
	assert( root.initialized && root.mapped && root.allocated );
	assert( root.reserve_calls == 1u && root.allocate_calls == 1u &&
		root.map_calls == 1u );
	assert( arena.live_cpu == 5u && arena.live_gpu == 0u );
	__wrap_free( zeroed );
	__wrap_free( aligned );
	__wrap_free( posix );
	__wrap_free( c11_aligned );
	__wrap_free( small );
	void *foreign = malloc( 73u );
	assert( foreign );
	__wrap_free( foreign );
	assert( PS5_MemShutdown( ) == PS5_MEMORY_OK );
	PS5_MemStats( &arena, &root );
	assert( arena.live_cpu == 0u && arena.live_gpu == 0u );
	assert( root.foreign_calls == 1u );
	assert( !root.mapped && !root.allocated );
	assert( root.unmap_calls == 1u && root.release_calls == 1u );
	assert( unmap_calls == 1 && release_calls == 1 );
	return 0;
}
