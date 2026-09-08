/*
prx_loader_ps5.c - Validated PRX descriptor scanner and loader lifecycle
Copyright (C) 2026 BlackBearReloaded
Copyright (C) 2026 Manuel Pereira
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "prx_loader_ps5.h"

#include <limits.h>
#include <string.h>

static int PS5_PrxContains( const ps5_prx_segment_t *segments,
	uint32_t segment_count, const void *pointer, size_t bytes )
{
	const uintptr_t address = (uintptr_t)pointer;
	uint32_t i;
	if( !segments || !pointer || bytes == 0 || address > UINTPTR_MAX - ( bytes - 1u ))
		return 0;
	for( i = 0; i < segment_count; ++i )
	{
		const uintptr_t base = (uintptr_t)segments[i].address;
		const size_t size = segments[i].size;
		if( !base || !size || base > UINTPTR_MAX - ( size - 1u ))
			continue;
		if( address >= base && bytes <= size && address - base <= size - bytes )
			return 1;
	}
	return 0;
}

static int PS5_PrxNameInside( const ps5_prx_segment_t *segments,
	uint32_t segment_count, const char *name )
{
	size_t length = 0;
	if( !PS5_PrxContains( segments, segment_count, name, 1 ))
		return 0;
	while( length <= 256u &&
		PS5_PrxContains( segments, segment_count, name + length, 1 ))
	{
		if( name[length] == '\0' )
			return length != 0;
		++length;
	}
	return 0;
}

static int PS5_PrxDescriptorValid( const ps5_prx_descriptor_t *descriptor,
	const ps5_prx_segment_t *segments, uint32_t segment_count )
{
	size_t bytes;
	uint32_t i, j;
	if( descriptor->header.magic != PS5_PRX_DESCRIPTOR_MAGIC ||
		descriptor->header.version != PS5_PRX_DESCRIPTOR_VERSION ||
		descriptor->header.count == 0 || descriptor->header.count > PS5_PRX_MAX_EXPORTS )
		return 0;
	bytes = sizeof( ps5_prx_descriptor_header_t ) +
		(size_t)descriptor->header.count * sizeof( ps5_prx_export_t );
	if( !PS5_PrxContains( segments, segment_count, descriptor, bytes ))
		return 0;
	for( i = 0; i < descriptor->header.count; ++i )
	{
		const ps5_prx_export_t *entry = &descriptor->exports[i];
		if( !PS5_PrxNameInside( segments, segment_count, entry->name ) ||
			!PS5_PrxContains( segments, segment_count, entry->address, 1 ))
			return 0;
		for( j = 0; j < i; ++j )
			if( strcmp( descriptor->exports[j].name, entry->name ) == 0 )
				return 0;
	}
	return 1;
}

int PS5_PrxParseModuleInfo( const void *info, char *name, size_t name_capacity,
	ps5_prx_segment_t *segments, uint32_t *segment_count )
{
	const uint8_t *raw = (const uint8_t *)info;
	uint64_t size;
	uint32_t count, i;
	const void *terminator;
	if( !info || !segments || !segment_count )
		return PS5_PRX_ERROR_ARGUMENT;
	memcpy( &size, raw, sizeof( size ));
	memcpy( &count, raw + 0x148, sizeof( count ));
	terminator = memchr( raw + 8, '\0', 256 );
	/* FW 12.02 clears the input size word on success (confirmed by the loader
	 * gate); host fakes and some SDK revisions preserve 0x160. Both shapes
	 * carry the same measured offsets below. */
	if(( size != 0 && size != PS5_PRX_MODULE_INFO_BYTES ) || count == 0 ||
		count > PS5_PRX_MAX_SEGMENTS || !terminator )
		return PS5_PRX_ERROR_MODULE_INFO;
	if( name && name_capacity )
	{
		size_t length = (const uint8_t *)terminator - ( raw + 8 );
		if( length >= name_capacity ) length = name_capacity - 1;
		memcpy( name, raw + 8, length );
		name[length] = '\0';
	}
	for( i = 0; i < count; ++i )
	{
		const uint8_t *segment = raw + 0x108 + i * 16;
		uintptr_t address;
		memcpy( &address, segment, sizeof( address ));
		memcpy( &segments[i].size, segment + 8, sizeof( segments[i].size ));
		memcpy( &segments[i].protection, segment + 12,
			sizeof( segments[i].protection ));
		segments[i].address = (const void *)address;
		if( !address || !segments[i].size ||
			address > UINTPTR_MAX - ( segments[i].size - 1u ))
			return PS5_PRX_ERROR_MODULE_INFO;
	}
	*segment_count = count;
	return PS5_PRX_OK;
}

int PS5_PrxFindDescriptor( const ps5_prx_segment_t *segments,
	uint32_t segment_count, const ps5_prx_descriptor_t **descriptor )
{
	uint32_t i;
	if( !segments || !descriptor || segment_count == 0 ||
		segment_count > PS5_PRX_MAX_SEGMENTS )
		return PS5_PRX_ERROR_ARGUMENT;
	*descriptor = NULL;
	for( i = 0; i < segment_count; ++i )
	{
		const uintptr_t base = (uintptr_t)segments[i].address;
		uintptr_t candidate;
		uintptr_t end;
		if( !base || !( segments[i].protection & 1u ) ||
			segments[i].size < sizeof( ps5_prx_descriptor_header_t ) ||
			base > UINTPTR_MAX - segments[i].size )
			continue;
		end = base + segments[i].size;
		candidate = ( base + PS5_PRX_DESCRIPTOR_ALIGNMENT - 1u ) &
			~(uintptr_t)( PS5_PRX_DESCRIPTOR_ALIGNMENT - 1u );
		for( ; candidate <= end - sizeof( ps5_prx_descriptor_header_t );
			candidate += PS5_PRX_DESCRIPTOR_ALIGNMENT )
		{
			uint64_t magic;
			memcpy( &magic, (const void *)candidate, sizeof( magic ));
			if( magic == PS5_PRX_DESCRIPTOR_MAGIC &&
				PS5_PrxDescriptorValid((const ps5_prx_descriptor_t *)candidate,
					segments, segment_count ))
			{
				*descriptor = (const ps5_prx_descriptor_t *)candidate;
				return PS5_PRX_OK;
			}
		}
	}
	return PS5_PRX_ERROR_NO_DESCRIPTOR;
}

const void *PS5_PrxDescriptorLookup( const ps5_prx_descriptor_t *descriptor,
	const char *name )
{
	uint32_t i;
	if( !descriptor || !name ) return NULL;
	for( i = 0; i < descriptor->header.count; ++i )
		if( strcmp( descriptor->exports[i].name, name ) == 0 )
			return descriptor->exports[i].address;
	return NULL;
}

int PS5_PrxLoad( ps5_prx_module_t *module, const char *path,
	const ps5_prx_loader_ops_t *ops )
{
	/* Match the guard space used by the hardware-proven spike. The firmware is
	 * asked for 0x160 bytes, but the caller owns 64 extra bytes after it. */
	uint64_t info[PS5_PRX_MODULE_INFO_BYTES / 8u + 8u];
	int result;
	if( !module || !path || !*path || !ops || !ops->load_start ||
		!ops->module_info || !ops->stop_unload )
		return PS5_PRX_ERROR_ARGUMENT;
	memset( module, 0, sizeof( *module ));
	module->handle = ops->load_start( path, 0, NULL, 0, NULL,
		&module->start_result );
	module->loaded_handle = module->handle;
	if( module->handle <= 0 ) return PS5_PRX_ERROR_LOAD;
	result = module->start_result == 0 ? PS5_PRX_OK : PS5_PRX_ERROR_START;
	memset( info, 0, sizeof( info ));
	info[0] = PS5_PRX_MODULE_INFO_BYTES;
	if( result == PS5_PRX_OK )
		module->module_info_result = ops->module_info( module->handle, info );
	memcpy( &module->module_info_size, info, sizeof( module->module_info_size ));
	memcpy( &module->module_info_segment_count, (const uint8_t *)info + 0x148,
		sizeof( module->module_info_segment_count ));
	if( result == PS5_PRX_OK && module->module_info_result != 0 )
		result = PS5_PRX_ERROR_MODULE_INFO;
	if( result == PS5_PRX_OK )
		result = PS5_PrxParseModuleInfo( info, module->name,
			sizeof( module->name ), module->segments, &module->segment_count );
	if( result == PS5_PRX_OK )
		result = PS5_PrxFindDescriptor( module->segments,
			module->segment_count, &module->descriptor );
	if( result != PS5_PRX_OK )
	{
		module->load_error = result;
		module->rollback_result = ops->stop_unload( module->handle, 0, NULL,
			0, NULL, &module->rollback_stop_result );
		module->descriptor = NULL;
		if( module->rollback_result == 0 && module->rollback_stop_result == 0 )
			module->handle = 0;
		else
			result = PS5_PRX_ERROR_ROLLBACK;
	}
	return result;
}

const void *PS5_PrxGetProc( const ps5_prx_module_t *module, const char *name )
{
	if( !module || module->handle <= 0 ) return NULL;
	return PS5_PrxDescriptorLookup( module->descriptor, name );
}

const char *PS5_PrxNameForAddress( const ps5_prx_module_t *module,
	const void *address )
{
	uint32_t i;
	if( !module || module->handle <= 0 || !module->descriptor || !address )
		return NULL;
	for( i = 0; i < module->descriptor->header.count; ++i )
		if( module->descriptor->exports[i].address == address )
			return module->descriptor->exports[i].name;
	return NULL;
}

int PS5_PrxUnload( ps5_prx_module_t *module,
	const ps5_prx_loader_ops_t *ops )
{
	int rc;
	if( !module || !ops || !ops->stop_unload )
		return PS5_PRX_ERROR_ARGUMENT;
	if( module->handle <= 0 ) return PS5_PRX_OK;
	rc = ops->stop_unload( module->handle, 0, NULL, 0, NULL,
		&module->stop_result );
	if( rc != 0 || module->stop_result != 0 ) return PS5_PRX_ERROR_UNLOAD;
	module->handle = 0;
	module->descriptor = NULL;
	return PS5_PRX_OK;
}

const char *PS5_PrxResultString( int result )
{
	switch( result )
	{
	case PS5_PRX_OK: return "ok";
	case PS5_PRX_ERROR_ARGUMENT: return "invalid argument";
	case PS5_PRX_ERROR_LOAD: return "load/start syscall failed";
	case PS5_PRX_ERROR_START: return "module start routine failed";
	case PS5_PRX_ERROR_MODULE_INFO: return "invalid module info";
	case PS5_PRX_ERROR_NO_DESCRIPTOR: return "PRXDESC1 not found or invalid";
	case PS5_PRX_ERROR_UNLOAD: return "stop/unload failed";
	case PS5_PRX_ERROR_ROLLBACK: return "load failed and rollback retained ownership";
	default: return "unknown PRX loader error";
	}
}

#if defined(XASH_PS5)
int32_t sceKernelLoadStartModule( const char *path, size_t argc,
	const void *argv, uint32_t flags, const void *option, int *result );
int sceKernelGetModuleInfo( int32_t handle, void *info );
int sceKernelStopUnloadModule( int32_t handle, size_t argc, const void *argv,
	uint32_t flags, const void *option, int *result );

const ps5_prx_loader_ops_t *PS5_PrxNativeOps( void )
{
	static const ps5_prx_loader_ops_t ops = {
		sceKernelLoadStartModule, sceKernelGetModuleInfo,
		sceKernelStopUnloadModule
	};
	return &ops;
}
#endif
