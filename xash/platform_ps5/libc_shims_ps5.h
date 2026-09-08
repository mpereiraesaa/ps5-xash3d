#ifndef XASH_PLATFORM_PS5_LIBC_SHIMS_H
#define XASH_PLATFORM_PS5_LIBC_SHIMS_H

#include <stddef.h>
#include <sys/types.h>

typedef struct Ps5LibcShimProbe
{
	unsigned int assert_format_pass;
	unsigned int identity_pass;
	unsigned int dladdr_pass;
	uid_t requested_uid;
	uid_t returned_uid;
	char username[16];
	char assert_message[192];
} Ps5LibcShimProbe;

int PS5_FormatAssertMessage( char *out, size_t size, const char *func,
	const char *file, int line, const char *expr );
int PS5_LibcShimProbe( uid_t uid, Ps5LibcShimProbe *probe );

#endif
