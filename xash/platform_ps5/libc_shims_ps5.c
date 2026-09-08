/*
libc_shims_ps5.c - project-owned PS5 replacements for unavailable POSIX APIs
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include "libc_shims_ps5.h"
#include "ps5log.h"

#include <dlfcn.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char ps5_identity_name[] = "ps5";
static char ps5_identity_root[] = "/";
static struct passwd ps5_identity;

int PS5_FormatAssertMessage( char *out, size_t size, const char *func,
	const char *file, int line, const char *expr )
{
	int written;
	if( !out || size == 0u )
		return -1;
	written = snprintf( out, size,
		"Assertion failed: (%s), function %s, file %s, line %d.",
		expr ? expr : "?", func ? func : "?", file ? file : "?", line );
	if( written < 0 || (size_t)written >= size )
	{
		out[size - 1u] = '\0';
		return -1;
	}
	return written;
}

/* FreeBSD libc assert() destination; libSceLibcInternal does not export it. */
__attribute__((noreturn))
void __assert( const char *func, const char *file, int line, const char *expr )
{
	char message[512];
	if( PS5_FormatAssertMessage( message, sizeof( message ), func, file, line,
		expr ) < 0 )
		(void)snprintf( message, sizeof( message ),
			"Assertion failed at %s:%d.", file ? file : "?", line );
	(void)ps5log_line( PS5LOG_ERR, "XASH_ASSERT_FAILURE schema=1 source=project-owned" );
	(void)ps5log_line( PS5LOG_ERR, message );
	ps5log_close( "xash-assert-failed" );
	abort( );
}

/* The engine only consumes pw_name to salt its non-security machine id. */
struct passwd *getpwuid( uid_t uid )
{
	memset( &ps5_identity, 0, sizeof( ps5_identity ));
	ps5_identity.pw_name = ps5_identity_name;
	ps5_identity.pw_dir = ps5_identity_root;
	ps5_identity.pw_shell = ps5_identity_root;
	ps5_identity.pw_uid = uid;
	return &ps5_identity;
}

/* whereami treats zero as a request to fall back to the supplied argv[0]. */
int dladdr( const void *addr, Dl_info *info )
{
	(void)addr;
	memset( info, 0, sizeof( *info ));
	return 0;
}

int PS5_LibcShimProbe( uid_t uid, Ps5LibcShimProbe *probe )
{
	struct passwd *pw;
	Dl_info info;
	int dl_rc;
	int formatted;
	if( !probe )
		return -1;
	memset( probe, 0, sizeof( *probe ));
	probe->requested_uid = uid;
	formatted = PS5_FormatAssertMessage( probe->assert_message,
		sizeof( probe->assert_message ), "ShimProbe", "libc_shims_ps5.c", 73,
		"value != 0" );
	probe->assert_format_pass = formatted > 0 &&
		strcmp( probe->assert_message,
			"Assertion failed: (value != 0), function ShimProbe, file libc_shims_ps5.c, line 73." ) == 0;

	pw = getpwuid( uid );
	if( pw )
	{
		probe->returned_uid = pw->pw_uid;
		(void)snprintf( probe->username, sizeof( probe->username ), "%s",
			pw->pw_name ? pw->pw_name : "" );
		probe->identity_pass = pw->pw_uid == uid &&
			strcmp( probe->username, "ps5" ) == 0 && pw->pw_dir &&
			strcmp( pw->pw_dir, "/" ) == 0;
	}

	memset( &info, 0xa5, sizeof( info ));
	dl_rc = dladdr( (const void *)&PS5_LibcShimProbe, &info );
	probe->dladdr_pass = dl_rc == 0 && info.dli_fname == NULL &&
		info.dli_fbase == NULL && info.dli_sname == NULL &&
		info.dli_saddr == NULL;
	return probe->assert_format_pass && probe->identity_pass &&
		probe->dladdr_pass ? 0 : -1;
}
