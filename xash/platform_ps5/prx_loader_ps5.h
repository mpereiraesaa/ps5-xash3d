/*
prx_loader_ps5.h - Application-owned PRX loader contract for PS5
Copyright (C) 2026 BlackBearReloaded
Copyright (C) 2026 Manuel Pereira
SPDX-License-Identifier: GPL-3.0-or-later

The descriptor ABI and firmware-facing flow originate in the hardware-proven
ps5-native-app-boilerplate exp/prx-module work.  The implementation lives in
prx_loader_ps5.c so the engine, modules and host tests share one contract.
*/

#ifndef XASH_PRX_LOADER_PS5_H
#define XASH_PRX_LOADER_PS5_H

#include <stddef.h>
#include <stdint.h>

#define PS5_PRX_DESCRIPTOR_MAGIC UINT64_C(0x3143534544585250)
#define PS5_PRX_DESCRIPTOR_VERSION 1u
#define PS5_PRX_DESCRIPTOR_ALIGNMENT 16u
#define PS5_PRX_MAX_EXPORTS 4096u
#define PS5_PRX_MAX_SEGMENTS 4u
#define PS5_PRX_MODULE_INFO_BYTES 0x160u

typedef struct ps5_prx_export_s
{
	const char *name;
	const void *address;
} ps5_prx_export_t;

typedef struct ps5_prx_descriptor_header_s
{
	uint64_t magic;
	uint32_t version;
	uint32_t count;
} ps5_prx_descriptor_header_t;

typedef struct ps5_prx_descriptor_s
{
	ps5_prx_descriptor_header_t header;
	ps5_prx_export_t exports[1];
} ps5_prx_descriptor_t;

#define PS5_PRX_EXPORT(symbol) { #symbol, (const void *)&(symbol) }
#define PS5_PRX_DEFINE_DESCRIPTOR(symbol, ...) \
	__attribute__((used, aligned(PS5_PRX_DESCRIPTOR_ALIGNMENT))) const struct \
	{ \
		ps5_prx_descriptor_header_t header; \
		ps5_prx_export_t exports[sizeof((ps5_prx_export_t[]){__VA_ARGS__}) / \
			sizeof(ps5_prx_export_t)]; \
	} symbol = { { PS5_PRX_DESCRIPTOR_MAGIC, PS5_PRX_DESCRIPTOR_VERSION, \
		(uint32_t)(sizeof((ps5_prx_export_t[]){__VA_ARGS__}) / \
			sizeof(ps5_prx_export_t)) }, { __VA_ARGS__ } }

typedef struct ps5_prx_segment_s
{
	const void *address;
	uint32_t size;
	uint32_t protection;
} ps5_prx_segment_t;

typedef struct ps5_prx_loader_ops_s
{
	int32_t (*load_start)( const char *path, size_t argc, const void *argv,
		uint32_t flags, const void *option, int *result );
	int (*module_info)( int32_t handle, void *info );
	int (*stop_unload)( int32_t handle, size_t argc, const void *argv,
		uint32_t flags, const void *option, int *result );
} ps5_prx_loader_ops_t;

typedef struct ps5_prx_module_s
{
	int32_t handle;
	int32_t loaded_handle;
	int start_result;
	int stop_result;
	int rollback_result;
	int rollback_stop_result;
	int load_error;
	int module_info_result;
	uint64_t module_info_size;
	uint32_t module_info_segment_count;
	char name[64];
	uint32_t segment_count;
	ps5_prx_segment_t segments[PS5_PRX_MAX_SEGMENTS];
	const ps5_prx_descriptor_t *descriptor;
} ps5_prx_module_t;

enum ps5_prx_result
{
	PS5_PRX_OK = 0,
	PS5_PRX_ERROR_ARGUMENT = -1,
	PS5_PRX_ERROR_LOAD = -2,
	PS5_PRX_ERROR_START = -3,
	PS5_PRX_ERROR_MODULE_INFO = -4,
	PS5_PRX_ERROR_NO_DESCRIPTOR = -5,
	PS5_PRX_ERROR_UNLOAD = -6,
	PS5_PRX_ERROR_ROLLBACK = -7,
};

int PS5_PrxParseModuleInfo( const void *info, char *name, size_t name_capacity,
	ps5_prx_segment_t *segments, uint32_t *segment_count );
int PS5_PrxFindDescriptor( const ps5_prx_segment_t *segments,
	uint32_t segment_count, const ps5_prx_descriptor_t **descriptor );
const void *PS5_PrxDescriptorLookup( const ps5_prx_descriptor_t *descriptor,
	const char *name );
int PS5_PrxLoad( ps5_prx_module_t *module, const char *path,
	const ps5_prx_loader_ops_t *ops );
const void *PS5_PrxGetProc( const ps5_prx_module_t *module, const char *name );
const char *PS5_PrxNameForAddress( const ps5_prx_module_t *module,
	const void *address );
int PS5_PrxUnload( ps5_prx_module_t *module,
	const ps5_prx_loader_ops_t *ops );
const char *PS5_PrxResultString( int result );

#if defined(XASH_PS5)
const ps5_prx_loader_ops_t *PS5_PrxNativeOps( void );
#endif

#endif
