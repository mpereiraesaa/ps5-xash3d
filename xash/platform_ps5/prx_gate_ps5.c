/* Phase 6 loader gate through Xash3D's COM_* API. GPL-3.0-or-later. */
#include "common.h"
#include "library.h"
#include "lib_ps5.h"
#include "ps5log.h"

#include <stdint.h>
#include <string.h>

int PS5_PrxGateRun( void )
{
	void *library;
	int (*add)( int, int );
	uint32_t (*sleep_count)( unsigned int );
	int (*module_start)( size_t, const void * );
	const uint32_t *version;
	const uint32_t *started;
	void *missing;
	int add_result;
	uint32_t call_count;
	uint32_t auto_started;
	int manual_start_result = 0;
	int pass;

	(void)ps5log_line( PS5LOG_MARK,
		"XASH_PRX_BEGIN schema=1 backend=COM_LoadLibrary resolver=PRXDESC1 module=xash_prx_probe.prx" );
	library = COM_LoadLibrary( "/app0/sce_module/xash_prx_probe.prx", 0, 1 );
	if( !library )
	{
		(void)ps5log_printf( PS5LOG_ERR,
			"XASH_PRX_COMPLETE pass=0 stage=load error=%s active=%u",
			COM_GetLibraryError( ), PS5_PrxLibraryActiveCount( ));
		return 1;
	}
	add = (int (*)( int, int ))COM_GetProcAddress( library, "xash_prx_probe_add" );
	sleep_count = (uint32_t (*)( unsigned int ))COM_GetProcAddress( library,
		"xash_prx_probe_sleep_count" );
	module_start = (int (*)( size_t, const void * ))COM_GetProcAddress( library,
		"module_start" );
	version = (const uint32_t *)COM_GetProcAddress( library, "xash_prx_probe_version" );
	started = (const uint32_t *)COM_GetProcAddress( library, "xash_prx_probe_started" );
	missing = COM_GetProcAddress( library, "xash_prx_probe_missing" );
	pass = add && sleep_count && module_start && version && started && !missing;
	(void)ps5log_printf( pass ? PS5LOG_MARK : PS5LOG_ERR,
		"XASH_PRX_RESOLVE add=%d sleep_count=%d module_start=%d version=%d started=%d missing=%d pass=%d",
		add != NULL, sleep_count != NULL, module_start != NULL,
		version != NULL, started != NULL,
		missing != NULL, pass );
	if( !pass )
	{
		COM_FreeLibrary( library );
		return 2;
	}
	add_result = add( 19, 23 );
	call_count = sleep_count( 1000 );
	auto_started = *started;
	if( !auto_started ) manual_start_result = module_start( 0, NULL );
	pass = add_result == 42 && call_count == 2 && *version == 0x00010000u &&
		manual_start_result == 0 && *started == 1u &&
		COM_NameForFunction( library, (void *)add ) &&
		strcmp( COM_NameForFunction( library, (void *)add ), "xash_prx_probe_add" ) == 0;
	(void)ps5log_printf( pass ? PS5LOG_MARK : PS5LOG_ERR,
		"XASH_PRX_CALL add=%d count=%u version=0x%x auto_started=%u manual_start_rc=%d "
		"started=%u kernel_import=sceKernelUsleep name_roundtrip=%d pass=%d",
		add_result, call_count, (unsigned)*version, (unsigned)auto_started,
		manual_start_result, (unsigned)*started,
		COM_NameForFunction( library, (void *)add ) != NULL, pass );
	COM_FreeLibrary( library );
	pass = pass && PS5_PrxLibraryLastResult( ) == 0 &&
		PS5_PrxLibraryActiveCount( ) == 0;
	(void)ps5log_printf( pass ? PS5LOG_MARK : PS5LOG_ERR,
		"XASH_PRX_COMPLETE pass=%d load=1 resolve=1 call=1 unload=%d active=%u ownership=exact",
		pass, PS5_PrxLibraryLastResult( ) == 0,
		PS5_PrxLibraryActiveCount( ));
	return pass ? 0 : 3;
}
