#!/usr/bin/env python3
"""Generate guarded trace copies of pinned Xash3D filesystem sources.

The public submodule remains clean.  This transformer intentionally fails if
the upstream source anchors move, so a trace binary can always be reproduced
from the recorded submodule commit instead of depending on ad-hoc edits.
"""

from __future__ import annotations

import argparse
from pathlib import Path


TRACE_SUPPORT = r'''
#ifdef PS5_XASH_FS_TRACE
#define PS5_FS_TRACE_GUARD_BYTES 64
extern int ps5log_printf( const char *level, const char *fmt, ... );
static volatile unsigned char *ps5_fs_trace_guard;
static size_t ps5_fs_trace_guard_size;
static const char *ps5_fs_trace_path;
static int ps5_fs_trace_active;

static unsigned char PS5_FSTraceGuardByte( size_t index )
{
	return (unsigned char)( 0xa5u ^ (unsigned char)( index * 29u ));
}

static void PS5_FSTraceGuardBegin( const char *path, volatile unsigned char *guard, size_t size )
{
	size_t i;
	ps5_fs_trace_active = path && !Q_stricmp( path, "gfx/palette.lmp" );
	ps5_fs_trace_guard = guard;
	ps5_fs_trace_guard_size = size;
	ps5_fs_trace_path = path;
	for( i = 0; i < size; i++ )
		guard[i] = PS5_FSTraceGuardByte( i );
	if( ps5_fs_trace_active )
		(void)ps5log_printf( "INFO", "XASH_FS_GUARD event=begin path=%s bytes=%zu", path, size );
}

int PS5_FSTraceGuardActive( void )
{
	return ps5_fs_trace_active;
}

int PS5_FSTraceGuardCheck( const char *stage, const void *search, int result )
{
	size_t i;
	if( !ps5_fs_trace_active )
		return 1;
	for( i = 0; i < ps5_fs_trace_guard_size; i++ )
	{
		unsigned char expected = PS5_FSTraceGuardByte( i );
		unsigned char observed = ps5_fs_trace_guard[i];
		if( observed != expected )
		{
			(void)ps5log_printf( "ERROR", "XASH_FS_GUARD event=corrupt path=%s stage=%s search=%p result=%d offset=%zu expected=%u observed=%u",
				ps5_fs_trace_path, stage, search, result, i, (unsigned)expected, (unsigned)observed );
			return 0;
		}
	}
	(void)ps5log_printf( "INFO", "XASH_FS_GUARD event=intact path=%s stage=%s search=%p result=%d",
		ps5_fs_trace_path, stage, search, result );
	return 1;
}

static void PS5_FSTraceGuardEnd( void )
{
	ps5_fs_trace_active = 0;
	ps5_fs_trace_guard = NULL;
	ps5_fs_trace_guard_size = 0;
	ps5_fs_trace_path = NULL;
}
#endif
'''


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise ValueError(f"{label}: expected one source anchor, found {count}")
    return source.replace(old, new, 1)


def instrument_io(source: str) -> str:
    source = replace_once(
        source,
        '#include "common/com_strings.h"\n',
        '#include "common/com_strings.h"\n' + TRACE_SUPPORT,
        "io trace support",
    )
    source = replace_once(
        source,
        'static byte *FS_LoadFile_( const char *path, fs_offset_t *filesizeptr, const qboolean gamedironly, const qboolean custom_alloc )\n{\n\tchar netpath[MAX_SYSPATH];',
        'static __attribute__((noinline)) byte *FS_LoadFile_( const char *path, fs_offset_t *filesizeptr, const qboolean gamedironly, const qboolean custom_alloc )\n{\n#ifdef PS5_XASH_FS_TRACE\n\tstruct { char bytes[MAX_SYSPATH]; volatile unsigned char guard[PS5_FS_TRACE_GUARD_BYTES]; } guarded_netpath;\n\tchar *netpath = guarded_netpath.bytes;\n#else\n\tchar netpath[MAX_SYSPATH];\n#endif',
        "FS_LoadFile_ guarded buffer",
    )
    source = replace_once(
        source,
        '\tsearch = FS_FindFile( path, &pack_ind, netpath, sizeof( netpath ), gamedironly ? FS_GAMEDIRONLY_SEARCH_FLAGS : 0 );',
        '#ifdef PS5_XASH_FS_TRACE\n\tPS5_FSTraceGuardBegin( path, guarded_netpath.guard, sizeof( guarded_netpath.guard ));\n\tsearch = FS_FindFile( path, &pack_ind, netpath, sizeof( guarded_netpath.bytes ), gamedironly ? FS_GAMEDIRONLY_SEARCH_FLAGS : 0 );\n\t(void)PS5_FSTraceGuardCheck( "after_findfile", search, search ? pack_ind : -1 );\n\tPS5_FSTraceGuardEnd( );\n#else\n\tsearch = FS_FindFile( path, &pack_ind, netpath, sizeof( netpath ), gamedironly ? FS_GAMEDIRONLY_SEARCH_FLAGS : 0 );\n#endif',
        "FS_LoadFile_ lookup",
    )
    source = replace_once(
        source,
        '\tbyte *buf;\n\n\t// custom load file function for compressed files',
        '\tbyte *buf;\n\n#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( path, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_FS_ARCHIVE event=enter path=%s search=%p type=%d load=%p open=%p",\n\t\t\tpath, sp, sp->type, sp->pfnLoadFile, sp->pfnOpenFile );\n#endif\n\n\t// custom load file function for compressed files',
        "FS_LoadFileFromArchive entry",
    )
    source = replace_once(
        source,
        '\tif( sp->pfnLoadFile )\n\t\treturn sp->pfnLoadFile( sp, path, pack_ind, filesizeptr, pfnAlloc, pfnFree );\n\n\tfile = sp->pfnOpenFile( sp, path, "rb", pack_ind );',
        '\tif( sp->pfnLoadFile )\n\t{\n#ifdef PS5_XASH_FS_TRACE\n\t\tif( !Q_stricmp( path, "gfx/palette.lmp" ))\n\t\t\t(void)ps5log_printf( "INFO", "XASH_FS_ARCHIVE event=call_load path=%s callback=%p", path, sp->pfnLoadFile );\n#endif\n\t\treturn sp->pfnLoadFile( sp, path, pack_ind, filesizeptr, pfnAlloc, pfnFree );\n\t}\n\n#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( path, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_FS_ARCHIVE event=call_open path=%s callback=%p", path, sp->pfnOpenFile );\n#endif\n\tfile = sp->pfnOpenFile( sp, path, "rb", pack_ind );\n#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( path, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_FS_ARCHIVE event=open_return path=%s file=%p", path, file );\n#endif',
        "FS_LoadFileFromArchive callbacks",
    )
    source = replace_once(
        source,
        '\tfilesize = file->real_length;\n\tbuf = (byte *)pfnAlloc( filesize + 1 );',
        '\tfilesize = file->real_length;\n#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( path, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_FS_ARCHIVE event=call_alloc path=%s handle=%d length=%lld position=%lld allocator=%p pool=%d",\n\t\t\tpath, file->handle, (long long)filesize, (long long)file->position, pfnAlloc, fs_mempool );\n#endif\n\tbuf = (byte *)pfnAlloc( filesize + 1 );\n#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( path, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_FS_ARCHIVE event=alloc_return path=%s bytes=%lld buffer=%p",\n\t\t\tpath, (long long)filesize + 1, buf );\n#endif',
        "FS_LoadFileFromArchive allocation",
    )
    source = replace_once(
        source,
        '\tbuf[filesize] = \'\\0\';\n\tFS_Read( file, buf, filesize );\n\tFS_Close( file );\n\tif( filesizeptr ) *filesizeptr = filesize;',
        '\tbuf[filesize] = \'\\0\';\n#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( path, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_FS_ARCHIVE event=call_read path=%s bytes=%lld buffer=%p",\n\t\t\tpath, (long long)filesize, buf );\n#endif\n\tFS_Read( file, buf, filesize );\n#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( path, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_FS_ARCHIVE event=read_return path=%s position=%lld",\n\t\t\tpath, (long long)file->position );\n#endif\n\tFS_Close( file );\n\tif( filesizeptr ) *filesizeptr = filesize;',
        "FS_LoadFileFromArchive read",
    )
    source = replace_once(
        source,
        '#endif\n\tFS_Close( file );\n\tif( filesizeptr ) *filesizeptr = filesize;\n\n\treturn buf;',
        '#endif\n#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( path, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_FS_ARCHIVE event=call_close path=%s handle=%d file=%p",\n\t\t\tpath, file->handle, file );\n\tint ps5_close_result = FS_Close( file );\n\tif( !Q_stricmp( path, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_FS_ARCHIVE event=close_return path=%s result=%d",\n\t\t\tpath, ps5_close_result );\n#else\n\tFS_Close( file );\n#endif\n\tif( filesizeptr )\n\t{\n#ifdef PS5_XASH_FS_TRACE\n\t\tif( !Q_stricmp( path, "gfx/palette.lmp" ))\n\t\t\t(void)ps5log_printf( "INFO", "XASH_FS_ARCHIVE event=call_sizeptr path=%s target=%p length=%lld",\n\t\t\t\tpath, filesizeptr, (long long)filesize );\n#endif\n\t\t*filesizeptr = filesize;\n#ifdef PS5_XASH_FS_TRACE\n\t\tif( !Q_stricmp( path, "gfx/palette.lmp" ))\n\t\t\t(void)ps5log_printf( "INFO", "XASH_FS_ARCHIVE event=sizeptr_return path=%s", path );\n#endif\n\t}\n#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( path, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_FS_ARCHIVE event=return path=%s buffer=%p", path, buf );\n#endif\n\n\treturn buf;',
        "FS_LoadFileFromArchive close and return",
    )
    return source.replace('"gfx/palette.lmp"', "PS5_XASH_FS_TRACE_PATH")


def instrument_searchpath(source: str) -> str:
    source = replace_once(
        source,
        '#include "library_suffix.h"\n',
        '#include "library_suffix.h"\n\n#ifdef PS5_XASH_FS_TRACE\nextern int ps5log_printf( const char *level, const char *fmt, ... );\nextern int PS5_FSTraceGuardActive( void );\nextern int PS5_FSTraceGuardCheck( const char *stage, const void *search, int result );\n#endif\n',
        "searchpath trace declarations",
    )
    source = replace_once(
        source,
        '\t\tpack_ind = search->pfnFindFile( search, name, fixedname, len );\n\t\tif( pack_ind >= 0 )',
        '#ifdef PS5_XASH_FS_TRACE\n\t\tif( PS5_FSTraceGuardActive( ))\n\t\t{\n\t\t\t(void)PS5_FSTraceGuardCheck( "before_callback", search, -1 );\n\t\t\t(void)ps5log_printf( "INFO", "XASH_FS_FIND event=before path=%s search=%p callback=%p root=%s capacity=%zu",\n\t\t\t\tname, search, search->pfnFindFile, search->filename, len );\n\t\t}\n#endif\n\t\tpack_ind = search->pfnFindFile( search, name, fixedname, len );\n#ifdef PS5_XASH_FS_TRACE\n\t\tif( PS5_FSTraceGuardActive( ))\n\t\t{\n\t\t\tsize_t used = 0;\n\t\t\tint terminated = fixedname == NULL;\n\t\t\tif( fixedname )\n\t\t\t{\n\t\t\t\twhile( used < len && fixedname[used] ) used++;\n\t\t\t\tterminated = used < len;\n\t\t\t}\n\t\t\t(void)PS5_FSTraceGuardCheck( "after_callback", search, pack_ind );\n\t\t\t(void)ps5log_printf( "INFO", "XASH_FS_FIND event=after path=%s search=%p result=%d used=%zu terminated=%d",\n\t\t\t\tname, search, pack_ind, used, terminated );\n\t\t}\n#endif\n\t\tif( pack_ind >= 0 )',
        "FS_FindFile callback",
    )
    return source


def instrument_img_main(source: str) -> str:
    source = replace_once(
        source,
        '#include "imagelib.h"\n',
        '#include "imagelib.h"\n\n#ifdef PS5_XASH_FS_TRACE\nextern int ps5log_printf( const char *level, const char *fmt, ... );\n#endif\n',
        "img_main trace declaration",
    )
    source = replace_once(
        source,
        '\treturn fmt->loadfunc( name, buf, size );',
        '#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( name, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_IMAGE event=call_loader name=%s buffer=%p bytes=%zu loader=%p hint=%d",\n\t\t\tname, buf, size, fmt->loadfunc, image.hint );\n#endif\n\tqboolean ps5_load_result = fmt->loadfunc( name, buf, size );\n#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( name, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_IMAGE event=loader_return name=%s result=%d", name, ps5_load_result );\n#endif\n\treturn ps5_load_result;',
        "Image_ProbeLoadBuffer_ loader",
    )
    return source.replace('"gfx/palette.lmp"', "PS5_XASH_FS_TRACE_PATH")


def instrument_img_wad(source: str) -> str:
    source = replace_once(
        source,
        '#include "swaplib.h"\n',
        '#include "swaplib.h"\n\n#ifdef PS5_XASH_FS_TRACE\nextern int ps5log_printf( const char *level, const char *fmt, ... );\n#endif\n',
        "img_wad trace declaration",
    )
    source = replace_once(
        source,
        '\tint\trendermode = LUMP_NORMAL;\n\tbyte pal[768];',
        '\tint\trendermode = LUMP_NORMAL;\n\tbyte pal[768];\n#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( name, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_IMAGE event=pal_enter name=%s buffer=%p bytes=%lld",\n\t\t\tname, buffer, (long long)filesize );\n#endif',
        "Image_LoadPAL entry",
    )
    source = replace_once(
        source,
        '\tImage_GetPaletteLMP( buffer, rendermode );\n\tImage_CopyPalette32bit();',
        '#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( name, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_IMAGE event=call_get_palette name=%s buffer=%p mode=%d", name, buffer, rendermode );\n#endif\n\tImage_GetPaletteLMP( buffer, rendermode );\n#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( name, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_IMAGE event=get_palette_return name=%s current=%p", name, image.d_currentpal );\n\tif( !Q_stricmp( name, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_IMAGE event=call_copy_palette name=%s pool=%d", name, host.imagepool );\n#endif\n\tImage_CopyPalette32bit();\n#ifdef PS5_XASH_FS_TRACE\n\tif( !Q_stricmp( name, "gfx/palette.lmp" ))\n\t\t(void)ps5log_printf( "INFO", "XASH_IMAGE event=copy_palette_return name=%s palette=%p", name, image.palette );\n#endif',
        "Image_LoadPAL palette conversion",
    )
    source = replace_once(
        source,
        '\tif( Q_stristr( name, "palette.lmp" ))\n\t\treturn Image_LoadPAL( name, buffer, filesize );',
        '\tif( Q_stristr( name, "palette.lmp" ))\n\t{\n#ifdef PS5_XASH_FS_TRACE\n\t\t(void)ps5log_printf( "INFO", "XASH_IMAGE event=call_pal name=%s buffer=%p bytes=%lld",\n\t\t\tname, buffer, (long long)filesize );\n#endif\n\t\tqboolean ps5_pal_result = Image_LoadPAL( name, buffer, filesize );\n#ifdef PS5_XASH_FS_TRACE\n\t\t(void)ps5log_printf( "INFO", "XASH_IMAGE event=pal_return name=%s result=%d", name, ps5_pal_result );\n#endif\n\t\treturn ps5_pal_result;\n\t}',
        "Image_LoadLMP palette call",
    )
    return source.replace('"gfx/palette.lmp"', "PS5_XASH_FS_TRACE_PATH")


def generate(io_path: Path, searchpath_path: Path, img_main_path: Path,
             img_wad_path: Path, output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    (output / "io.c").write_text(instrument_io(io_path.read_text()))
    (output / "searchpath.c").write_text(instrument_searchpath(searchpath_path.read_text()))
    (output / "img_main.c").write_text(instrument_img_main(img_main_path.read_text()))
    (output / "img_wad.c").write_text(instrument_img_wad(img_wad_path.read_text()))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--io", type=Path, required=True)
    parser.add_argument("--searchpath", type=Path, required=True)
    parser.add_argument("--img-main", type=Path, required=True)
    parser.add_argument("--img-wad", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.io, args.searchpath, args.img_main, args.img_wad, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
