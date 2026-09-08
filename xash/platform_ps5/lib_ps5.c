/*
lib_ps5.c - Xash3D static-library and application-owned PRX backend
Copyright (C) 2026 Manuel Pereira
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "platform/platform.h"
#include "common.h"
#include "library.h"
#include "lib_ps5.h"
#include "prx_loader_ps5.h"
#include "ps5log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if XASH_LIB == LIB_STATIC
typedef struct table_s
{
	const char *name;
	void *pointer;
} table_t;

#include "generated_library_tables.h"

typedef struct ps5_dynamic_library_s
{
	struct ps5_dynamic_library_s *next;
	ps5_prx_module_t module;
	int lifecycle_started;
	int module_start_result;
	int module_stop_result;
} ps5_dynamic_library_t;

static ps5_dynamic_library_t *ps5_dynamic_libraries;
static int ps5_last_result;

static int PS5_IsFilesystemPrx( const ps5_dynamic_library_t *library )
{
	return library && !strcmp( library->module.name, "filesystem_stdio.prx" );
}

static void PS5_LogFilesystemPrxState( ps5_dynamic_library_t *library,
	const char *marker )
{
	int ( *index_count )( void );
	int ( *allocator_result )( void );
	int ( *listing_refused )( void );
	if( !PS5_IsFilesystemPrx( library )) return;
	index_count = (int ( * )( void ))PS5_PrxGetProc( &library->module,
		"PS5_FilesystemPrxIndexCount" );
	allocator_result = (int ( * )( void ))PS5_PrxGetProc( &library->module,
		"PS5_FilesystemPrxAllocatorContractResult" );
	listing_refused = (int ( * )( void ))PS5_PrxGetProc( &library->module,
		"PS5_FilesystemPrxListingRefusedCount" );
	(void)ps5log_printf( index_count && allocator_result && listing_refused ?
		PS5LOG_MARK : PS5LOG_ERR,
		"%s module=filesystem_stdio.prx index_entries=%d "
		"allocator_contract=libc-shared allocator_result=%d "
		"listing_refused=%d resolver=PRXDESC1",
		marker, index_count ? index_count( ) : -1,
		allocator_result ? allocator_result( ) : -1,
		listing_refused ? listing_refused( ) : -1 );
}

static int PS5_DynamicStart( ps5_dynamic_library_t *library )
{
	int ( *start )( size_t, const void * );
	int ( *stop )( size_t, const void * );
	start = (int ( * )( size_t, const void * ))PS5_PrxGetProc(
		&library->module, "module_start" );
	stop = (int ( * )( size_t, const void * ))PS5_PrxGetProc(
		&library->module, "module_stop" );
	if( !start || !stop )
	{
		library->module_start_result = PS5_PRX_ERROR_NO_DESCRIPTOR;
		return library->module_start_result;
	}
	/* FW 12.02 returns a loaded module from sceKernelLoadStartModule without
	 * calling this application-owned entry. COM_* therefore owns lifecycle. */
	library->lifecycle_started = 1;
	library->module_start_result = start( 0, NULL );
	return library->module_start_result;
}

static int PS5_DynamicStop( ps5_dynamic_library_t *library )
{
	int ( *stop )( size_t, const void * );
	if( !library->lifecycle_started ) return 0;
	stop = (int ( * )( size_t, const void * ))PS5_PrxGetProc(
		&library->module, "module_stop" );
	if( !stop )
	{
		library->module_stop_result = PS5_PRX_ERROR_NO_DESCRIPTOR;
		return library->module_stop_result;
	}
	library->module_stop_result = stop( 0, NULL );
	if( library->module_stop_result == 0 ) library->lifecycle_started = 0;
	return library->module_stop_result;
}

static void *PS5_StaticFind( table_t *table, const char *name )
{
	if( !table || !name ) return NULL;
	while( table->name )
	{
		if( !Q_strcmp( table->name, name )) return table->pointer;
		++table;
	}
	return NULL;
}

static int PS5_IsStaticHandle( const void *handle )
{
	table_t *entry = (table_t *)libs;
	while( entry->name )
	{
		if( entry->pointer == handle ) return 1;
		++entry;
	}
	return 0;
}

static ps5_dynamic_library_t *PS5_FindDynamic( const void *handle,
	ps5_dynamic_library_t ***link )
{
	ps5_dynamic_library_t **cursor = &ps5_dynamic_libraries;
	while( *cursor )
	{
		if( *cursor == handle )
		{
			if( link ) *link = cursor;
			return *cursor;
		}
		cursor = &(*cursor)->next;
	}
	return NULL;
}

static int PS5_PrxPath( const char *dllname, char *path, size_t capacity )
{
	const char *base, *cursor, *dot;
	size_t length, i;
	if( !dllname || !*dllname || !path || capacity == 0 ) return 0;
	if( !strncmp( dllname, "/app0/sce_module/", 17 ) &&
		strlen( dllname ) > 21 && !strcmp( dllname + strlen( dllname ) - 4, ".prx" ))
		return snprintf( path, capacity, "%s", dllname ) > 0 && strlen( dllname ) < capacity;
	base = dllname;
	for( cursor = dllname; *cursor; ++cursor )
		if( *cursor == '/' || *cursor == '\\' ) base = cursor + 1;
	dot = strrchr( base, '.' );
	length = dot ? (size_t)( dot - base ) : strlen( base );
	if( length == 0 || length > 96 ) return 0;
	for( i = 0; i < length; ++i )
		if( !( isalnum((unsigned char)base[i] ) || base[i] == '_' || base[i] == '-' ))
			return 0;
	return snprintf( path, capacity, "/app0/sce_module/%.*s.prx",
		(int)length, base ) > 0 && 17u + length + 4u < capacity;
}

qboolean COM_CheckLibraryDirectDependency( const char *name,
	const char *depname, qboolean directpath )
{
	(void)name; (void)depname; (void)directpath;
	return true;
}

void *COM_LoadLibrary( const char *dllname, int build_ordinals_table,
	qboolean directpath )
{
	ps5_dynamic_library_t *library;
	void *static_handle;
	char path[160];
	char error[256];
	int result;
	(void)build_ordinals_table; (void)directpath;
	COM_ResetLibraryError( );
	static_handle = PS5_StaticFind((table_t *)libs, dllname );
	if( static_handle ) return static_handle;
	if( !PS5_PrxPath( dllname, path, sizeof( path )))
	{
		COM_PushLibraryError( "Invalid PS5 PRX module name" );
		return NULL;
	}
	library = calloc( 1, sizeof( *library ));
	if( !library )
	{
		COM_PushLibraryError( "Out of memory allocating PS5 PRX handle" );
		return NULL;
	}
	result = PS5_PrxLoad( &library->module, path, PS5_PrxNativeOps( ));
	ps5_last_result = result;
	if( result != PS5_PRX_OK )
	{
		const int retained = library->module.handle > 0;
		snprintf( error, sizeof( error ), "Failed to load %s: %s (%d)",
			path, PS5_PrxResultString( result ), result );
		COM_PushLibraryError( error );
		(void)ps5log_printf( PS5LOG_ERR,
			"XASH_PRX_LOAD path=%s result=%d reason=%s loaded_handle=0x%x "
			"start_result=%d module_info_rc=0x%x info_size=0x%llx segment_count=%u "
			"load_error=%d rollback_rc=0x%x rollback_stop_result=%d rollback=%s",
			path, result, PS5_PrxResultString( result ),
			(unsigned)library->module.loaded_handle, library->module.start_result,
			(unsigned)library->module.module_info_result,
			(unsigned long long)library->module.module_info_size,
			library->module.module_info_segment_count,
			library->module.load_error,
			(unsigned)library->module.rollback_result,
			library->module.rollback_stop_result,
			retained ? "retained" : "complete" );
		if( retained )
		{
			library->next = ps5_dynamic_libraries;
			ps5_dynamic_libraries = library;
		}
		else free( library );
		return NULL;
	}
	result = PS5_DynamicStart( library );
	if( result != 0 )
	{
		const int stop_result = PS5_DynamicStop( library );
		const int unload_result = stop_result == 0 ?
			PS5_PrxUnload( &library->module, PS5_PrxNativeOps( )) :
			PS5_PRX_ERROR_UNLOAD;
		const int retained = library->module.handle > 0;
		ps5_last_result = PS5_PRX_ERROR_START;
		snprintf( error, sizeof( error ),
			"Failed to initialize %s: module_start returned %d", path, result );
		COM_PushLibraryError( error );
		(void)ps5log_printf( PS5LOG_ERR,
			"XASH_PRX_INIT module=%s start_result=%d stop_result=%d "
			"unload_result=%d ownership=%s",
			library->module.name, result, stop_result, unload_result,
			retained ? "retained" : "released" );
		if( retained )
		{
			library->next = ps5_dynamic_libraries;
			ps5_dynamic_libraries = library;
		}
		else free( library );
		return NULL;
	}
	library->next = ps5_dynamic_libraries;
	ps5_dynamic_libraries = library;
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_PRX_LOAD path=%s module=%s handle=0x%x segments=%u exports=%u "
		"init_result=%d result=0",
		path, library->module.name, (unsigned)library->module.handle,
		library->module.segment_count, library->module.descriptor->header.count,
		library->module_start_result );
	PS5_LogFilesystemPrxState( library, "XASH_FS_PRX_READY" );
	return library;
}

void COM_FreeLibrary( void *hInstance )
{
	ps5_dynamic_library_t **link = NULL;
	ps5_dynamic_library_t *library = PS5_FindDynamic( hInstance, &link );
	int result;
	if( !library ) return;
	PS5_LogFilesystemPrxState( library, "XASH_FS_PRX_STATE" );
	if( PS5_DynamicStop( library ) != 0 )
	{
		ps5_last_result = PS5_PRX_ERROR_UNLOAD;
		(void)ps5log_printf( PS5LOG_ERR,
			"XASH_PRX_UNLOAD module=%s result=%d reason=module_stop_failed "
			"stop_result=%d ownership=retained",
			library->module.name, PS5_PRX_ERROR_UNLOAD,
			library->module_stop_result );
		return;
	}
	result = PS5_PrxUnload( &library->module, PS5_PrxNativeOps( ));
	ps5_last_result = result;
	(void)ps5log_printf( result == PS5_PRX_OK ? PS5LOG_MARK : PS5LOG_ERR,
		"XASH_PRX_UNLOAD module=%s result=%d reason=%s stop_result=%d ownership=%s",
		library->module.name, result, PS5_PrxResultString( result ),
		library->module_stop_result,
		result == PS5_PRX_OK ? "released" : "retained" );
	if( result == PS5_PRX_OK )
	{
		const int filesystem = PS5_IsFilesystemPrx( library );
		*link = library->next;
		free( library );
		if( filesystem )
			(void)ps5log_printf( PS5LOG_MARK,
				"XASH_FS_PRX_COMPLETE module=filesystem_stdio.prx stop_result=0 "
				"active_modules=%u ownership=exact",
				PS5_PrxLibraryActiveCount( ));
	}
}

void *COM_GetProcAddress( void *hInstance, const char *name )
{
	ps5_dynamic_library_t *library = PS5_FindDynamic( hInstance, NULL );
	if( library ) return (void *)PS5_PrxGetProc( &library->module, name );
	if( PS5_IsStaticHandle( hInstance ))
		return PS5_StaticFind((table_t *)hInstance, name );
	return NULL;
}

void *COM_FunctionFromName( void *hInstance, const char *name )
{
	return COM_GetProcAddress( hInstance, name );
}

const char *COM_NameForFunction( void *hInstance, void *function )
{
	ps5_dynamic_library_t *library = PS5_FindDynamic( hInstance, NULL );
	if( library ) return PS5_PrxNameForAddress( &library->module, function );
#ifdef XASH_ALLOW_SAVERESTORE_OFFSETS
	return COM_OffsetNameForFunction( function );
#else
	return NULL;
#endif
}

unsigned PS5_PrxLibraryActiveCount( void )
{
	unsigned count = 0;
	ps5_dynamic_library_t *library;
	for( library = ps5_dynamic_libraries; library; library = library->next ) ++count;
	return count;
}

int PS5_PrxLibraryLastResult( void )
{
	return ps5_last_result;
}

unsigned PS5_PrxLibraryShutdown( void )
{
	ps5_dynamic_library_t **link = &ps5_dynamic_libraries;
	while( *link )
	{
		ps5_dynamic_library_t *library = *link;
		int result;
		if( PS5_DynamicStop( library ) != 0 )
		{
			ps5_last_result = PS5_PRX_ERROR_UNLOAD;
			(void)ps5log_printf( PS5LOG_ERR,
				"XASH_PRX_SHUTDOWN module=%s result=%d reason=module_stop_failed "
				"stop_result=%d ownership=retained",
				library->module.name[0] ? library->module.name : "unvalidated",
				PS5_PRX_ERROR_UNLOAD, library->module_stop_result );
			link = &library->next;
			continue;
		}
		result = PS5_PrxUnload( &library->module, PS5_PrxNativeOps( ));
		ps5_last_result = result;
		(void)ps5log_printf( result == PS5_PRX_OK ? PS5LOG_MARK : PS5LOG_ERR,
			"XASH_PRX_SHUTDOWN module=%s result=%d reason=%s ownership=%s",
			library->module.name[0] ? library->module.name : "unvalidated",
			result, PS5_PrxResultString( result ),
			result == PS5_PRX_OK ? "released" : "retained" );
		if( result != PS5_PRX_OK )
		{
			link = &library->next;
			continue;
		}
		*link = library->next;
		free( library );
	}
	return PS5_PrxLibraryActiveCount( );
}
#endif
