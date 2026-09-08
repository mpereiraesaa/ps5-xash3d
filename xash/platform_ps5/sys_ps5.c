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
POSIX clocks that libkernel exports. The C++ __dso_handle anchor remains here;
the project-owned libc compatibility surface lives in libc_shims_ps5.c.

The boot gate is bounded: PS5_XASH_GATE_SECONDS after the first Platform_Sleep
call the backend queues "quit" once, so a hardware run ends with the engine's
own shutdown path and a clean telemetry BYE instead of an operator close.
*/

#include "platform/platform.h"
#include "common.h"
#include "ps5log.h"
#include "ps5_xash_build.h"
#include "in_ps5.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <stdint.h>
#include <ucontext.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef PS5_XASH_GATE_SECONDS
#error "ps5_xash_build.h must define PS5_XASH_GATE_SECONDS"
#endif

/* Anchor for __cxa_atexit registrations made by the C++ server module. */
void *__dso_handle = &__dso_handle;

/*
Console sink. The sandbox refuses dup2() onto descriptors 0-2 (EPERM) and
starts the title with all three closed, so stdio can never reach ps5log by
redirection. The engine console writes with write(STDOUT_FILENO, ...)
(sys_con.c Sys_PrintLogfile); this write() shim forwards those bytes to the
telemetry stream as RAW lines and hands every other descriptor to the kernel
syscall alias _write that libkernel exports.
*/
extern ssize_t _write( int fd, const void *buf, size_t count );

/*
The engine emits one console line as several write() calls (timestamp, text,
ANSI colour escapes), and structured telemetry records may land between
them. Console bytes are therefore assembled into whole lines and stripped of
escape sequences before they enter the stream, so RAW lines stay parseable
next to the structured records.
*/
static char console_line[2048];
static size_t console_used;
static int console_in_escape;

static void console_flush( void )
{
	if( console_used )
	{
		(void)ps5log_raw( console_line, console_used );
		console_used = 0;
	}
}

static void console_write( const unsigned char *bytes, size_t count )
{
	size_t i;
	for( i = 0; i < count; i++ )
	{
		unsigned char c = bytes[i];
		if( console_in_escape )
		{
			if(( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ))
				console_in_escape = 0;
			continue;
		}
		if( c == 0x1b )
		{
			console_in_escape = 1;
			continue;
		}
		if( c == '\r' )
			continue;
		console_line[console_used++] = (char)c;
		if( c == '\n' || console_used >= sizeof( console_line ) - 1 )
		{
			if( c != '\n' )
				console_line[console_used++] = '\n';
			console_flush( );
		}
	}
}

ssize_t write( int fd, const void *buf, size_t count )
{
	if( fd == STDOUT_FILENO || fd == STDERR_FILENO )
	{
		console_write( (const unsigned char *)buf, count );
		return (ssize_t)count;
	}
	return _write( fd, buf, count );
}

void PS5_ConsoleFlush( void )
{
	if( console_used )
	{
		console_line[console_used++] = '\n';
		console_flush( );
	}
}

/*
Non-blocking sockets. The sandbox refuses ioctl(FIONBIO) with EPERM and also
refused the fcntl(F_GETFL) round trip on the engine's UDP socket, while
fcntl(F_SETFL, O_NONBLOCK) is what the telemetry client uses successfully on
its TCP socket. FIONBIO is therefore mapped to a plain F_SETFL; if even that
is refused the descriptor is remembered and recvfrom() is emulated with a
zero-timeout poll() so the frame loop never blocks on an empty socket.
*/
extern int _ioctl( int fd, unsigned long request, ... );
extern ssize_t _recvfrom( int fd, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen );

#define PS5_MAX_FD 1024
static unsigned char ps5_emulated_nonblock[PS5_MAX_FD];

int ioctl( int fd, unsigned long request, ... )
{
	va_list args;
	void *arg;
	va_start( args, request );
	arg = va_arg( args, void * );
	va_end( args );
	if( request == FIONBIO )
	{
		int enable = arg ? *(int *)arg : 1;
		int rc = fcntl( fd, F_SETFL, enable ? O_NONBLOCK : 0 );
		int saved = errno;
		if( rc < 0 && fd >= 0 && fd < PS5_MAX_FD )
		{
			ps5_emulated_nonblock[fd] = enable ? 1 : 0;
			(void)ps5log_printf( PS5LOG_INFO, "XASH_SOCKET_NONBLOCK fd=%d fcntl_rc=%d errno=%d emulated=%d",
				fd, rc, saved, enable );
			errno = 0;
			return 0;
		}
		if( rc == 0 && fd >= 0 && fd < PS5_MAX_FD )
			ps5_emulated_nonblock[fd] = 0;
		return rc < 0 ? -1 : 0;
	}
	return _ioctl( fd, request, arg );
}

ssize_t recvfrom( int fd, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen )
{
	if( fd >= 0 && fd < PS5_MAX_FD && ps5_emulated_nonblock[fd] )
	{
		struct pollfd pfd;
		int ready;
		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		ready = poll( &pfd, 1, 0 );
		if( ready <= 0 )
		{
			errno = ready < 0 ? errno : EWOULDBLOCK;
			return -1;
		}
	}
	return _recvfrom( fd, buf, len, flags, from, fromlen );
}

/*
Name resolution. gethostname and getaddrinfo would come from
libScePosixForWebKit, a browser-oriented module the title has no business
depending on. Numeric addresses are resolved locally; names fail cleanly.
*/
int gethostname( char *name, size_t len )
{
	if( !name || len == 0 )
	{
		errno = EINVAL;
		return -1;
	}
	Q_strncpy( name, "ps5", len );
	return 0;
}

int getaddrinfo( const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res )
{
	struct addrinfo *ai;
	struct sockaddr_in *sin;
	struct in_addr addr;
	int port = service ? atoi( service ) : 0;
	if( !res )
		return EAI_FAIL;
	*res = NULL;
	if( !node || inet_pton( AF_INET, node, &addr ) != 1 )
		return EAI_NONAME;
	ai = calloc( 1, sizeof( *ai ) + sizeof( *sin ));
	if( !ai )
		return EAI_MEMORY;
	sin = (struct sockaddr_in *)( ai + 1 );
	sin->sin_len = sizeof( *sin );
	sin->sin_family = AF_INET;
	sin->sin_port = htons( (unsigned short)port );
	sin->sin_addr = addr;
	ai->ai_family = AF_INET;
	ai->ai_socktype = hints ? hints->ai_socktype : SOCK_DGRAM;
	ai->ai_protocol = hints ? hints->ai_protocol : 0;
	ai->ai_addrlen = sizeof( *sin );
	ai->ai_addr = (struct sockaddr *)sin;
	*res = ai;
	return 0;
}

void freeaddrinfo( struct addrinfo *ai )
{
	free( ai );
}

/*
The engine's own crash handler is disabled (XASH_CRASHHANDLER=0). This minimal
one reports the signal, fault address and the program counter as an offset
from main(), which llvm-symbolizer maps back onto build/engine-boot/llvm-pie.elf,
then closes telemetry so the transcript ends with a BYE instead of silence.
*/
extern int main( int argc, char **argv );

extern int sceKernelGetModuleList( int *handles, size_t capacity, size_t *count );
extern int sceKernelGetModuleInfo( int handle, void *info );

/* Name the loaded module that contains an address and the offset inside it
   (module info layout measured in the PRX spike: size 0x160, name at +8,
   segments {addr,size,prot} x4 at +0x108, count at +0x148). */
static void PS5_DescribeAddress( uintptr_t address, char *out, size_t size )
{
	int handles[128];
	size_t count = 0, i;
	int rc;
	out[0] = 0;
	rc = sceKernelGetModuleList( handles, sizeof( handles ) / sizeof( handles[0] ), &count );
	if( rc != 0 )
	{
		snprintf( out, size, "modulelist_rc=0x%x", (unsigned)rc );
		return;
	}
	if( count > sizeof( handles ) / sizeof( handles[0] ))
		count = sizeof( handles ) / sizeof( handles[0] );
	for( i = 0; i < count; i++ )
	{
		unsigned char info[0x160];
		unsigned int segments, s;
		memset( info, 0, sizeof( info ));
		*(unsigned long long *)info = sizeof( info );
		rc = sceKernelGetModuleInfo( handles[i], info );
		if( rc != 0 )
		{
			snprintf( out, size, "moduleinfo_rc=0x%x count=%u", (unsigned)rc, (unsigned)count );
			return;
		}
		segments = *(unsigned int *)( info + 0x148 );
		for( s = 0; s < segments && s < 4; s++ )
		{
			uintptr_t base = *(uintptr_t *)( info + 0x108 + s * 16 );
			unsigned int len = *(unsigned int *)( info + 0x108 + s * 16 + 8 );
			if( address >= base && address < base + len )
			{
				snprintf( out, size, "%s+0x%lx", (const char *)( info + 8 ), (unsigned long)( address - base ));
				return;
			}
		}
	}
}

static void PS5_FatalSignal( int signal, siginfo_t *info, void *context )
{
	ucontext_t *uc = (ucontext_t *)context;
	uintptr_t pc = uc ? (uintptr_t)uc->uc_mcontext.mc_rip : 0;
	uintptr_t base = (uintptr_t)&main;
	uintptr_t anchor = 0;
	uintptr_t sp = uc ? (uintptr_t)uc->uc_mcontext.mc_rsp : 0;
	uintptr_t here = (uintptr_t)&anchor;
	/* The kernel does not always populate mc_rsp for a SEGV; fall back to the
	   handler's own frame, which is on the faulting thread's stack. */
	uintptr_t *scan = ( sp && sp > here - 0x200000 && sp < here + 0x200000 )
		? (uintptr_t *)sp : &anchor;
	/* eboot.bin runtime span (from XASH_MODULES seg0..seg3); return addresses
	   into it are reported as offsets from main so llvm-symbolizer can resolve
	   them against build/engine-boot/llvm-pie.elf. */
	const uintptr_t img_lo = 0x400000, img_hi = 0x2600000;
	char trace[900], where[160];
	size_t used = 0;
	int i, found = 0;
	PS5_DescribeAddress( pc, where, sizeof( where ));
	(void)ps5log_printf( PS5LOG_WARN,
		"XASH_SIGNAL sig=%d code=%d addr=%p pc=%p in=%s main=%p pc_minus_main=%ld",
		signal, info ? info->si_code : 0, info ? info->si_addr : NULL,
		(void *)pc, where[0] ? where : "?", (void *)base, (long)( pc - base ));
	if( uc )
		(void)ps5log_printf( PS5LOG_WARN,
			"XASH_SIGNAL_REGS rsp=%p rbp=%p rdi=%p rsi=%p rax=%p rbx=%p handler_stack=%p thread=%p",
			(void *)uc->uc_mcontext.mc_rsp, (void *)uc->uc_mcontext.mc_rbp, (void *)uc->uc_mcontext.mc_rdi,
			(void *)uc->uc_mcontext.mc_rsi, (void *)uc->uc_mcontext.mc_rax, (void *)uc->uc_mcontext.mc_rbx,
			(void *)&anchor, (void *)pthread_self( ));
	/* No frame pointers at -O2: walk up from the handler's own frame and
	   report every word that points into this executable, relative to main(). */
	trace[0] = 0;
	for( i = 0; i < 16384 && found < 32; i++ )
	{
		uintptr_t word = scan[i];
		if( word >= img_lo && word < img_hi )
		{
			int n = snprintf( trace + used, sizeof( trace ) - used, "%s%ld", found ? "," : "", (long)( word - base ));
			if( n < 0 || (size_t)n >= sizeof( trace ) - used )
				break;
			used += (size_t)n;
			found++;
		}
	}
	(void)ps5log_printf( PS5LOG_WARN, "XASH_SIGNAL_STACK words_minus_main=%s", trace );
	ps5log_close( "xash-engine-boot-crashed" );
	_exit( 1 );
}

void PS5_LogModuleMap( void )
{
	int handles[128];
	size_t count = 0, i;
	int rc = sceKernelGetModuleList( handles, sizeof( handles ) / sizeof( handles[0] ), &count );
	(void)ps5log_printf( PS5LOG_INFO, "XASH_MODULES rc=0x%x count=%u", (unsigned)rc, (unsigned)count );
	if( rc != 0 )
		return;
	if( count > sizeof( handles ) / sizeof( handles[0] ))
		count = sizeof( handles ) / sizeof( handles[0] );
	for( i = 0; i < count; i++ )
	{
		unsigned char info[0x160];
		unsigned int segments, s;
		char line[300];
		size_t used;
		memset( info, 0, sizeof( info ));
		*(unsigned long long *)info = sizeof( info );
		rc = sceKernelGetModuleInfo( handles[i], info );
		if( rc != 0 )
		{
			(void)ps5log_printf( PS5LOG_INFO, "XASH_MODULE handle=%d info_rc=0x%x", handles[i], (unsigned)rc );
			continue;
		}
		segments = *(unsigned int *)( info + 0x148 );
		used = (size_t)snprintf( line, sizeof( line ), "XASH_MODULE handle=%d name=%s", handles[i], (const char *)( info + 8 ));
		for( s = 0; s < segments && s < 4 && used < sizeof( line ) - 40; s++ )
			used += (size_t)snprintf( line + used, sizeof( line ) - used, " seg%u=%p+0x%x",
				s, (void *)*(uintptr_t *)( info + 0x108 + s * 16 ), *(unsigned int *)( info + 0x108 + s * 16 + 8 ));
		(void)ps5log_line( PS5LOG_INFO, line );
	}
}

void Sys_SetupCrashHandler( const char *argv0 )
{
	static const int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS };
	struct sigaction act;
	size_t i;
	(void)argv0;
	memset( &act, 0, sizeof( act ));
	act.sa_sigaction = PS5_FatalSignal;
	act.sa_flags = SA_SIGINFO | SA_RESETHAND;
	for( i = 0; i < sizeof( signals ) / sizeof( signals[0] ); i++ )
		sigaction( signals[i], &act, NULL );
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

/* XASH_MESSAGEBOX=99 keeps sys_con.c's stderr box out; fatal errors go to
   the structured transcript, independent of how libc routes stdio. */
void Platform_MessageBox( const char *title, const char *message, qboolean parentMainWindow )
{
	char flat[512];
	size_t i;
	(void)parentMainWindow;
	Q_strncpy( flat, message, sizeof( flat ));
	for( i = 0; flat[i]; i++ )
		if( flat[i] == '\n' || flat[i] == '\t' ) flat[i] = ' ';
	(void)ps5log_printf( PS5LOG_WARN, "XASH_MESSAGEBOX title=\"%s\" message=\"%s\"", title, flat );
	fprintf( stderr, "%s: %s\n", title, message );
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

#if PS5_XASH_PAD_GATE
	(void)PS5_PadInputPoll( );
	if( PS5_PadInputGatePassed( ))
	{
		quit_queued = true;
		Con_Printf( "XASH_PAD_GATE_PASS action=quit\n" );
		Cbuf_AddText( "quit\n" );
		return;
	}
#endif

#if PS5_XASH_GATE_SECONDS > 0
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
	struct timespec request, remaining;
	if( msec <= 0 )
		return;
	request.tv_sec = msec / 1000;
	request.tv_nsec = (long)( msec % 1000 ) * 1000000l;
	while( nanosleep( &request, &remaining ) != 0 && errno == EINTR )
		request = remaining;
}
