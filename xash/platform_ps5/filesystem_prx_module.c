/* filesystem_stdio application-owned PRX lifecycle. SPDX-License-Identifier: GPL-3.0-or-later. */
#include "prx_loader_ps5.h"
#include "filesystem_internal.h"

#include <stddef.h>

void PS5_SetCwd( const char *dir );
int PS5_LoadDirIndex( const char *image_root, const char *index_path );
void PS5_UnloadDirIndex( void );
int PS5_DirIndexCount( void );
int PS5_ListingRefusedCount( void );
extern void GetFSAPI( void );
extern void CreateInterface( void );

static int filesystem_prx_started;
static int filesystem_prx_index_entries;

qboolean PS5_FilesystemPrxOriginalInitStdio( qboolean unused_set_to_true,
	const char *rootdir, const char *basedir, const char *gamedir,
	const char *rodir );
void PS5_FilesystemPrxOriginalLoadGameInfo( uint32_t flags,
	const char *language );

static unsigned long long filesystem_prx_hash( const byte *data, size_t bytes )
{
	unsigned long long hash = 1469598103934665603ull;
	size_t i;
	for( i = 0; i < bytes; ++i )
	{
		hash ^= data[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

qboolean PS5_FilesystemPrxInitStdio( qboolean unused_set_to_true,
	const char *rootdir, const char *basedir, const char *gamedir,
	const char *rodir )
{
	return PS5_FilesystemPrxOriginalInitStdio( unused_set_to_true,
		rootdir, basedir, gamedir, rodir );
}

void PS5_FilesystemPrxLoadGameInfo( uint32_t flags, const char *language )
{
	static const char palette_path[] = "GfX/PaLeTtE.LmP";
	static const char large_path[] = "maps/c1a0.bsp";
	search_t *listing;
	byte *palette = NULL, *large = NULL;
	fs_offset_t palette_bytes = 0, large_bytes = 0;
	unsigned long long palette_hash = 0, large_hash = 0;
	int listing_matches = 0;
	qboolean result;
	PS5_FilesystemPrxOriginalLoadGameInfo( flags, language );

	listing = FS_Search( "gfx/*", true, true );
	if( listing ) listing_matches = listing->numfilenames;
	palette = FS_LoadFile( palette_path, &palette_bytes, true );
	if( palette ) palette_hash = filesystem_prx_hash( palette,
		(size_t)palette_bytes );
	large = FS_LoadFile( large_path, &large_bytes, true );
	if( large ) large_hash = filesystem_prx_hash( large,
		(size_t)large_bytes );

	result = listing_matches > 0 && palette && palette_bytes == 768 &&
		large && large_bytes >= 1048576;
	Con_Printf( "XASH_FS_PRX_PROBE schema=1 index_entries=%d "
		"listing_pattern=gfx/* listing_matches=%d case_path=%s "
		"palette_bytes=%lld palette_hash=%016llx large_path=%s "
		"large_bytes=%lld large_hash=%016llx pass=%d\n",
		filesystem_prx_index_entries, listing_matches, palette_path,
		(long long)palette_bytes, palette_hash, large_path,
		(long long)large_bytes, large_hash, result ? 1 : 0 );
	if( listing ) Mem_Free( listing );
	if( palette ) Mem_Free( palette );
	if( large ) Mem_Free( large );
	if( !result )
		Sys_Error( "filesystem_stdio PRX workload probe failed" );
}

/* libSceLibcInternal does not provide FreeBSD's assert destination. Keep the
 * module self-contained; an assertion is unrecoverable and must never jump
 * through an unresolved firmware import. */
__attribute__((noreturn))
void __assert( const char *function, const char *file, int line,
	const char *expression )
{
	(void)function; (void)file; (void)line; (void)expression;
	__builtin_trap( );
}

int PS5_FilesystemPrxIndexCount( void )
{
	return filesystem_prx_index_entries;
}

int PS5_FilesystemPrxAllocatorContractResult( void )
{
	/* LoadFileMalloc is explicitly a libc-owned ABI.  The host may free its
	 * result after this function returns, so this PRX must use the process
	 * libc rather than a module-private arena for malloc/free. */
	return filesystem_prx_started && filesystem_prx_index_entries > 0 ? 0 : -1;
}

int PS5_FilesystemPrxListingRefusedCount( void )
{
	return PS5_ListingRefusedCount( );
}

int module_start( size_t argc, const void *argv )
{
	(void)argc; (void)argv;
	PS5_SetCwd( "/app0/xash3d" );
	filesystem_prx_index_entries = PS5_LoadDirIndex( "/app0/xash3d",
		"/app0/xash3d/.dirindex" );
	if( filesystem_prx_index_entries <= 0 )
		return -1;
	filesystem_prx_started = 1;
	return 0;
}

int module_stop( size_t argc, const void *argv )
{
	(void)argc; (void)argv;
	PS5_UnloadDirIndex( );
	filesystem_prx_index_entries = 0;
	filesystem_prx_started = 0;
	return 0;
}

PS5_PRX_DEFINE_DESCRIPTOR( filesystem_stdio_prx_exports,
	PS5_PRX_EXPORT( GetFSAPI ),
	PS5_PRX_EXPORT( CreateInterface ),
	PS5_PRX_EXPORT( PS5_FilesystemPrxIndexCount ),
	PS5_PRX_EXPORT( PS5_FilesystemPrxAllocatorContractResult ),
	PS5_PRX_EXPORT( PS5_FilesystemPrxListingRefusedCount ),
	PS5_PRX_EXPORT( filesystem_prx_started ),
	PS5_PRX_EXPORT( module_start ),
	PS5_PRX_EXPORT( module_stop ));
