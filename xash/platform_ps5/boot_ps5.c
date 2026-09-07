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

typedef void ( *pfnChangeGame )( const char *progname );
int Host_Main( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame pChangeGame );
void Sys_SetupCrashHandler( const char *argv0 );
void PS5_LogModuleMap( void );

void PS5_SetCwd( const char *dir );
void PS5_ConsoleFlush( void );
void PS5_MemStats( size_t *bytes, size_t *peak, int *count, int *failures );
unsigned long long PS5_LibcCalls( void );
unsigned long long PS5_LibcBytes( void );
extern void *__real_malloc( size_t size );
extern void __real_free( void *ptr );
int PS5_ListingRefusedCount( void );
int PS5_LoadDirIndex( const char *image_root, const char *index_path );

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
		"rodir_present=%d",
		PS5_XASH_MODE, PS5_XASH_MODE_CLIENT ? PS5_XASH_REF : "none",
		PS5_XASH_ENGINE_COMMIT, PS5_XASH_HLSDK_COMMIT, rwdir ? PS5_XASH_RODIR : "none", basedir,
		PS5_XASH_GAMEDIR, PS5_XASH_BOOT_MAP,
		PS5_XASH_GATE_SECONDS, PS5_XASH_PAD_GATE,
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

#if PS5_XASH_PAD_GATE
	pad_result = PS5_PadInputShutdown( );
	if( pad_result != 0 )
		(void)ps5log_printf( PS5LOG_ERR, "XASH_PAD_GATE_SHUTDOWN_FAILED rc=%d", pad_result );
#endif

	{
		size_t bytes, peak;
		int count, failures;
		PS5_MemStats( &bytes, &peak, &count, &failures );
		(void)ps5log_printf( PS5LOG_MARK, "XASH_EXIT result=%d listing_refused=%d large_alloc_bytes=%zu large_alloc_peak=%zu large_alloc_count=%d large_alloc_failures=%d libc_calls=%llu libc_bytes=%llu pad_gate=%d",
			result, PS5_ListingRefusedCount( ), bytes, peak, count, failures,
			PS5_LibcCalls( ), PS5_LibcBytes( ), PS5_XASH_PAD_GATE );
	}
	ps5log_close( "xash-engine-boot-complete" );
	_exit( 0 );
}
