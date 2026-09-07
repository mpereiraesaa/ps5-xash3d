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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ps5log.h"
#include "ps5_xash_build.h"

typedef void ( *pfnChangeGame )( const char *progname );
int Host_Main( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame pChangeGame );

#ifndef PS5_XASH_TITLE_ID
#define PS5_XASH_TITLE_ID "PPSA99996"
#endif
#define PS5_XASH_APP_NAME "xash3d-engine"
#define PS5_XASH_RODIR "/app0/xash3d"
#define PS5_XASH_GAMEDIR "valve"

static const char *const basedir_candidates[] = {
	"/download0/xash3d",
	"/data/homebrew/" PS5_XASH_TITLE_ID "/xash3d-rw",
	"/tmp/xash3d",
};

static uint64_t now_ns( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void change_game_stub( const char *progname )
{
	(void)progname;
}

static const char *select_basedir( void )
{
	size_t i;
	for( i = 0; i < sizeof( basedir_candidates ) / sizeof( basedir_candidates[0] ); i++ )
	{
		const char *dir = basedir_candidates[i];
		if( mkdir( dir, 0777 ) != 0 && errno != EEXIST )
			continue;
		if( access( dir, W_OK ) != 0 )
			continue;
		if( chdir( dir ) != 0 )
			continue;
		setenv( "XASH3D_BASEDIR", dir, 1 );
		return dir;
	}
	return NULL;
}

int main( int argc, char **argv )
{
	ps5log_config log_config;
	const char *log_path = 0;
	const char *basedir;
	const uint64_t boot_token = now_ns( );
	int config_result, log_result, result;
	struct stat st;
	char *engine_argv[] = {
		"eboot.bin",
		"-dev", "2",
		"-console",
		"-log",
		"-rodir", PS5_XASH_RODIR,
		"-game", PS5_XASH_GAMEDIR,
		"+map", PS5_XASH_BOOT_MAP,
		NULL,
	};
	int engine_argc = (int)( sizeof( engine_argv ) / sizeof( engine_argv[0] )) - 1;
	(void)argc;
	(void)argv;

	ps5log_config_defaults( &log_config );
	config_result = ps5log_load_config( ps5log_default_conf_paths,
		ps5log_default_conf_path_count, &log_config, &log_path );
	log_result = config_result == 0
		? ps5log_init( &log_config, PS5_XASH_TITLE_ID, PS5_XASH_APP_NAME, boot_token )
		: 1;
	(void)ps5log_capture_stdio( PS5LOG_CAPTURE_STDIO );
	(void)ps5log_line( PS5LOG_INFO, "LOG_SCHEMA=3" );
	(void)ps5log_line( PS5LOG_INFO, "LOG_TRANSPORT=ps5log/1 tcp structured" );
	(void)ps5log_hex64( PS5LOG_INFO, "LOG_BOOT_MONOTONIC_NS", boot_token );
	(void)ps5log_printf( PS5LOG_INFO, "LOG_CONFIG_RESULT=%d LOG_INIT_RESULT=%d path=%s",
		config_result, log_result, log_path ? log_path : "unavailable" );

	basedir = select_basedir( );
	(void)ps5log_printf( PS5LOG_MARK,
		"XASH_BOOT schema=1 slice=engine-boot mode=dedicated fw=12.02 "
		"engine=%s hlsdk=%s rodir=%s basedir=%s gamedir=%s map=%s gate_seconds=%d "
		"rodir_present=%d",
		PS5_XASH_ENGINE_COMMIT, PS5_XASH_HLSDK_COMMIT, PS5_XASH_RODIR,
		basedir ? basedir : "none", PS5_XASH_GAMEDIR, PS5_XASH_BOOT_MAP,
		PS5_XASH_GATE_SECONDS, stat( PS5_XASH_RODIR "/" PS5_XASH_GAMEDIR, &st ) == 0 );
	if( !basedir )
		(void)ps5log_line( PS5LOG_WARN, "XASH_BASEDIR_UNAVAILABLE staying in process cwd" );

	fflush( stdout );
	result = Host_Main( engine_argc, engine_argv, PS5_XASH_GAMEDIR, 0, change_game_stub );
	fflush( stdout );
	fflush( stderr );

	(void)ps5log_printf( PS5LOG_MARK, "XASH_EXIT result=%d", result );
	ps5log_close( "xash-engine-boot-complete" );
	_exit( 0 );
}
