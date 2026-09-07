/*
boot_ps5.c - PS5 entry point for the Xash3D dedicated engine boot gate
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Brings up ps5log/1 with stdio capture so every engine console line reaches the
laboratory, chooses a writable base directory, points the engine at the
read-only game data under /app0 and hands control to Host_Main. The engine's
own quit path returns here; the process then leaves through _exit(0), which is
the exit the shell accepts without an error dialog.
*/

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>

#include "ps5log.h"
#include "ps5_xash_build.h"
#include "in_ps5.h"
#include "audio_ps5.h"
#include "mem_ps5.h"

typedef void ( *pfnChangeGame )( const char *progname );
int Host_Main( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame pChangeGame );
void Sys_SetupCrashHandler( const char *argv0 );
void PS5_LogModuleMap( void );

void PS5_SetCwd( const char *dir );
void PS5_ConsoleFlush( void );
extern void *__real_malloc( size_t size );
extern void __real_free( void *ptr );
int PS5_ListingRefusedCount( void );
int PS5_LoadDirIndex( const char *image_root, const char *index_path );
void PS5_UnloadDirIndex( void );
int PS5_ThreadTimeGateRun( void );
int sceUserServiceInitialize( const void *params );
int sceUserServiceGetForegroundUser( int32_t *user_id );
int sceUserServiceTerminate( void );

#ifndef PS5_XASH_TITLE_ID
#define PS5_XASH_TITLE_ID "PPSA99996"
#endif
#define PS5_XASH_APP_NAME "xash3d-engine"
#define PS5_XASH_RODIR "/app0/xash3d"
#define PS5_XASH_GAMEDIR "valve"

/*
Writable storage inside the application sandbox. /download0 is the persistent
title-local volume the foundation validated across relaunches whenever
param.json carries a positive downloadDataSize (this title reserves 256).
/temp0 is conditional and was absent on the tested ShadowMount environment;
it is probed, never assumed. Host paths such as /data or /user are outside
the sandbox and are deliberately not attempted.
*/
static const char *const basedir_candidates[] = {
	"/download0/xash3d",
	"/temp0/xash3d",
};

static uint64_t now_ns( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#if PS5_XASH_LIBC_SMOKE
/*
 * Evidence-only calls through volatile pointers prevent the compiler from
 * folding these into builtins. This gate answers whether the named FW exports
 * used by the production engine execute correctly on representative input.
 */
static int probe_optional_libc( void )
{
	int ( *volatile system_strcasecmp )( const char *, const char * ) = strcasecmp;
	size_t ( *volatile system_strnlen )( const char *, size_t ) = strnlen;
	size_t ( *volatile system_strlcpy )( char *, const char *, size_t ) = strlcpy;
	size_t ( *volatile system_strlcat )( char *, const char *, size_t ) = strlcat;
	char copy[8] = "";
	char cat[12] = "gfx/";
	int rc;
	size_t n;
	int passed = 1;

	(void)ps5log_line( PS5LOG_MARK, "XASH_LIBC_SMOKE_BEGIN schema=1 symbols=strcasecmp,strnlen,strlcpy,strlcat" );
	(void)ps5log_line( PS5LOG_INFO, "XASH_LIBC_SMOKE_CALL symbol=strcasecmp" );
	rc = system_strcasecmp( "PaLeTtE.LmP", "palette.lmp" );
	passed &= rc == 0;
	(void)ps5log_printf( PS5LOG_MARK, "XASH_LIBC_SMOKE_RESULT symbol=strcasecmp result=%d pass=%d", rc, rc == 0 );

	(void)ps5log_line( PS5LOG_INFO, "XASH_LIBC_SMOKE_CALL symbol=strnlen" );
	n = system_strnlen( "palette", 4 );
	passed &= n == 4;
	(void)ps5log_printf( PS5LOG_MARK, "XASH_LIBC_SMOKE_RESULT symbol=strnlen result=%zu pass=%d", n, n == 4 );

	(void)ps5log_line( PS5LOG_INFO, "XASH_LIBC_SMOKE_CALL symbol=strlcpy" );
	n = system_strlcpy( copy, "palette", sizeof( copy ));
	passed &= n == 7 && strcmp( copy, "palette" ) == 0;
	(void)ps5log_printf( PS5LOG_MARK, "XASH_LIBC_SMOKE_RESULT symbol=strlcpy result=%zu value=%s pass=%d",
		n, copy, n == 7 && strcmp( copy, "palette" ) == 0 );

	(void)ps5log_line( PS5LOG_INFO, "XASH_LIBC_SMOKE_CALL symbol=strlcat" );
	n = system_strlcat( cat, "palette", sizeof( cat ));
	passed &= n == 11 && strcmp( cat, "gfx/palette" ) == 0;
	(void)ps5log_printf( PS5LOG_MARK, "XASH_LIBC_SMOKE_RESULT symbol=strlcat result=%zu value=%s pass=%d",
		n, cat, n == 11 && strcmp( cat, "gfx/palette" ) == 0 );
	(void)ps5log_printf( PS5LOG_MARK, "XASH_LIBC_SMOKE_END pass=%d", passed );
	return passed;
}
#endif

#if PS5_XASH_AUDIO_GATE
/*
The reference lifecycle opens the main port for the system user (0xff). If FW
12.02 refuses that, XASH_AUDIO_USER=foreground selects the foreground user as
an explicit, recorded build variant - never a silent fallback, and the marker
below always names which one the artifact carries. UserService is terminated
only when this call owned it, the same rule in_ps5.c already follows, so the
ScePad backend keeps its own ownership when both gates are enabled.
*/
static int32_t ps5_audio_gate_user( void )
{
#if PS5_XASH_AUDIO_USER_FOREGROUND
	int32_t user_id = -1;
	const int init_rc = sceUserServiceInitialize( NULL );
	const int get_rc = sceUserServiceGetForegroundUser( &user_id );
	int terminate_rc = 0;

	if( init_rc == 0 )
		terminate_rc = sceUserServiceTerminate( );
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_AUDIO_USER schema=1 source=foreground user=%d user_service_rc=%d "
		"get_rc=%d owned=%d terminate_rc=%d",
		user_id, init_rc, get_rc, init_rc == 0, terminate_rc );
	return user_id;
#else
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_AUDIO_USER schema=1 source=system user=0x%x",
		(unsigned)PS5_AUDIO_USER_SYSTEM );
	return PS5_AUDIO_USER_SYSTEM;
#endif
}
#endif

static void change_game_stub( const char *progname )
{
	(void)progname;
}

static void probe_listing( const char *path )
{
	DIR *dir = opendir( path );
	int entries = -1;
	int err = 0;
	if( dir )
	{
		struct dirent *ent;
		entries = 0;
		while(( ent = readdir( dir )) != NULL )
			entries++;
		closedir( dir );
	}
	else err = errno;
	(void)ps5log_printf( PS5LOG_INFO, "XASH_FS_PROBE listdir=%s entries=%d errno=%d", path, entries, err );
}

/* Report every step so a refused volume explains itself in the transcript. */
static void probe_path( const char *path )
{
	struct stat st;
	int rc = stat( path, &st );
	(void)ps5log_printf( PS5LOG_INFO, "XASH_FS_PROBE path=%s stat_rc=%d errno=%d mode=0%o",
		path, rc, rc ? errno : 0, rc ? 0 : (unsigned)st.st_mode );
}

/* How much can the libc heap actually give? Doubling probe, freed at once. */
static void probe_libc_heap( void )
{
	size_t size, largest = 0;
	for( size = 1u << 20; size <= ( 512u << 20 ); size <<= 1 )
	{
		void *mem = __real_malloc( size );
		if( !mem )
			break;
		((volatile char *)mem)[0] = 1;
		((volatile char *)mem)[size - 1] = 1;
		__real_free( mem );
		largest = size;
	}
	(void)ps5log_printf( PS5LOG_INFO, "XASH_HEAP_PROBE libc_malloc_largest_mib=%u first_failure_mib=%u",
		(unsigned)( largest >> 20 ), (unsigned)( size >> 20 ));
}

/* Returns the writable directory when file creation there works. */
static const char *select_rwdir( void )
{
	size_t i;
	probe_path( "/app0" );
	probe_path( "/app0/xash3d/valve" );
	probe_path( "/app0/xash3d/valve/liblist.gam" );
	probe_path( "/download0" );
	probe_path( "/temp0" );
	probe_listing( "/app0/xash3d" );
	probe_listing( "/app0/xash3d/valve" );
	probe_listing( "/download0/xash3d" );
	for( i = 0; i < sizeof( basedir_candidates ) / sizeof( basedir_candidates[0] ); i++ )
	{
		const char *dir = basedir_candidates[i];
		char probe[300];
		int mkdir_rc, mkdir_errno, access_rc, access_errno, open_fd, open_errno;

		mkdir_rc = mkdir( dir, 0777 );
		mkdir_errno = mkdir_rc ? errno : 0;
		access_rc = access( dir, W_OK );
		access_errno = access_rc ? errno : 0;
		snprintf( probe, sizeof( probe ), "%s/.xash-probe", dir );
		open_fd = open( probe, O_WRONLY | O_CREAT | O_TRUNC, 0666 );
		open_errno = open_fd < 0 ? errno : 0;
		if( open_fd >= 0 )
		{
			(void)write( open_fd, "ok\n", 3 );
			close( open_fd );
			unlink( probe );
		}
		(void)ps5log_printf( PS5LOG_INFO,
			"XASH_RWDIR_PROBE dir=%s mkdir_rc=%d mkdir_errno=%d access_rc=%d access_errno=%d "
			"open_rc=%d open_errno=%d",
			dir, mkdir_rc, mkdir_errno, access_rc, access_errno,
			open_fd >= 0 ? 0 : -1, open_errno );
		if( open_fd >= 0 )
			return dir;
	}
	return NULL;
}

int main( int argc, char **argv )
{
	ps5log_config log_config;
	const char *log_path = 0;
	const char *basedir;
	const char *rwdir;
	char logpath[300] = "";
	const uint64_t boot_token = now_ns( );
	int config_result, log_result, result;
#if PS5_XASH_PAD_GATE
	int pad_result;
#endif
#if PS5_XASH_AUDIO_GATE
	int audio_result;
#endif
#if PS5_XASH_MEMORY_GATE
	int memory_gate_result;
#endif
	int thread_time_pass = 1;
	int memory_pass = 1;
	int memory_shutdown_result;
	struct stat st;
	char *engine_argv[16];
	int engine_argc = 0;
	(void)argc;
	(void)argv;

	ps5log_config_defaults( &log_config );
	config_result = ps5log_load_config( ps5log_default_conf_paths,
		ps5log_default_conf_path_count, &log_config, &log_path );
	log_result = config_result == 0
		? ps5log_init( &log_config, PS5_XASH_TITLE_ID, PS5_XASH_APP_NAME, boot_token )
		: 1;
	/* stdio cannot be redirected in this sandbox (dup2 onto 0-2 fails with
	   EPERM and the title starts with them closed); the console reaches the
	   transcript through the write() shim in sys_ps5.c instead. */
	Sys_SetupCrashHandler( "eboot.bin" );
	PS5_LogModuleMap( );
	(void)ps5log_line( PS5LOG_INFO, "LOG_SCHEMA=3" );
	(void)ps5log_line( PS5LOG_INFO, "LOG_TRANSPORT=ps5log/1 tcp structured" );
	(void)ps5log_hex64( PS5LOG_INFO, "LOG_BOOT_MONOTONIC_NS", boot_token );
	(void)ps5log_printf( PS5LOG_INFO, "LOG_CONFIG_RESULT=%d LOG_INIT_RESULT=%d path=%s",
		config_result, log_result, log_path ? log_path : "unavailable" );

#if PS5_XASH_PAD_GATE
	pad_result = PS5_PadInputInit( );
	if( pad_result != 0 )
	{
		(void)ps5log_printf( PS5LOG_ERR, "XASH_PAD_GATE_ABORT init_rc=%d", pad_result );
		ps5log_close( "xash-pad-init-failed" );
		_exit( 2 );
	}
#endif

#if PS5_XASH_LIBC_SMOKE
	if( !probe_optional_libc( ))
	{
		(void)ps5log_line( PS5LOG_ERR, "XASH_LIBC_SMOKE_FAILED" );
		ps5log_close( "xash-libc-smoke-failed" );
		_exit( 2 );
	}
#endif

#if PS5_XASH_AUDIO_GATE
	audio_result = PS5_AudioGateRun( ps5_audio_gate_user( ));
	if( audio_result != 0 )
	{
		(void)ps5log_printf( PS5LOG_ERR, "XASH_AUDIO_GATE_FAILED rc=%d", audio_result );
		ps5log_close( "xash-audio-gate-failed" );
		_exit( 2 );
	}
#endif
#if PS5_XASH_MEMORY_GATE
	memory_gate_result = PS5_MemoryGateRun( );
	if( memory_gate_result != 0 )
	{
		(void)ps5log_printf( PS5LOG_ERR,
			"XASH_MEMORY_GATE_FAILED rc=%d", memory_gate_result );
		memory_pass = 0;
	}
#endif
#if PS5_XASH_THREAD_TIME_GATE
	if( PS5_ThreadTimeGateRun( ) != 0 )
		thread_time_pass = 0;
#endif
	(void)ps5log_printf( PS5LOG_INFO, "XASH_DIRINDEX root=%s entries=%d",
		PS5_XASH_RODIR, PS5_LoadDirIndex( PS5_XASH_RODIR, PS5_XASH_RODIR "/.dirindex" ));
	probe_libc_heap( );
	rwdir = select_rwdir( );
	if( rwdir )
	{
		probe_listing( rwdir );
		basedir = rwdir;
		snprintf( logpath, sizeof( logpath ), "%s/engine.log", rwdir );
	}
	else
	{
		(void)ps5log_line( PS5LOG_WARN, "XASH_BASEDIR_UNAVAILABLE using /app0/xash3d read-only" );
		basedir = PS5_XASH_RODIR;
	}

	setenv( "XASH3D_BASEDIR", basedir, 1 );
	PS5_SetCwd( basedir );
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_BOOT schema=1 slice=engine-boot mode=%s ref=%s fw=12.02 "
		"engine=%s hlsdk=%s rodir=%s basedir=%s gamedir=%s map=%s gate_seconds=%d pad_gate=%d "
		"audio_gate=%d memory_gate=%d thread_time_gate=%d "
		"rodir_present=%d",
		PS5_XASH_MODE, PS5_XASH_MODE_CLIENT ? PS5_XASH_REF : "none",
		PS5_XASH_ENGINE_COMMIT, PS5_XASH_HLSDK_COMMIT, rwdir ? PS5_XASH_RODIR : "none", basedir,
		PS5_XASH_GAMEDIR, PS5_XASH_BOOT_MAP,
		PS5_XASH_GATE_SECONDS, PS5_XASH_PAD_GATE, PS5_XASH_AUDIO_GATE,
		PS5_XASH_MEMORY_GATE, PS5_XASH_THREAD_TIME_GATE,
		stat( PS5_XASH_RODIR "/" PS5_XASH_GAMEDIR, &st ) == 0 );

	engine_argv[engine_argc++] = "eboot.bin";
	/* developer 1 keeps the Con_DPrintf proofs (filesystem load, spawn)
	   without the per-asset spam that congests the telemetry stream. */
	engine_argv[engine_argc++] = "-dev";
	engine_argv[engine_argc++] = "1";
	engine_argv[engine_argc++] = "-console";
	engine_argv[engine_argc++] = "-log";
	if( rwdir )
	{
		engine_argv[engine_argc++] = logpath;
		engine_argv[engine_argc++] = "-rodir";
		engine_argv[engine_argc++] = PS5_XASH_RODIR;
	}
	engine_argv[engine_argc++] = "-game";
	engine_argv[engine_argc++] = PS5_XASH_GAMEDIR;
#if PS5_XASH_MODE_CLIENT
	engine_argv[engine_argc++] = "-ref";
	engine_argv[engine_argc++] = PS5_XASH_REF;
	engine_argv[engine_argc++] = "-nosound";
#endif
	engine_argv[engine_argc++] = "+map";
	engine_argv[engine_argc++] = PS5_XASH_BOOT_MAP;
	engine_argv[engine_argc] = NULL;
	fflush( stdout );
	result = Host_Main( engine_argc, engine_argv, PS5_XASH_GAMEDIR, 0, change_game_stub );
	fflush( stdout );
	fflush( stderr );
	PS5_ConsoleFlush( );
	PS5_UnloadDirIndex( );

#if PS5_XASH_PAD_GATE
	pad_result = PS5_PadInputShutdown( );
	if( pad_result != 0 )
		(void)ps5log_printf( PS5LOG_ERR, "XASH_PAD_GATE_SHUTDOWN_FAILED rc=%d", pad_result );
#endif

	{
		Ps5MemoryStats arena = {0};
		Ps5MemoryRootStats root = {0};
		PS5_MemStats( &arena, &root );
		if( PS5_MemValidate( ) != PS5_MEMORY_OK || arena.live_gpu != 0u ||
			arena.retiring_gpu != 0u || arena.guard_failures != 0u ||
			arena.stale_errors != 0u || arena.failures != 0u )
			memory_pass = 0;
		(void)ps5log_printf( memory_pass ? PS5LOG_MARK : PS5LOG_ERR,
			"XASH_MEMORY_SUMMARY schema=1 arena_bytes=%zu live_bytes=%zu "
			"peak_bytes=%zu alloc_calls=%llu free_calls=%llu realloc_calls=%llu "
			"live_cpu=%u live_gpu=%u retiring_gpu=%u failures=%llu "
			"guard_failures=%llu stale_errors=%llu retire_calls=%llu "
			"reclaim_calls=%llu process_lifetime_cpu=%u "
			"process_lifetime_bytes=%zu foreign_calls=%llu foreign_bytes=%llu pass=%d",
			arena.arena_bytes, arena.live_bytes, arena.peak_bytes,
			(unsigned long long)arena.alloc_calls,
			(unsigned long long)arena.free_calls,
			(unsigned long long)arena.realloc_calls, arena.live_cpu,
			arena.live_gpu, arena.retiring_gpu,
			(unsigned long long)arena.failures,
			(unsigned long long)arena.guard_failures,
			(unsigned long long)arena.stale_errors,
			(unsigned long long)arena.retire_calls,
			(unsigned long long)arena.reclaim_calls,
			arena.live_cpu, arena.live_bytes,
			(unsigned long long)root.foreign_calls,
			(unsigned long long)root.foreign_bytes, memory_pass );

		memory_shutdown_result = PS5_MemShutdown( );
		PS5_MemStats( &arena, &root );
		if( memory_shutdown_result != PS5_MEMORY_OK || root.mapped ||
			root.allocated || root.unmap_calls != 1u ||
			root.release_calls != 1u || root.unmap_rc != 0 ||
			root.release_rc != 0 )
			memory_pass = 0;
		(void)ps5log_printf( memory_pass ? PS5LOG_MARK : PS5LOG_ERR,
			"XASH_MEMORY_TEARDOWN schema=1 result=%d reserve_calls=%u "
			"allocate_calls=%u map_calls=%u unmap_calls=%u release_calls=%u "
			"reserve_rc=%d allocate_rc=%d map_rc=%d unmap_rc=%d release_rc=%d "
			"mapped=%u allocated=%u live_bytes=%zu live_cpu=%u live_gpu=%u "
			"retiring_gpu=%u lifetime_reclaims=%llu lifetime_bytes=%llu "
			"ownership=exact pass=%d",
			memory_shutdown_result, root.reserve_calls, root.allocate_calls,
			root.map_calls, root.unmap_calls, root.release_calls,
			root.reserve_rc, root.allocate_rc, root.map_rc, root.unmap_rc,
			root.release_rc, root.mapped, root.allocated, arena.live_bytes,
			arena.live_cpu, arena.live_gpu, arena.retiring_gpu,
			(unsigned long long)arena.lifetime_reclaims,
			(unsigned long long)arena.lifetime_bytes, memory_pass );
		(void)ps5log_printf( PS5LOG_MARK,
			"XASH_EXIT result=%d listing_refused=%d large_alloc_bytes=%zu "
			"large_alloc_peak=%zu large_alloc_count=%u large_alloc_failures=%llu "
			"libc_calls=%llu libc_bytes=%llu pad_gate=%d memory_gate=%d memory_pass=%d "
			"thread_time_gate=%d thread_time_pass=%d",
			result, PS5_ListingRefusedCount( ), arena.live_bytes,
			arena.peak_bytes, arena.live_cpu + arena.live_gpu,
			(unsigned long long)arena.failures,
			(unsigned long long)root.foreign_calls,
			(unsigned long long)root.foreign_bytes, PS5_XASH_PAD_GATE,
			PS5_XASH_MEMORY_GATE, memory_pass, PS5_XASH_THREAD_TIME_GATE,
			thread_time_pass );
	}
	ps5log_close( memory_pass && thread_time_pass ? "xash-engine-boot-complete" :
		thread_time_pass ? "xash-memory-gate-failed" : "xash-thread-time-gate-failed" );
	_exit( memory_pass && thread_time_pass ? 0 : 2 );
}
