#include <assert.h>
#include <pwd.h>
#include <string.h>

#include "libc_shims_ps5.h"

int ps5log_line( const char *level, const char *text )
{
	(void)level;
	(void)text;
	return 0;
}

void ps5log_close( const char *reason )
{
	(void)reason;
}

int main( void )
{
	Ps5LibcShimProbe probe;
	char tiny[8];
	struct passwd *pw;

	assert( PS5_LibcShimProbe( 0xffu, &probe ) == 0 );
	assert( probe.assert_format_pass && probe.identity_pass &&
		probe.dladdr_pass );
	assert( probe.requested_uid == 0xffu && probe.returned_uid == 0xffu );
	assert( strcmp( probe.username, "ps5" ) == 0 );
	assert( strstr( probe.assert_message, "function ShimProbe" ) != NULL );
	assert( PS5_LibcShimProbe( 0u, &probe ) == 0 );
	pw = getpwuid( 42u );
	assert( pw && pw->pw_uid == 42u && strcmp( pw->pw_name, "ps5" ) == 0 );
	assert( PS5_FormatAssertMessage( tiny, sizeof( tiny ), "f", "p", 1,
		"x" ) == -1 );
	assert( tiny[sizeof( tiny ) - 1u] == '\0' );
	assert( PS5_FormatAssertMessage( NULL, 0u, NULL, NULL, 0, NULL ) == -1 );
	return 0;
}
