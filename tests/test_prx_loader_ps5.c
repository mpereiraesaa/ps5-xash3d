/* Host contract for the Phase 6 PRX loader. SPDX-License-Identifier: GPL-3.0-or-later. */
#include "prx_loader_ps5.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if( !(x) ) { fprintf( stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x ); exit( 1 ); } } while( 0 )

static unsigned char code[64];
static struct
{
	char names[3][32];
	unsigned char alignment_skew[3];
	ps5_prx_descriptor_header_t header __attribute__((aligned(16)));
	ps5_prx_export_t exports[2];
} rodata;
static uint32_t data[8];
static ps5_prx_segment_t segments[3];

static void BuildImage( void )
{
	memset( &rodata, 0, sizeof( rodata ));
	strcpy( rodata.names[0], "probe_add" );
	strcpy( rodata.names[1], "probe_version" );
	rodata.header.magic = PS5_PRX_DESCRIPTOR_MAGIC;
	rodata.header.version = PS5_PRX_DESCRIPTOR_VERSION;
	rodata.header.count = 2;
	rodata.exports[0].name = rodata.names[0];
	rodata.exports[0].address = code;
	rodata.exports[1].name = rodata.names[1];
	rodata.exports[1].address = &data[1];
	segments[0] = (ps5_prx_segment_t){ code, sizeof( code ), 5 };
	segments[1] = (ps5_prx_segment_t){ &rodata, sizeof( rodata ), 1 };
	segments[2] = (ps5_prx_segment_t){ data, sizeof( data ), 3 };
}

static void TestDescriptor( void )
{
	const ps5_prx_descriptor_t *descriptor = NULL;
	BuildImage( );
	CHECK( PS5_PrxFindDescriptor( segments, 3, &descriptor ) == PS5_PRX_OK );
	CHECK( descriptor == (const ps5_prx_descriptor_t *)&rodata.header );
	CHECK( PS5_PrxDescriptorLookup( descriptor, "probe_add" ) == code );
	CHECK( PS5_PrxDescriptorLookup( descriptor, "missing" ) == NULL );
	CHECK( PS5_PrxDescriptorLookup( descriptor, NULL ) == NULL );

	BuildImage( ); rodata.header.version++;
	CHECK( PS5_PrxFindDescriptor( segments, 3, &descriptor ) == PS5_PRX_ERROR_NO_DESCRIPTOR );
	BuildImage( ); rodata.exports[1].address = (void *)(uintptr_t)0x10;
	CHECK( PS5_PrxFindDescriptor( segments, 3, &descriptor ) == PS5_PRX_ERROR_NO_DESCRIPTOR );
	BuildImage( ); rodata.exports[0].name = "outside";
	CHECK( PS5_PrxFindDescriptor( segments, 3, &descriptor ) == PS5_PRX_ERROR_NO_DESCRIPTOR );
	BuildImage( ); rodata.header.count = 4000;
	CHECK( PS5_PrxFindDescriptor( segments, 3, &descriptor ) == PS5_PRX_ERROR_NO_DESCRIPTOR );
	BuildImage( ); segments[1].protection = 0;
	CHECK( PS5_PrxFindDescriptor( segments, 3, &descriptor ) == PS5_PRX_ERROR_NO_DESCRIPTOR );
	BuildImage( ); rodata.exports[1].name = rodata.names[0];
	CHECK( PS5_PrxFindDescriptor( segments, 3, &descriptor ) == PS5_PRX_ERROR_NO_DESCRIPTOR );
	CHECK( PS5_PrxFindDescriptor( NULL, 3, &descriptor ) == PS5_PRX_ERROR_ARGUMENT );
}

static void FillInfo( unsigned char *info )
{
	uint64_t size = PS5_PRX_MODULE_INFO_BYTES;
	uint32_t count = 3, i;
	memset( info, 0, PS5_PRX_MODULE_INFO_BYTES );
	memcpy( info, &size, sizeof( size ));
	strcpy((char *)info + 8, "xash_prx_probe.prx" );
	for( i = 0; i < count; ++i )
	{
		uintptr_t address = (uintptr_t)segments[i].address;
		memcpy( info + 0x108 + i * 16, &address, sizeof( address ));
		memcpy( info + 0x108 + i * 16 + 8, &segments[i].size, 4 );
		memcpy( info + 0x108 + i * 16 + 12, &segments[i].protection, 4 );
	}
	memcpy( info + 0x148, &count, sizeof( count ));
}

static void TestInfo( void )
{
	unsigned char info[PS5_PRX_MODULE_INFO_BYTES];
	char name[64];
	ps5_prx_segment_t parsed[PS5_PRX_MAX_SEGMENTS];
	uint32_t count = 0;
	BuildImage( ); FillInfo( info );
	CHECK( PS5_PrxParseModuleInfo( info, name, sizeof( name ), parsed, &count ) == PS5_PRX_OK );
	CHECK( !strcmp( name, "xash_prx_probe.prx" ) && count == 3 );
	memset( info, 0, sizeof( uint64_t ));
	CHECK( PS5_PrxParseModuleInfo( info, name, sizeof( name ), parsed, &count ) == PS5_PRX_OK );
	FillInfo( info );
	info[0] = 0x50;
	CHECK( PS5_PrxParseModuleInfo( info, name, sizeof( name ), parsed, &count ) == PS5_PRX_ERROR_MODULE_INFO );
	FillInfo( info ); memset( info + 8, 'x', 256 );
	CHECK( PS5_PrxParseModuleInfo( info, name, sizeof( name ), parsed, &count ) == PS5_PRX_ERROR_MODULE_INFO );
}

static int load_calls, info_calls, unload_calls, start_result, stop_result;
static int fail_info, hide_descriptor, fail_unload;
static int32_t FakeLoad( const char *path, size_t argc, const void *argv,
	uint32_t flags, const void *option, int *result )
{
	(void)argc; (void)argv; (void)flags; (void)option;
	++load_calls; *result = start_result;
	return !strcmp( path, "/app0/sce_module/xash_prx_probe.prx" ) ? 0xd0 : -1;
}
static int FakeInfo( int32_t handle, void *info )
{
	CHECK( handle == 0xd0 ); ++info_calls;
	if( fail_info ) return -1;
	FillInfo( info );
	if( hide_descriptor ) rodata.header.magic = 0;
	return 0;
}
static int FakeUnload( int32_t handle, size_t argc, const void *argv,
	uint32_t flags, const void *option, int *result )
{
	(void)argc; (void)argv; (void)flags; (void)option;
	CHECK( handle == 0xd0 ); ++unload_calls; *result = stop_result;
	return fail_unload ? -1 : 0;
}

static void ResetFakes( void )
{
	load_calls = info_calls = unload_calls = 0;
	start_result = stop_result = fail_info = hide_descriptor = fail_unload = 0;
	BuildImage( );
}

static void TestLifecycle( void )
{
	const ps5_prx_loader_ops_t ops = { FakeLoad, FakeInfo, FakeUnload };
	ps5_prx_module_t module;
	ResetFakes( );
	CHECK( PS5_PrxLoad( &module, "/app0/sce_module/xash_prx_probe.prx", &ops ) == PS5_PRX_OK );
	CHECK( module.handle == 0xd0 && !strcmp( module.name, "xash_prx_probe.prx" ));
	CHECK( PS5_PrxGetProc( &module, "probe_add" ) == code );
	CHECK( !strcmp( PS5_PrxNameForAddress( &module, code ), "probe_add" ));
	CHECK( PS5_PrxUnload( &module, &ops ) == PS5_PRX_OK && module.handle == 0 );
	CHECK( load_calls == 1 && info_calls == 1 && unload_calls == 1 );

	ResetFakes( ); start_result = 7;
	CHECK( PS5_PrxLoad( &module, "/app0/sce_module/xash_prx_probe.prx", &ops ) == PS5_PRX_ERROR_START );
	CHECK( module.handle == 0 && info_calls == 0 && unload_calls == 1 );
	ResetFakes( ); fail_info = 1;
	CHECK( PS5_PrxLoad( &module, "/app0/sce_module/xash_prx_probe.prx", &ops ) == PS5_PRX_ERROR_MODULE_INFO );
	CHECK( module.handle == 0 && unload_calls == 1 );
	ResetFakes( ); hide_descriptor = 1;
	CHECK( PS5_PrxLoad( &module, "/app0/sce_module/xash_prx_probe.prx", &ops ) == PS5_PRX_ERROR_NO_DESCRIPTOR );
	CHECK( module.handle == 0 && unload_calls == 1 );
	ResetFakes( ); hide_descriptor = 1; fail_unload = 1;
	CHECK( PS5_PrxLoad( &module, "/app0/sce_module/xash_prx_probe.prx", &ops ) == PS5_PRX_ERROR_ROLLBACK );
	CHECK( module.handle == 0xd0 && module.load_error == PS5_PRX_ERROR_NO_DESCRIPTOR );
	CHECK( module.rollback_result == -1 && unload_calls == 1 );
	fail_unload = 0;
	CHECK( PS5_PrxUnload( &module, &ops ) == PS5_PRX_OK && module.handle == 0 );
	ResetFakes( );
	CHECK( PS5_PrxLoad( &module, "/app0/sce_module/xash_prx_probe.prx", &ops ) == PS5_PRX_OK );
	fail_unload = 1;
	CHECK( PS5_PrxUnload( &module, &ops ) == PS5_PRX_ERROR_UNLOAD && module.handle == 0xd0 );
	fail_unload = 0; stop_result = 3;
	CHECK( PS5_PrxUnload( &module, &ops ) == PS5_PRX_ERROR_UNLOAD && module.handle == 0xd0 );
	stop_result = 0;
	CHECK( PS5_PrxUnload( &module, &ops ) == PS5_PRX_OK );
}

static int macro_fn( void ) { return 1; }
static const uint32_t macro_data = 2;
PS5_PRX_DEFINE_DESCRIPTOR( macro_descriptor,
	PS5_PRX_EXPORT( macro_fn ), PS5_PRX_EXPORT( macro_data ));

int main( void )
{
	TestDescriptor( ); TestInfo( ); TestLifecycle( );
	CHECK( macro_descriptor.header.count == 2 );
	CHECK(((uintptr_t)&macro_descriptor % PS5_PRX_DESCRIPTOR_ALIGNMENT ) == 0 );
	puts( "prx loader PS5 tests passed" );
	return 0;
}
