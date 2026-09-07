/*
sys_ps5.c - PlayStation 5 platform backend for the dedicated engine boot gate
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Replaces engine/platform/posix/sys_posix.c on PS5. Timing and sleeping use the
POSIX clocks that libkernel exports. The three libc symbols the SDK does not
provide (__assert, getpwuid, and the C++ __dso_handle anchor) are defined here
so the engine and the statically linked modules link without patches.

The boot gate is bounded: PS5_XASH_GATE_SECONDS after the first Platform_Sleep
call the backend queues "quit" once, so a hardware run ends with the engine's
own shutdown path and a clean telemetry BYE instead of an operator close.
*/

#include "platform/platform.h"
#include "common.h"
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef PS5_XASH_GATE_SECONDS
#define PS5_XASH_GATE_SECONDS 0
#endif

/* Anchor for __cxa_atexit registrations made by the C++ server module. */
void *__dso_handle = &__dso_handle;

/* FreeBSD libc assert() destination; libSceLibcInternal does not export it. */
void __assert( const char *func, const char *file, int line, const char *expr )
{
	fprintf( stderr, "Assertion failed: (%s), function %s, file %s, line %d.\n",
		expr, func ? func : "?", file, line );
	fflush( stderr );
	abort( );
}

/* id_posix.c only uses pw_name to salt the machine identifier. */
struct passwd *getpwuid( uid_t uid )
{
	static struct passwd pw;
	static char name[] = "ps5";
	memset( &pw, 0, sizeof( pw ));
	pw.pw_name = name;
	pw.pw_dir = name;
	pw.pw_shell = name;
	pw.pw_uid = uid;
	return &pw;
}

/* whereami.c probes dladdr for the executable path; there is no dynamic
   symbol table to consult on this firmware, so it falls back to argv[0]. */
int dladdr( const void *addr, Dl_info *info )
{
	(void)addr;
	(void)info;
	return 0;
}

/* Crash handling is disabled (XASH_CRASHHANDLER=0); klog already reports
   signals on the console. */
void Sys_SetupCrashHandler( const char *argv0 )
{
	(void)argv0;
}

void Sys_RestoreCrashHandler( void )
{
}

void Posix_Daemonize( void )
{
	if( Sys_CheckParm( "-daemonize" ))
		Sys_Error( "Daemonize not supported on this platform!" );
}

static void PS5_SigtermCallback( int signal )
{
	string reason;
	Q_snprintf( reason, sizeof( reason ), "caught signal %d", signal );
	Sys_Quit( reason );
}

void Posix_SetupSigtermHandling( void )
{
	struct sigaction act = { 0 };
	act.sa_handler = PS5_SigtermCallback;
	act.sa_flags = 0;
	sigaction( SIGTERM, &act, NULL );
}

void Platform_ShellExecute( const char *path, const char *parms )
{
	Con_Reportf( S_WARN "Tried to shell execute ;%s; -- not supported\n", path );
}

static double PS5_MonotonicSeconds( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

/*
The host reads the clock at the top of every frame on the main thread, which
makes it the one hook a dedicated build reaches every frame without touching
engine sources. Only the thread that made the first call (Host_Main) may queue
the quit; worker threads that read the clock are ignored.
*/
static void PS5_GateTick( double now )
{
#if PS5_XASH_GATE_SECONDS > 0
	static double started;
	static pthread_t owner;
	static qboolean quit_queued;

	if( quit_queued )
		return;
	if( started == 0.0 )
	{
		started = now;
		owner = pthread_self( );
		return;
	}
	if( !pthread_equal( owner, pthread_self( )))
		return;
	if( now - started < (double)PS5_XASH_GATE_SECONDS )
		return;

	quit_queued = true;
	Con_Printf( "PS5_XASH_GATE_TIMEOUT seconds=%d action=quit\n", PS5_XASH_GATE_SECONDS );
	Cbuf_AddText( "quit\n" );
#else
	(void)now;
#endif
}

double Platform_DoubleTime( void )
{
	double now = PS5_MonotonicSeconds( );
	PS5_GateTick( now );
	return now;
}

void Platform_Sleep( int msec )
{
	usleep( msec * 1000 );
}
