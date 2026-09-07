/*
memory_gate_ps5.c - Phase 5 direct-memory allocator hardware gate
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.
*/

#include "mem_ps5.h"
#include "ps5log.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct Ps5MemoryGateResource
{
	const char *kind;
	size_t bytes;
	size_t alignment;
	unsigned char seed;
	Ps5GpuAllocation allocation;
	int state;
} Ps5MemoryGateResource;

static uint64_t fill_and_hash( unsigned char *memory, size_t bytes,
	unsigned char seed )
{
	uint64_t hash = UINT64_C( 1469598103934665603 );
	for( size_t index = 0; index < bytes; ++index )
	{
		const unsigned char value = (unsigned char)( seed +
			( index * 17u ) + ( index >> 8 ));
		memory[index] = value;
		hash ^= value;
		hash *= UINT64_C( 1099511628211 );
	}
	return hash;
}

int PS5_MemoryGateRun( void )
{
	static const uint64_t retire_token = UINT64_C( 0x5048354d454d0001 );
	Ps5MemoryGateResource resources[] = {
		{ "command", 2u * 1024u * 1024u, 256u, 0x11u, {0}, 0 },
		{ "buffer", 4u * 1024u * 1024u, 65536u, 0x33u, {0}, 0 },
		{ "texture", 8u * 1024u * 1024u, 65536u, 0x55u, {0}, 0 },
		{ "depth", 4u * 1024u * 1024u, 65536u, 0x77u, {0}, 0 },
	};
	Ps5MemoryStats before = {0};
	Ps5MemoryStats after = {0};
	Ps5MemoryRootStats root = {0};
	unsigned char *cpu = NULL;
	unsigned char *zeroed = NULL;
	size_t allocated = 0;
	uint64_t combined_hash = UINT64_C( 1469598103934665603 );
	int result = 0;

	(void)ps5log_line( PS5LOG_MARK,
		"XASH_MEMORY_BEGIN schema=1 arena=direct root_mib=128 "
		"cpu=malloc+calloc+realloc+free gpu=command+buffer+texture+depth" );
	cpu = malloc( 257u );
	zeroed = calloc( 1024u, 4u );
	if( !cpu || !zeroed )
	{
		result = -1;
		goto cleanup;
	}
	for( size_t index = 0; index < 4096u; ++index )
		if( zeroed[index] != 0 )
		{
			result = -2;
			goto cleanup;
		}
	memset( cpu, 0x5a, 257u );
	cpu = realloc( cpu, 1024u * 1024u );
	if( !cpu )
	{
		result = -3;
		goto cleanup;
	}
	for( size_t index = 0; index < 257u; ++index )
		if( cpu[index] != 0x5a )
		{
			result = -4;
			goto cleanup;
		}
	PS5_MemStats( &before, &root );
	if( !root.initialized || !root.mapped || !root.allocated ||
		root.reserve_calls != 1u || root.allocate_calls != 1u ||
		root.map_calls != 1u )
	{
		result = -5;
		goto cleanup;
	}

	for( size_t index = 0; index < sizeof( resources ) / sizeof( resources[0] );
		++index )
	{
		Ps5MemoryGateResource *resource = &resources[index];
		if( PS5_MemGpuAllocate( resource->bytes, resource->alignment,
			&resource->allocation ) != PS5_MEMORY_OK ||
			!resource->allocation.pointer ||
			((uintptr_t)resource->allocation.pointer &
			 ( resource->alignment - 1u )))
		{
			result = -10 - (int)index;
			goto cleanup;
		}
		allocated++;
		resource->state = 1;
		const uint64_t hash = fill_and_hash( resource->allocation.pointer,
			resource->bytes, resource->seed );
		combined_hash ^= hash;
		combined_hash *= UINT64_C( 1099511628211 );
		(void)ps5log_printf( PS5LOG_MARK,
			"XASH_MEMORY_RESOURCE kind=%s bytes=%zu alignment=%zu "
			"generation=%u hash=%016llx owner=gpu-active",
			resource->kind, resource->bytes, resource->alignment,
			resource->allocation.generation, (unsigned long long)hash );
	}
	if( PS5_MemValidate( ) != PS5_MEMORY_OK )
	{
		result = -20;
		goto cleanup;
	}
	for( size_t index = 0; index < allocated; ++index )
		if( PS5_MemGpuRetire( &resources[index].allocation,
			retire_token ) != PS5_MEMORY_OK )
		{
			result = -30 - (int)index;
			goto cleanup;
		}
		else
			resources[index].state = 2;
	for( size_t index = 0; index < allocated; ++index )
		if( PS5_MemGpuReclaim( &resources[index].allocation,
			retire_token, 1 ) != PS5_MEMORY_OK )
		{
			result = -40 - (int)index;
			goto cleanup;
		}
		else
			resources[index].state = 0;
	allocated = 0;

cleanup:
	while( allocated )
	{
		--allocated;
		if( resources[allocated].state == 2 )
			(void)PS5_MemGpuReclaim( &resources[allocated].allocation,
				retire_token, 1 );
		else if( resources[allocated].state == 1 )
			(void)PS5_MemGpuReleaseUnsubmitted(
				&resources[allocated].allocation );
		resources[allocated].state = 0;
	}
	free( zeroed );
	free( cpu );
	PS5_MemStats( &after, &root );
	if( result == 0 && ( PS5_MemValidate( ) != PS5_MEMORY_OK ||
		after.live_bytes != 0u || after.live_cpu != 0u ||
		after.live_gpu != 0u || after.retiring_gpu != 0u ||
		after.guard_failures != before.guard_failures ||
		after.failures != before.failures ||
		after.retire_calls != before.retire_calls + 4u ||
		after.reclaim_calls != before.reclaim_calls + 4u ))
		result = -50;
	(void)ps5log_printf( result == 0 ? PS5LOG_MARK : PS5LOG_ERR,
		"XASH_MEMORY_COMPLETE schema=1 result=%d resources=4 "
		"resource_bytes=%u hash=%016llx retire_token=%016llx "
		"completion=synthetic-contract live_bytes=%zu live_cpu=%u "
		"live_gpu=%u retiring_gpu=%u peak_bytes=%zu guards=%s "
		"alloc_failures=%llu root_calls=%u/%u/%u pass=%d",
		result, 18u * 1024u * 1024u, (unsigned long long)combined_hash,
		(unsigned long long)retire_token, after.live_bytes,
		after.live_cpu, after.live_gpu, after.retiring_gpu,
		after.peak_bytes, after.guard_failures == 0 ? "intact" : "failed",
		(unsigned long long)after.failures, root.reserve_calls,
		root.allocate_calls, root.map_calls, result == 0 );
	return result;
}
