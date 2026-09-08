/*
lib_ps5.c - Xash3D static-library and application-owned PRX backend
Copyright (C) 2026 Manuel Pereira
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "platform/platform.h"
#include "common.h"
#include "cdll_int.h"
#include "library.h"
#include "menu_int.h"
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
static void ( *ps5_server_give_fnptrs )( void *, void * );
static MENUAPI ps5_menu_get_api;
static UIEXTENEDEDAPI ps5_menu_get_ext_api;
static void ( *ps5_menu_init )( void );
static void ( *ps5_menu_shutdown )( void );
static void ( *ps5_menu_redraw )( float );
static void ( *ps5_menu_set_active )( int );
static int ( *ps5_menu_is_visible )( void );
static unsigned ps5_menu_init_calls;
static unsigned ps5_menu_shutdown_calls;
static unsigned ps5_menu_redraw_calls;
static unsigned ps5_menu_active_calls;
static int ps5_menu_api_pass;
static int ps5_menu_ext_api_pass;
static int ( *ps5_client_initialize )( cl_enginefunc_t *, int );
static void ( *ps5_client_init )( void );
static int ( *ps5_client_vid_init )( void );
static void ( *ps5_client_frame )( double );
static int ( *ps5_client_redraw )( float, int );
static void ( *ps5_client_shutdown )( void );
static unsigned ps5_client_initialize_calls;
static unsigned ps5_client_init_calls;
static unsigned ps5_client_vid_init_calls;
static unsigned ps5_client_frame_calls;
static unsigned ps5_client_redraw_calls;
static unsigned ps5_client_shutdown_calls;
static int ps5_client_api_pass;
static int ps5_client_abi_pass;

static unsigned PS5_MenuFunctionCount( const UI_FUNCTIONS *functions )
{
	unsigned count = 0;
	if( !functions ) return 0;
	count += functions->pfnVidInit != NULL;
	count += functions->pfnInit != NULL;
	count += functions->pfnShutdown != NULL;
	count += functions->pfnRedraw != NULL;
	count += functions->pfnKeyEvent != NULL;
	count += functions->pfnMouseMove != NULL;
	count += functions->pfnSetActiveMenu != NULL;
	count += functions->pfnAddServerToList != NULL;
	count += functions->pfnGetCursorPos != NULL;
	count += functions->pfnSetCursorPos != NULL;
	count += functions->pfnShowCursor != NULL;
	count += functions->pfnCharEvent != NULL;
	count += functions->pfnMouseInRect != NULL;
	count += functions->pfnIsVisible != NULL;
	count += functions->pfnCreditsActive != NULL;
	count += functions->pfnFinalCredits != NULL;
	return count;
}

static unsigned PS5_MenuExtendedFunctionCount( const UI_EXTENDED_FUNCTIONS *functions )
{
	unsigned count = 0;
	if( !functions ) return 0;
	count += functions->pfnAddTouchButtonToList != NULL;
	count += functions->pfnResetPing != NULL;
	count += functions->pfnShowConnectionWarning != NULL;
	count += functions->pfnShowUpdateDialog != NULL;
	count += functions->pfnShowMessageBox != NULL;
	count += functions->pfnConnectionProgress_Disconnect != NULL;
	count += functions->pfnConnectionProgress_Download != NULL;
	count += functions->pfnConnectionProgress_DownloadEnd != NULL;
	count += functions->pfnConnectionProgress_Precache != NULL;
	count += functions->pfnConnectionProgress_Connect != NULL;
	count += functions->pfnConnectionProgress_ChangeLevel != NULL;
	count += functions->pfnConnectionProgress_ParseServerInfo != NULL;
	return count;
}

static void PS5_MenuInitTrampoline( void )
{
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_MENU_PRX_INIT phase=begin call=%u", ps5_menu_init_calls + 1u );
	if( ps5_menu_init ) ps5_menu_init( );
	ps5_menu_init_calls++;
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_MENU_PRX_INIT phase=complete call=%u active_modules=%u",
		ps5_menu_init_calls, PS5_PrxLibraryActiveCount( ));
}

static void PS5_MenuShutdownTrampoline( void )
{
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_MENU_PRX_SHUTDOWN phase=begin call=%u redraw_calls=%u",
		ps5_menu_shutdown_calls + 1u, ps5_menu_redraw_calls );
	if( ps5_menu_shutdown ) ps5_menu_shutdown( );
	ps5_menu_shutdown_calls++;
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_MENU_PRX_SHUTDOWN phase=complete call=%u redraw_calls=%u",
		ps5_menu_shutdown_calls, ps5_menu_redraw_calls );
}

static void PS5_MenuRedrawTrampoline( float realtime )
{
	int visible;
	if( ps5_menu_redraw ) ps5_menu_redraw( realtime );
	ps5_menu_redraw_calls++;
	visible = ps5_menu_is_visible ? ps5_menu_is_visible( ) : -1;
	if( ps5_menu_redraw_calls == 1u ||
		( visible == 1 && ps5_menu_redraw_calls == 2u ))
		(void)ps5log_printf( visible == 1 ? PS5LOG_MARK : PS5LOG_INFO,
			"XASH_MENU_PRX_REDRAW call=%u visible=%d realtime_ms=%u active_modules=%u",
			ps5_menu_redraw_calls, visible, (unsigned)( realtime * 1000.0f ),
			PS5_PrxLibraryActiveCount( ));
}

static void PS5_MenuSetActiveTrampoline( int active )
{
	if( ps5_menu_set_active ) ps5_menu_set_active( active );
	ps5_menu_active_calls++;
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_MENU_PRX_ACTIVE active=%d call=%u visible=%d",
		active ? 1 : 0, ps5_menu_active_calls,
		ps5_menu_is_visible ? ps5_menu_is_visible( ) : -1 );
}

static int PS5_MenuGetApiTrampoline( UI_FUNCTIONS *functions,
	ui_enginefuncs_t *engine, ui_globalvars_t *globals )
{
	unsigned callbacks, engine_mask = 0;
	int result = ps5_menu_get_api ? ps5_menu_get_api( functions, engine, globals ) : 0;
	callbacks = PS5_MenuFunctionCount( functions );
	if( engine )
	{
		if( engine->pfnPIC_Load ) engine_mask |= 1u;
		if( engine->pfnRegisterVariable ) engine_mask |= 2u;
		if( engine->pfnAddCommand ) engine_mask |= 4u;
		if( engine->COM_LoadFile ) engine_mask |= 8u;
		if( engine->pfnMemAlloc ) engine_mask |= 16u;
		if( engine->pfnGetGameInfo ) engine_mask |= 32u;
	}
	ps5_menu_api_pass = result == 1 && callbacks == 16u && engine_mask == 63u && globals != NULL;
	if( result == 1 && functions )
	{
		ps5_menu_init = functions->pfnInit;
		ps5_menu_shutdown = functions->pfnShutdown;
		ps5_menu_redraw = functions->pfnRedraw;
		ps5_menu_set_active = functions->pfnSetActiveMenu;
		ps5_menu_is_visible = functions->pfnIsVisible;
		functions->pfnInit = PS5_MenuInitTrampoline;
		functions->pfnShutdown = PS5_MenuShutdownTrampoline;
		functions->pfnRedraw = PS5_MenuRedrawTrampoline;
		functions->pfnSetActiveMenu = PS5_MenuSetActiveTrampoline;
	}
	(void)ps5log_printf( ps5_menu_api_pass ? PS5LOG_MARK : PS5LOG_ERR,
		"XASH_MENU_PRX_API result=%d callbacks=%u expected=16 "
		"engine_mask=%u expected_mask=63 globals=%d pass=%d",
		result, callbacks, engine_mask, globals ? 1 : 0, ps5_menu_api_pass );
	return result;
}

static int PS5_MenuGetExtApiTrampoline( int version,
	UI_EXTENDED_FUNCTIONS *functions, ui_extendedfuncs_t *engine )
{
	unsigned callbacks, engine_mask = 0;
	int result = ps5_menu_get_ext_api ? ps5_menu_get_ext_api( version, functions, engine ) : 0;
	callbacks = PS5_MenuExtendedFunctionCount( functions );
	if( engine )
	{
		if( engine->pfnDoubleTime ) engine_mask |= 1u;
		if( engine->pfnGetGameInfo ) engine_mask |= 2u;
		if( engine->pfnIsCvarReadOnly ) engine_mask |= 4u;
		if( engine->pNetAPI ) engine_mask |= 8u;
	}
	ps5_menu_ext_api_pass = result == 1 && version == MENU_EXTENDED_API_VERSION &&
		callbacks == 12u && engine_mask == 15u;
	(void)ps5log_printf( ps5_menu_ext_api_pass ? PS5LOG_MARK : PS5LOG_ERR,
		"XASH_MENU_PRX_EXT_API version=%d result=%d callbacks=%u expected=12 "
		"engine_mask=%u expected_mask=15 pass=%d",
		version, result, callbacks, engine_mask, ps5_menu_ext_api_pass );
	return result;
}

static int PS5_ClientInitializeTrampoline( cl_enginefunc_t *engine, int version )
{
	ps5_dynamic_library_t *library;
	int ( *mask )( void ) = NULL;
	int ( *smoke )( int ) = NULL;
	unsigned engine_mask = 0;
	int module_mask = -1;
	int smoke_pass = 1;
	int result;
	int step;

	result = ps5_client_initialize ? ps5_client_initialize( engine, version ) : 0;
	ps5_client_initialize_calls++;
	if( engine )
	{
		if( engine->pfnGetCvarPointer ) engine_mask |= 1u;
		if( engine->pfnRegisterVariable ) engine_mask |= 2u;
		if( engine->Con_Printf ) engine_mask |= 4u;
		if( engine->pfnAddCommand ) engine_mask |= 8u;
		if( engine->COM_LoadFile ) engine_mask |= 16u;
		if( engine->pfnGetGameDirectory ) engine_mask |= 32u;
	}
	for( library = ps5_dynamic_libraries; library; library = library->next )
		if( !strcmp( library->module.name, "client.prx" ))
		{
			mask = (int ( * )( void ))PS5_PrxGetProc( &library->module,
				"PS5_ClientPrxEngineTableMask" );
			smoke = (int ( * )( int ))PS5_PrxGetProc( &library->module,
				"PS5_ClientPrxEngineTableSmoke" );
			break;
		}
	if( mask ) module_mask = mask( );
	for( step = 1; step <= 2; ++step )
	{
		int smoke_result = smoke ? smoke( step ) : 0;
		if( smoke_result != 1 ) smoke_pass = 0;
		(void)ps5log_printf( smoke_result == 1 ? PS5LOG_MARK : PS5LOG_ERR,
			"XASH_CLIENT_PRX_ABI_SMOKE step=%d result=%d pass=%d",
			step, smoke_result, smoke_result == 1 ? 1 : 0 );
	}
	ps5_client_api_pass = result == 1 && version == CLDLL_INTERFACE_VERSION &&
		engine_mask == 63u;
	ps5_client_abi_pass = module_mask == 15 && smoke_pass;
	(void)ps5log_printf( ps5_client_api_pass && ps5_client_abi_pass ?
		PS5LOG_MARK : PS5LOG_ERR,
		"XASH_CLIENT_PRX_API result=%d version=%d expected_version=%d "
		"engine_mask=%u expected_mask=63 module_mask=%d expected_module_mask=15 "
		"calls=%u pass=%d",
		result, version, CLDLL_INTERFACE_VERSION, engine_mask, module_mask,
		ps5_client_initialize_calls,
		ps5_client_api_pass && ps5_client_abi_pass );
	return result;
}

static void PS5_ClientInitTrampoline( void )
{
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_CLIENT_PRX_INIT phase=begin call=%u", ps5_client_init_calls + 1u );
	if( ps5_client_init ) ps5_client_init( );
	ps5_client_init_calls++;
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_CLIENT_PRX_INIT phase=complete call=%u active_modules=%u",
		ps5_client_init_calls, PS5_PrxLibraryActiveCount( ));
}

static int PS5_ClientVidInitTrampoline( void )
{
	int result = ps5_client_vid_init ? ps5_client_vid_init( ) : 0;
	ps5_client_vid_init_calls++;
	(void)ps5log_printf( result == 1 ? PS5LOG_MARK : PS5LOG_ERR,
		"XASH_CLIENT_PRX_VID_INIT call=%u result=%d pass=%d",
		ps5_client_vid_init_calls, result, result == 1 ? 1 : 0 );
	return result;
}

static void PS5_ClientFrameTrampoline( double time )
{
	if( ps5_client_frame ) ps5_client_frame( time );
	ps5_client_frame_calls++;
	if( ps5_client_frame_calls == 1u )
		(void)ps5log_printf( PS5LOG_MARK,
			"XASH_CLIENT_PRX_FRAME call=1 time_ms=%u active_modules=%u",
			(unsigned)( time * 1000.0 ), PS5_PrxLibraryActiveCount( ));
}

static int PS5_ClientRedrawTrampoline( float time, int intermission )
{
	int result = ps5_client_redraw ? ps5_client_redraw( time, intermission ) : 0;
	ps5_client_redraw_calls++;
	if( ps5_client_redraw_calls == 1u )
		(void)ps5log_printf( PS5LOG_MARK,
			"XASH_CLIENT_PRX_REDRAW call=1 time_ms=%u intermission=%d result=%d",
			(unsigned)( time * 1000.0f ), intermission, result );
	return result;
}

static void PS5_ClientShutdownTrampoline( void )
{
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_CLIENT_PRX_SHUTDOWN phase=begin call=%u frame_calls=%u redraw_calls=%u",
		ps5_client_shutdown_calls + 1u, ps5_client_frame_calls,
		ps5_client_redraw_calls );
	if( ps5_client_shutdown ) ps5_client_shutdown( );
	ps5_client_shutdown_calls++;
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_CLIENT_PRX_SHUTDOWN phase=complete call=%u",
		ps5_client_shutdown_calls );
}

static void PS5_ServerGiveFnptrsTrampoline( void *functions, void *globals )
{
	ps5_dynamic_library_t *library;
	int ( *mask )( void ) = NULL;
	int ( *smoke )( int ) = NULL;
	int engine_table_mask = -1;
	int step;
	if( ps5_server_give_fnptrs ) ps5_server_give_fnptrs( functions, globals );
	for( library = ps5_dynamic_libraries; library; library = library->next )
		if( !strcmp( library->module.name, "server.prx" ))
		{
			mask = (int ( * )( void ))PS5_PrxGetProc( &library->module,
				"PS5_ServerPrxEngineTableMask" );
			smoke = (int ( * )( int ))PS5_PrxGetProc( &library->module,
				"PS5_ServerPrxEngineTableSmoke" );
			break;
		}
	if( mask ) engine_table_mask = mask( );
	(void)ps5log_printf( engine_table_mask == 7 ? PS5LOG_MARK : PS5LOG_ERR,
		"XASH_SERVER_PRX_ABI engine_table_mask=%d expected=7 pass=%d",
		engine_table_mask, engine_table_mask == 7 ? 1 : 0 );
	for( step = 1; smoke && step <= 2; ++step )
	{
		int result;
		(void)ps5log_printf( PS5LOG_MARK,
			"XASH_SERVER_PRX_ABI_SMOKE step=%d phase=begin", step );
		result = smoke( step );
		(void)ps5log_printf( result == 1 ? PS5LOG_MARK : PS5LOG_ERR,
			"XASH_SERVER_PRX_ABI_SMOKE step=%d phase=complete result=%d pass=%d",
			step, result, result == 1 ? 1 : 0 );
	}
}

static int PS5_IsFilesystemPrx( const ps5_dynamic_library_t *library )
{
	return library && !strcmp( library->module.name, "filesystem_stdio.prx" );
}

static int PS5_IsServerPrx( const ps5_dynamic_library_t *library )
{
	return library && !strcmp( library->module.name, "server.prx" );
}

static int PS5_IsMenuPrx( const ps5_dynamic_library_t *library )
{
	return library && !strcmp( library->module.name, "menu.prx" );
}

static int PS5_IsClientPrx( const ps5_dynamic_library_t *library )
{
	return library && !strcmp( library->module.name, "client.prx" );
}

static int PS5_IsRefAgcPrx( const ps5_dynamic_library_t *library )
{
	return library && !strcmp( library->module.name, "ref_agc.prx" );
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

static void PS5_LogServerPrxState( ps5_dynamic_library_t *library,
	const char *marker )
{
	int ( *state )( void );
	int ( *export_count )( void );
	if( !PS5_IsServerPrx( library )) return;
	state = (int ( * )( void ))PS5_PrxGetProc( &library->module,
		"PS5_ServerPrxState" );
	export_count = (int ( * )( void ))PS5_PrxGetProc( &library->module,
		"PS5_ServerPrxExportCount" );
	(void)ps5log_printf( state && export_count && state( ) == 1 ?
		PS5LOG_MARK : PS5LOG_ERR,
		"%s module=server.prx state=%d exports=%d resolver=PRXDESC1",
		marker, state ? state( ) : -1, export_count ? export_count( ) : -1 );
}

static void PS5_LogMenuPrxState( ps5_dynamic_library_t *library,
	const char *marker )
{
	int ( *state )( void );
	int ( *export_count )( void );
	if( !PS5_IsMenuPrx( library )) return;
	state = (int ( * )( void ))PS5_PrxGetProc( &library->module,
		"PS5_MenuPrxState" );
	export_count = (int ( * )( void ))PS5_PrxGetProc( &library->module,
		"PS5_MenuPrxExportCount" );
	(void)ps5log_printf( state && export_count && state( ) == 1 ?
		PS5LOG_MARK : PS5LOG_ERR,
		"%s module=menu.prx state=%d exports=%d resolver=PRXDESC1",
		marker, state ? state( ) : -1, export_count ? export_count( ) : -1 );
}

static void PS5_LogClientPrxState( ps5_dynamic_library_t *library,
	const char *marker )
{
	int ( *state )( void );
	int ( *export_count )( void );
	if( !PS5_IsClientPrx( library )) return;
	state = (int ( * )( void ))PS5_PrxGetProc( &library->module,
		"PS5_ClientPrxState" );
	export_count = (int ( * )( void ))PS5_PrxGetProc( &library->module,
		"PS5_ClientPrxExportCount" );
	(void)ps5log_printf( state && export_count && state( ) == 1 ?
		PS5LOG_MARK : PS5LOG_ERR,
		"%s module=client.prx state=%d exports=%d resolver=PRXDESC1",
		marker, state ? state( ) : -1, export_count ? export_count( ) : -1 );
}

static void PS5_LogRefAgcPrxState( ps5_dynamic_library_t *library,
	const char *marker )
{
	int ( *state )( void );
	int ( *result )( void );
	int ( *teardown )( void );
	int ( *engine_mask )( void );
	uint64_t ( *frames )( void );
	uint64_t ( *frame_hash )( void );
	uint64_t ( *bright )( void );
	uint64_t ( *begin )( void );
	uint64_t ( *scene )( void );
	uint64_t ( *end )( void );
	uint64_t ( *newmap )( void );
	int complete, pass;
	if( !PS5_IsRefAgcPrx( library )) return;
#define REF_AGC_PROC(type, name) ((type)PS5_PrxGetProc( &library->module, name ))
	state = REF_AGC_PROC( int ( * )( void ), "PS5_RefAgcPrxRuntimeState" );
	result = REF_AGC_PROC( int ( * )( void ), "PS5_RefAgcPrxRuntimeResult" );
	teardown = REF_AGC_PROC( int ( * )( void ), "PS5_RefAgcPrxTeardownResult" );
	engine_mask = REF_AGC_PROC( int ( * )( void ), "PS5_RefAgcPrxEngineTableMask" );
	frames = REF_AGC_PROC( uint64_t ( * )( void ), "PS5_RefAgcPrxRuntimeFrames" );
	frame_hash = REF_AGC_PROC( uint64_t ( * )( void ), "PS5_RefAgcPrxFrameHash" );
	bright = REF_AGC_PROC( uint64_t ( * )( void ), "PS5_RefAgcPrxBrightPixels" );
	begin = REF_AGC_PROC( uint64_t ( * )( void ), "PS5_RefAgcPrxBeginCalls" );
	scene = REF_AGC_PROC( uint64_t ( * )( void ), "PS5_RefAgcPrxSceneCalls" );
	end = REF_AGC_PROC( uint64_t ( * )( void ), "PS5_RefAgcPrxEndCalls" );
	newmap = REF_AGC_PROC( uint64_t ( * )( void ), "PS5_RefAgcPrxNewMapCalls" );
#undef REF_AGC_PROC
	complete = marker && !strcmp( marker, "XASH_REF_AGC_PRX_STATE" );
	pass = state && result && teardown && engine_mask && frames && frame_hash &&
		bright && begin && scene && end && newmap;
	if( complete )
		pass = pass && state( ) == 5 && result( ) == 0 && teardown( ) == 0 &&
			engine_mask( ) == 63 && frames( ) == 600u && frame_hash( ) != 0u &&
			bright( ) != 0u && begin( ) > 0u && scene( ) > 0u && end( ) > 0u &&
			newmap( ) > 0u;
	(void)ps5log_printf( pass ? PS5LOG_MARK : PS5LOG_ERR,
		"%s module=ref_agc.prx api=18 state=%d runtime_result=%d "
		"teardown_result=%d engine_mask=%d expected_mask=63 frames=%llu "
		"frame_hash=%016llx bright_pixels=%llu begin_calls=%llu "
		"scene_calls=%llu end_calls=%llu newmap_calls=%llu "
		"backend=phase4-native ownership=fence+videoout pass=%d",
		marker, state ? state( ) : -1, result ? result( ) : -1,
		teardown ? teardown( ) : -1, engine_mask ? engine_mask( ) : -1,
		(unsigned long long)( frames ? frames( ) : 0u ),
		(unsigned long long)( frame_hash ? frame_hash( ) : 0u ),
		(unsigned long long)( bright ? bright( ) : 0u ),
		(unsigned long long)( begin ? begin( ) : 0u ),
		(unsigned long long)( scene ? scene( ) : 0u ),
		(unsigned long long)( end ? end( ) : 0u ),
		(unsigned long long)( newmap ? newmap( ) : 0u ), pass );
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
		const int optional_vgui = !strcmp( path,
			"/app0/sce_module/libvgui_support.prx" );
		snprintf( error, sizeof( error ), "Failed to load %s: %s (%d)",
			path, PS5_PrxResultString( result ), result );
		COM_PushLibraryError( error );
		(void)ps5log_printf( optional_vgui ? PS5LOG_WARN : PS5LOG_ERR,
			"%s path=%s module=%s result=%d reason=%s loaded_handle=0x%x "
			"start_result=%d module_info_rc=0x%x info_size=0x%llx segment_count=%u "
			"load_error=%d rollback_rc=0x%x rollback_stop_result=%d rollback=%s%s",
			optional_vgui ? "XASH_PRX_OPTIONAL_MISS" : "XASH_PRX_LOAD",
			path, library->module.name[0] ? library->module.name : "libvgui_support.prx",
			result, optional_vgui ? "optional-prx-absent" :
			PS5_PrxResultString( result ),
			(unsigned)library->module.loaded_handle, library->module.start_result,
			(unsigned)library->module.module_info_result,
			(unsigned long long)library->module.module_info_size,
			library->module.module_info_segment_count,
			library->module.load_error,
			(unsigned)library->module.rollback_result,
			library->module.rollback_stop_result,
			retained ? "retained" : "complete",
			optional_vgui ? " fallback=client-probe" : "" );
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
		"init_result=%d result=0 seg0=%llx+0x%x seg1=%llx+0x%x "
		"seg2=%llx+0x%x seg3=%llx+0x%x",
		path, library->module.name, (unsigned)library->module.handle,
		library->module.segment_count, library->module.descriptor->header.count,
		library->module_start_result,
		(unsigned long long)(uintptr_t)library->module.segments[0].address,
		library->module.segments[0].size,
		(unsigned long long)(uintptr_t)library->module.segments[1].address,
		library->module.segments[1].size,
		(unsigned long long)(uintptr_t)library->module.segments[2].address,
		library->module.segments[2].size,
		(unsigned long long)(uintptr_t)library->module.segments[3].address,
		library->module.segments[3].size );
	PS5_LogFilesystemPrxState( library, "XASH_FS_PRX_READY" );
	PS5_LogServerPrxState( library, "XASH_SERVER_PRX_READY" );
	PS5_LogMenuPrxState( library, "XASH_MENU_PRX_READY" );
	PS5_LogClientPrxState( library, "XASH_CLIENT_PRX_READY" );
	PS5_LogRefAgcPrxState( library, "XASH_REF_AGC_PRX_READY" );
	return library;
}

void COM_FreeLibrary( void *hInstance )
{
	ps5_dynamic_library_t **link = NULL;
	ps5_dynamic_library_t *library = PS5_FindDynamic( hInstance, &link );
	int result;
	if( !library ) return;
	PS5_LogFilesystemPrxState( library, "XASH_FS_PRX_STATE" );
	PS5_LogServerPrxState( library, "XASH_SERVER_PRX_STATE" );
	PS5_LogMenuPrxState( library, "XASH_MENU_PRX_STATE" );
	PS5_LogClientPrxState( library, "XASH_CLIENT_PRX_STATE" );
	PS5_LogRefAgcPrxState( library, "XASH_REF_AGC_PRX_STATE" );
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
		const int server = PS5_IsServerPrx( library );
		const int menu = PS5_IsMenuPrx( library );
		const int client = PS5_IsClientPrx( library );
		const int ref_agc = PS5_IsRefAgcPrx( library );
		*link = library->next;
		free( library );
		if( server ) ps5_server_give_fnptrs = NULL;
		if( filesystem )
			(void)ps5log_printf( PS5LOG_MARK,
				"XASH_FS_PRX_COMPLETE module=filesystem_stdio.prx stop_result=0 "
				"active_modules=%u ownership=exact",
				PS5_PrxLibraryActiveCount( ));
		if( server )
			(void)ps5log_printf( PS5LOG_MARK,
				"XASH_SERVER_PRX_COMPLETE module=server.prx stop_result=0 "
				"active_modules=%u ownership=exact",
				PS5_PrxLibraryActiveCount( ));
		if( menu )
		{
			(void)ps5log_printf( ps5_menu_api_pass && ps5_menu_ext_api_pass &&
				ps5_menu_init_calls == 1u && ps5_menu_shutdown_calls == 1u ?
				PS5LOG_MARK : PS5LOG_ERR,
				"XASH_MENU_PRX_COMPLETE module=menu.prx stop_result=0 "
				"api_pass=%d ext_api_pass=%d init_calls=%u shutdown_calls=%u "
				"redraw_calls=%u active_calls=%u active_modules=%u ownership=exact pass=%d",
				ps5_menu_api_pass, ps5_menu_ext_api_pass, ps5_menu_init_calls,
				ps5_menu_shutdown_calls, ps5_menu_redraw_calls, ps5_menu_active_calls,
				PS5_PrxLibraryActiveCount( ),
				ps5_menu_api_pass && ps5_menu_ext_api_pass &&
				ps5_menu_init_calls == 1u && ps5_menu_shutdown_calls == 1u );
			ps5_menu_get_api = NULL;
			ps5_menu_get_ext_api = NULL;
			ps5_menu_init = NULL;
			ps5_menu_shutdown = NULL;
			ps5_menu_redraw = NULL;
			ps5_menu_set_active = NULL;
			ps5_menu_is_visible = NULL;
			ps5_menu_init_calls = ps5_menu_shutdown_calls = 0u;
			ps5_menu_redraw_calls = ps5_menu_active_calls = 0u;
			ps5_menu_api_pass = ps5_menu_ext_api_pass = 0;
		}
		if( client )
		{
			const int pass = ps5_client_api_pass && ps5_client_abi_pass &&
				ps5_client_initialize_calls == 1u && ps5_client_init_calls == 1u &&
				ps5_client_vid_init_calls > 0u && ps5_client_frame_calls > 0u &&
				ps5_client_redraw_calls > 0u &&
				ps5_client_shutdown_calls == 1u;
			(void)ps5log_printf( pass ? PS5LOG_MARK : PS5LOG_ERR,
				"XASH_CLIENT_PRX_COMPLETE module=client.prx stop_result=0 "
				"api_pass=%d abi_pass=%d initialize_calls=%u init_calls=%u "
				"vid_init_calls=%u frame_calls=%u redraw_calls=%u shutdown_calls=%u "
				"active_modules=%u ownership=exact pass=%d",
				ps5_client_api_pass, ps5_client_abi_pass,
				ps5_client_initialize_calls, ps5_client_init_calls,
				ps5_client_vid_init_calls, ps5_client_frame_calls,
				ps5_client_redraw_calls, ps5_client_shutdown_calls,
				PS5_PrxLibraryActiveCount( ), pass );
			ps5_client_initialize = NULL;
			ps5_client_init = NULL;
			ps5_client_vid_init = NULL;
			ps5_client_frame = NULL;
			ps5_client_redraw = NULL;
			ps5_client_shutdown = NULL;
			ps5_client_initialize_calls = ps5_client_init_calls = 0u;
			ps5_client_vid_init_calls = ps5_client_frame_calls = 0u;
			ps5_client_redraw_calls = ps5_client_shutdown_calls = 0u;
			ps5_client_api_pass = ps5_client_abi_pass = 0;
		}
		if( ref_agc )
			(void)ps5log_printf( PS5LOG_MARK,
				"XASH_REF_AGC_PRX_COMPLETE module=ref_agc.prx stop_result=0 "
				"active_modules=%u ownership=exact pass=1",
				PS5_PrxLibraryActiveCount( ));
	}
}

void *COM_GetProcAddress( void *hInstance, const char *name )
{
	ps5_dynamic_library_t *library = PS5_FindDynamic( hInstance, NULL );
	if( library )
	{
		void *result = (void *)PS5_PrxGetProc( &library->module, name );
		if( PS5_IsServerPrx( library ) && result &&
			!strcmp( name, "GiveFnptrsToDll" ))
		{
			ps5_server_give_fnptrs = (void ( * )( void *, void * ))result;
			return (void *)PS5_ServerGiveFnptrsTrampoline;
		}
		if( PS5_IsMenuPrx( library ) && result && !strcmp( name, "GetMenuAPI" ))
		{
			ps5_menu_get_api = (MENUAPI)result;
			return (void *)PS5_MenuGetApiTrampoline;
		}
		if( PS5_IsMenuPrx( library ) && result && !strcmp( name, "GetExtAPI" ))
		{
			ps5_menu_get_ext_api = (UIEXTENEDEDAPI)result;
			return (void *)PS5_MenuGetExtApiTrampoline;
		}
		if( PS5_IsClientPrx( library ) && result && !strcmp( name, "Initialize" ))
		{
			ps5_client_initialize = (int ( * )( cl_enginefunc_t *, int ))result;
			return (void *)PS5_ClientInitializeTrampoline;
		}
		if( PS5_IsClientPrx( library ) && result && !strcmp( name, "HUD_Init" ))
		{
			ps5_client_init = (void ( * )( void ))result;
			return (void *)PS5_ClientInitTrampoline;
		}
		if( PS5_IsClientPrx( library ) && result && !strcmp( name, "HUD_VidInit" ))
		{
			ps5_client_vid_init = (int ( * )( void ))result;
			return (void *)PS5_ClientVidInitTrampoline;
		}
		if( PS5_IsClientPrx( library ) && result && !strcmp( name, "HUD_Frame" ))
		{
			ps5_client_frame = (void ( * )( double ))result;
			return (void *)PS5_ClientFrameTrampoline;
		}
		if( PS5_IsClientPrx( library ) && result && !strcmp( name, "HUD_Redraw" ))
		{
			ps5_client_redraw = (int ( * )( float, int ))result;
			return (void *)PS5_ClientRedrawTrampoline;
		}
		if( PS5_IsClientPrx( library ) && result && !strcmp( name, "HUD_Shutdown" ))
		{
			ps5_client_shutdown = (void ( * )( void ))result;
			return (void *)PS5_ClientShutdownTrampoline;
		}
		return result;
	}
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
