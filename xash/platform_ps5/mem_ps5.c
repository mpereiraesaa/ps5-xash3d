/*
mem_ps5.c - large-allocation router for the Xash3D engine on PlayStation 5
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

libSceLibcInternal's malloc refused a 21 MiB request while the engine loaded
its first map (FW 12.02), and the application cannot size that heap: the
sceLibcHeapSize knobs are not part of the SDK stubs and the native converter
publishes no application exports. Anonymous mmap, measured in the laboratory
up to 432 MiB, is the memory the engine can actually reach.

The final link wraps malloc/free/realloc/calloc (lld --wrap), so every
allocation made by the engine, the statically linked modules and the
telemetry client passes through here: requests at or above
PS5_LARGE_ALLOC_BYTES are served by mmap and tracked in a small registry,
everything else stays with libc. This is the interim allocator; the direct-
memory engine allocator of the platform phase replaces it.
*/

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#ifndef PS5_LARGE_ALLOC_BYTES
#define PS5_LARGE_ALLOC_BYTES ( 256 * 1024 )
#endif
#define PS5_PAGE 16384u
#define PS5_LARGE_SLOTS 4096

extern void *__real_malloc( size_t size );
extern void __real_free( void *ptr );
extern void *__real_realloc( void *ptr, size_t size );
extern void *__real_calloc( size_t count, size_t size );
extern size_t malloc_usable_size( void *ptr );

typedef struct { void *ptr; size_t size; } ps5_large_t;

static ps5_large_t ps5_large[PS5_LARGE_SLOTS];
static int ps5_large_count;
static size_t ps5_large_bytes, ps5_large_peak;
static int ps5_large_failures;

static ps5_large_t *find_large( void *ptr )
{
	int i;
	for( i = 0; i < PS5_LARGE_SLOTS; i++ )
		if( ps5_large[i].ptr == ptr && ptr )
			return &ps5_large[i];
	return NULL;
}

static void *large_alloc( size_t size )
{
	size_t rounded = ( size + PS5_PAGE - 1 ) & ~(size_t)( PS5_PAGE - 1 );
	void *mem;
	int i;
	if( ps5_large_count >= PS5_LARGE_SLOTS )
	{
		ps5_large_failures++;
		return NULL;
	}
	mem = mmap( NULL, rounded, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );
	if( mem == MAP_FAILED )
	{
		ps5_large_failures++;
		return NULL;
	}
	for( i = 0; i < PS5_LARGE_SLOTS; i++ )
	{
		if( ps5_large[i].ptr == NULL )
		{
			ps5_large[i].ptr = mem;
			ps5_large[i].size = rounded;
			break;
		}
	}
	ps5_large_count++;
	ps5_large_bytes += rounded;
	if( ps5_large_bytes > ps5_large_peak )
		ps5_large_peak = ps5_large_bytes;
	return mem;
}

static void large_free( ps5_large_t *slot )
{
	munmap( slot->ptr, slot->size );
	ps5_large_bytes -= slot->size;
	ps5_large_count--;
	slot->ptr = NULL;
	slot->size = 0;
}

void *__wrap_malloc( size_t size )
{
	if( size >= PS5_LARGE_ALLOC_BYTES )
		return large_alloc( size );
	return __real_malloc( size );
}

void __wrap_free( void *ptr )
{
	ps5_large_t *slot;
	if( !ptr )
		return;
	slot = find_large( ptr );
	if( slot )
		large_free( slot );
	else
		__real_free( ptr );
}

void *__wrap_calloc( size_t count, size_t size )
{
	size_t total;
	if( count && size > SIZE_MAX / count )
	{
		errno = ENOMEM;
		return NULL;
	}
	total = count * size;
	if( total >= PS5_LARGE_ALLOC_BYTES )
		return large_alloc( total ); /* fresh anonymous pages are zero */
	return __real_calloc( count, size );
}

void *__wrap_realloc( void *ptr, size_t size )
{
	ps5_large_t *slot;
	void *mem;
	size_t old_size;
	if( !ptr )
		return __wrap_malloc( size );
	if( size == 0 )
	{
		__wrap_free( ptr );
		return NULL;
	}
	slot = find_large( ptr );
	if( !slot && size < PS5_LARGE_ALLOC_BYTES )
		return __real_realloc( ptr, size );
	if( slot && size <= slot->size )
		return ptr;
	mem = __wrap_malloc( size );
	if( !mem )
		return NULL;
	old_size = slot ? slot->size : malloc_usable_size( ptr );
	memcpy( mem, ptr, old_size < size ? old_size : size );
	__wrap_free( ptr );
	return mem;
}

void PS5_MemStats( size_t *bytes, size_t *peak, int *count, int *failures )
{
	*bytes = ps5_large_bytes;
	*peak = ps5_large_peak;
	*count = ps5_large_count;
	*failures = ps5_large_failures;
}
