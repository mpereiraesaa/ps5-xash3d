/* Minimal Phase 6 PRX loader probe. SPDX-License-Identifier: GPL-3.0-or-later. */
#include "prx_loader_ps5.h"

#include <stddef.h>
#include <stdint.h>

int sceKernelUsleep( unsigned int microseconds );

static uint32_t probe_calls;
uint32_t xash_prx_probe_started;
const uint32_t xash_prx_probe_version = 0x00010000u;

int xash_prx_probe_add( int left, int right )
{
	++probe_calls;
	return left + right;
}

uint32_t xash_prx_probe_sleep_count( unsigned int microseconds )
{
	if( microseconds ) (void)sceKernelUsleep( microseconds );
	return ++probe_calls;
}

int module_start( size_t argc, const void *argv )
{
	(void)argc; (void)argv;
	xash_prx_probe_started = 1;
	return 0;
}

int module_stop( size_t argc, const void *argv )
{
	(void)argc; (void)argv;
	return 0;
}

PS5_PRX_DEFINE_DESCRIPTOR( xash_prx_probe_exports,
	PS5_PRX_EXPORT( xash_prx_probe_add ),
	PS5_PRX_EXPORT( xash_prx_probe_sleep_count ),
	PS5_PRX_EXPORT( xash_prx_probe_version ),
	PS5_PRX_EXPORT( xash_prx_probe_started ),
	PS5_PRX_EXPORT( module_start ),
	PS5_PRX_EXPORT( module_stop ));
