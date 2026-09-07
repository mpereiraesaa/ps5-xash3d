/*
fs_ps5.c - path layer for the Xash3D filesystem on PlayStation 5
Copyright (C) 2026 Manuel Pereira

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Measured on FW 12.02 from a ShadowMount title:
  - chdir(2) fails with EPERM for every directory, including /app0;
  - libSceLibcInternal's getcwd() faults and its opendir() fails with EPERM;
  - sceKernelOpen(O_DIRECTORY) + sceKernelGetdents list /download0 but return
    EINVAL on the packaged image under /app0;
  - open/stat/mkdir/unlink/rename on absolute paths work as expected.

filesystem_stdio changes into its root directory once and then addresses it
as "./", so the engine cannot run without a working directory. This file
keeps a virtual one: every path-taking call the engine and its modules use is
resolved against it and forwarded to the kernel entry points that libkernel
exports under their sceKernel* names. Directory listing is implemented over
getdents; an image directory that refuses enumeration lists as empty.
*/

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>

extern int sceKernelOpen( const char *path, int flags, int mode );
extern int sceKernelClose( int fd );
extern int sceKernelStat( const char *path, struct stat *st );
extern int sceKernelMkdir( const char *path, int mode );
extern int sceKernelRmdir( const char *path );
extern int sceKernelUnlink( const char *path );
extern int sceKernelRename( const char *from, const char *to );
extern int sceKernelUtimes( const char *path, const struct timeval *times );
extern int sceKernelGetdents( int fd, char *buf, int nbytes );
extern int ps5log_printf( const char *level, const char *fmt, ... );
#define PS5LOG_INFO "INFO"
static unsigned ps5_opendir_seq;

#define PS5_PATH_MAX 1024

static char ps5_cwd[PS5_PATH_MAX] = "/app0/xash3d";
static int ps5_listing_refused;

/*
Directory index for the packaged image. getdents refuses the nullfs-backed
/app0 tree with EINVAL, but filesystem_stdio discovers game folders, WAD
archives and case-insensitive file names by enumeration. The build writes
xash3d/.dirindex (one "relative/path<TAB>d|f" line per entry, sorted) and
opendir() under the image root serves entries from it instead of the kernel.
*/
typedef struct ps5_index_entry_s
{
	const char *path;   /* relative to the image root, no leading slash */
	const char *name;   /* last component of path */
	size_t parent_len;  /* length of the parent directory prefix */
	unsigned char type; /* DT_DIR or DT_REG */
} ps5_index_entry_t;

static char ps5_image_root[PS5_PATH_MAX];
static char *ps5_index_text;
static ps5_index_entry_t *ps5_index;
static int ps5_index_count;

int PS5_LoadDirIndex( const char *image_root, const char *index_path )
{
	struct stat st;
	int fd, count = 0, i;
	char *line, *save;
	ssize_t got, total = 0;

	strncpy( ps5_image_root, image_root, sizeof( ps5_image_root ) - 1 );
	fd = sceKernelOpen( index_path, O_RDONLY, 0 );
	if( fd < 0 )
		return -1;
	if( sceKernelStat( index_path, &st ) != 0 || st.st_size <= 0 )
	{
		sceKernelClose( fd );
		return -1;
	}
	ps5_index_text = malloc( (size_t)st.st_size + 1 );
	if( !ps5_index_text )
	{
		sceKernelClose( fd );
		return -1;
	}
	while( total < st.st_size && ( got = read( fd, ps5_index_text + total, (size_t)( st.st_size - total ))) > 0 )
		total += got;
	sceKernelClose( fd );
	ps5_index_text[total] = 0;
	for( i = 0; i < total; i++ )
		if( ps5_index_text[i] == '\n' ) count++;
	ps5_index = calloc( (size_t)count + 1, sizeof( *ps5_index ));
	if( !ps5_index )
		return -1;
	for( line = strtok_r( ps5_index_text, "\n", &save ); line; line = strtok_r( NULL, "\n", &save ))
	{
		char *tab = strchr( line, '\t' );
		char *slash;
		ps5_index_entry_t *e = &ps5_index[ps5_index_count];
		if( !tab || !*line )
			continue;
		*tab = 0;
		e->path = line;
		e->type = tab[1] == 'd' ? DT_DIR : DT_REG;
		slash = strrchr( line, '/' );
		e->name = slash ? slash + 1 : line;
		e->parent_len = slash ? (size_t)( slash - line ) : 0;
		ps5_index_count++;
	}
	return ps5_index_count;
}

int PS5_DirIndexCount( void )
{
	return ps5_index_count;
}

void PS5_SetCwd( const char *dir )
{
	strncpy( ps5_cwd, dir, sizeof( ps5_cwd ) - 1 );
	ps5_cwd[sizeof( ps5_cwd ) - 1] = 0;
}

int PS5_ListingRefusedCount( void )
{
	return ps5_listing_refused;
}

/* Sony's kernel returns positive SCE error codes through libkernel wrappers;
   translate the ones this layer meets so errno keeps POSIX meaning. */
static int ps5_errno_from( int rc )
{
	switch( (unsigned)rc )
	{
	case 0x80020002: return ENOENT;
	case 0x80020001: return EPERM;
	case 0x8002000d: return EACCES;
	case 0x80020011: return EEXIST;
	case 0x80020014: return ENOTDIR;
	case 0x80020015: return EISDIR;
	case 0x80020016: return EINVAL;
	case 0x8002001e: return EROFS;
	default: return errno ? errno : EIO;
	}
}

/*
Normalize into an absolute path: relative names hang off the virtual cwd,
"." and ".." components are folded, duplicate slashes dropped.
*/
static const char *ps5_resolve( const char *path, char *out, size_t size )
{
	char joined[PS5_PATH_MAX * 2];
	char *parts[128];
	int count = 0, i;
	char *save, *token;
	size_t used = 0;

	if( !path )
	{
		errno = EFAULT;
		return NULL;
	}
	if( path[0] == '/' )
		snprintf( joined, sizeof( joined ), "%s", path );
	else
		snprintf( joined, sizeof( joined ), "%s/%s", ps5_cwd, path );

	for( token = strtok_r( joined, "/", &save ); token; token = strtok_r( NULL, "/", &save ))
	{
		if( !strcmp( token, "." ) || !*token )
			continue;
		if( !strcmp( token, ".." ))
		{
			if( count > 0 ) count--;
			continue;
		}
		if( count < (int)( sizeof( parts ) / sizeof( parts[0] )))
			parts[count++] = token;
	}
	out[0] = 0;
	if( count == 0 )
	{
		snprintf( out, size, "/" );
		return out;
	}
	for( i = 0; i < count; i++ )
	{
		int n = snprintf( out + used, size - used, "/%s", parts[i] );
		if( n < 0 || (size_t)n >= size - used )
		{
			errno = ENAMETOOLONG;
			return NULL;
		}
		used += (size_t)n;
	}
	return out;
}

#define RESOLVE( var, path, fail ) \
	char var[PS5_PATH_MAX]; \
	if( !ps5_resolve( path, var, sizeof( var ))) return fail

int chdir( const char *path )
{
	struct stat st;
	RESOLVE( full, path, -1 );
	if( sceKernelStat( full, &st ) != 0 )
	{
		errno = ENOENT;
		return -1;
	}
	if( !S_ISDIR( st.st_mode ))
	{
		errno = ENOTDIR;
		return -1;
	}
	PS5_SetCwd( full );
	return 0;
}

char *getcwd( char *buf, size_t size )
{
	if( !buf || size <= strlen( ps5_cwd ))
	{
		errno = ERANGE;
		return NULL;
	}
	strcpy( buf, ps5_cwd );
	return buf;
}

char *realpath( const char *path, char *resolved )
{
	char full[PS5_PATH_MAX];
	struct stat st;
	if( !ps5_resolve( path, full, sizeof( full )))
		return NULL;
	if( sceKernelStat( full, &st ) != 0 )
	{
		errno = ENOENT;
		return NULL;
	}
	if( !resolved )
		resolved = malloc( strlen( full ) + 1 );
	if( !resolved )
		return NULL;
	strcpy( resolved, full );
	return resolved;
}

int open( const char *path, int flags, ... )
{
	int mode = 0;
	int fd;
	RESOLVE( full, path, -1 );
	if( flags & O_CREAT )
	{
		va_list args;
		va_start( args, flags );
		mode = va_arg( args, int );
		va_end( args );
	}
	fd = sceKernelOpen( full, flags, mode );
	if( fd < 0 )
	{
		errno = ps5_errno_from( fd );
		return -1;
	}
	return fd;
}

int stat( const char *path, struct stat *st )
{
	int rc;
	RESOLVE( full, path, -1 );
	rc = sceKernelStat( full, st );
	if( rc != 0 )
	{
		errno = ps5_errno_from( rc );
		return -1;
	}
	return 0;
}

int lstat( const char *path, struct stat *st )
{
	return stat( path, st );
}

int mkdir( const char *path, mode_t mode )
{
	int rc;
	RESOLVE( full, path, -1 );
	rc = sceKernelMkdir( full, (int)mode );
	if( rc != 0 )
	{
		errno = ps5_errno_from( rc );
		return -1;
	}
	return 0;
}

int rmdir( const char *path )
{
	int rc;
	RESOLVE( full, path, -1 );
	rc = sceKernelRmdir( full );
	if( rc != 0 )
	{
		errno = ps5_errno_from( rc );
		return -1;
	}
	return 0;
}

int unlink( const char *path )
{
	int rc;
	RESOLVE( full, path, -1 );
	rc = sceKernelUnlink( full );
	if( rc != 0 )
	{
		errno = ps5_errno_from( rc );
		return -1;
	}
	return 0;
}

int remove( const char *path )
{
	if( unlink( path ) == 0 )
		return 0;
	if( errno == EISDIR || errno == EPERM )
		return rmdir( path );
	return -1;
}

int rename( const char *from, const char *to )
{
	int rc;
	char full_from[PS5_PATH_MAX], full_to[PS5_PATH_MAX];
	if( !ps5_resolve( from, full_from, sizeof( full_from )) ||
	    !ps5_resolve( to, full_to, sizeof( full_to )))
		return -1;
	rc = sceKernelRename( full_from, full_to );
	if( rc != 0 )
	{
		errno = ps5_errno_from( rc );
		return -1;
	}
	return 0;
}

int utime( const char *path, const struct utimbuf *times )
{
	struct timeval tv[2];
	int rc;
	RESOLVE( full, path, -1 );
	if( times )
	{
		tv[0].tv_sec = times->actime;
		tv[1].tv_sec = times->modtime;
		tv[0].tv_usec = tv[1].tv_usec = 0;
		rc = sceKernelUtimes( full, tv );
	}
	else rc = sceKernelUtimes( full, NULL );
	if( rc != 0 )
	{
		errno = ps5_errno_from( rc );
		return -1;
	}
	return 0;
}

int access( const char *path, int mode )
{
	struct stat st;
	(void)mode;
	return stat( path, &st );
}

/* Directory streams over the kernel getdents interface. The kernel record
   layout matches this SDK's struct dirent (32-bit fileno, reclen, type,
   namlen, name), so records are handed out in place. */
typedef struct ps5_dir_s
{
	int fd;          /* -1 for an index-backed stream */
	int length;
	int offset;
	int exhausted;
	char *synthetic; /* dirent records built from the index */
	char buffer[8192];
} ps5_dir_t;

/* Build dirent records for one image directory from the index. */
static ps5_dir_t *ps5_open_indexed( const char *relative )
{
	size_t rel_len = strlen( relative );
	size_t need = 0, used = 0;
	int i, matches = 0;
	ps5_dir_t *dir;
	for( i = 0; i < ps5_index_count; i++ )
	{
		const ps5_index_entry_t *e = &ps5_index[i];
		if( e->parent_len != rel_len || strncmp( e->path, relative, rel_len ) != 0 )
			continue;
		need += ( offsetof( struct dirent, d_name ) + strlen( e->name ) + 1 + 3 ) & ~(size_t)3;
		matches++;
	}
	dir = calloc( 1, sizeof( *dir ));
	if( !dir )
	{
		errno = ENOMEM;
		return NULL;
	}
	dir->fd = -1;
	dir->synthetic = malloc( need + 1 );
	if( !dir->synthetic )
	{
		free( dir );
		errno = ENOMEM;
		return NULL;
	}
	for( i = 0; i < ps5_index_count && matches > 0; i++ )
	{
		const ps5_index_entry_t *e = &ps5_index[i];
		struct dirent *d;
		size_t namelen, reclen;
		if( e->parent_len != rel_len || strncmp( e->path, relative, rel_len ) != 0 )
			continue;
		namelen = strlen( e->name );
		reclen = ( offsetof( struct dirent, d_name ) + namelen + 1 + 3 ) & ~(size_t)3;
		d = (struct dirent *)( dir->synthetic + used );
		memset( d, 0, reclen );
		d->d_fileno = (unsigned)( i + 1 );
		d->d_reclen = (unsigned short)reclen;
		d->d_type = e->type;
		d->d_namlen = (unsigned char)namelen;
		memcpy( d->d_name, e->name, namelen + 1 );
		used += reclen;
		matches--;
	}
	dir->length = (int)used;
	return dir;
}

DIR *opendir( const char *path )
{
	ps5_dir_t *dir;
	int fd;
	size_t root_len = strlen( ps5_image_root );
	RESOLVE( full, path, NULL );
	if( ps5_index_count > 0 && root_len > 0 && strncmp( full, ps5_image_root, root_len ) == 0 &&
	    ( full[root_len] == 0 || full[root_len] == '/' ))
	{
		const char *relative = full + root_len;
		unsigned matches = 0; int k;
		while( *relative == '/' ) relative++;
		for( k = 0; k < ps5_index_count; k++ )
			if( ps5_index[k].parent_len == strlen( relative ) &&
			    strncmp( ps5_index[k].path, relative, strlen( relative )) == 0 ) matches++;
#ifdef PS5_XASH_FS_TRACE
		(void)ps5log_printf( PS5LOG_INFO, "XASH_OPENDIR seq=%u kind=index path=%s rel=%s entries=%u",
			++ps5_opendir_seq, full, relative, matches );
#else
		(void)matches;
#endif
		return (DIR *)ps5_open_indexed( relative );
	}
#ifdef PS5_XASH_FS_TRACE
	(void)ps5log_printf( PS5LOG_INFO, "XASH_OPENDIR seq=%u kind=kernel path=%s", ++ps5_opendir_seq, full );
#endif
	fd = sceKernelOpen( full, O_RDONLY | O_DIRECTORY, 0 );
	if( fd < 0 )
	{
		errno = ps5_errno_from( fd );
		return NULL;
	}
	dir = calloc( 1, sizeof( *dir ));
	if( !dir )
	{
		sceKernelClose( fd );
		errno = ENOMEM;
		return NULL;
	}
	dir->fd = fd;
	return (DIR *)dir;
}

struct dirent *readdir( DIR *stream )
{
	ps5_dir_t *dir = (ps5_dir_t *)stream;
	struct dirent *entry;
	if( !dir )
	{
		errno = EBADF;
		return NULL;
	}
	for( ;; )
	{
		if( dir->synthetic )
		{
			if( dir->offset >= dir->length )
				return NULL;
			entry = (struct dirent *)( dir->synthetic + dir->offset );
			dir->offset += entry->d_reclen;
			return entry;
		}
		if( dir->offset >= dir->length )
		{
			int n;
			if( dir->exhausted )
				return NULL;
			n = sceKernelGetdents( dir->fd, dir->buffer, sizeof( dir->buffer ));
			if( n <= 0 )
			{
				/* The packaged image under /app0 answers EINVAL: expose it as
				   an empty directory rather than an error. */
				if( n < 0 )
					ps5_listing_refused++;
				dir->exhausted = 1;
				return NULL;
			}
			dir->length = n;
			dir->offset = 0;
		}
		entry = (struct dirent *)( dir->buffer + dir->offset );
		if( entry->d_reclen == 0 )
		{
			dir->exhausted = 1;
			return NULL;
		}
		dir->offset += entry->d_reclen;
		if( entry->d_fileno == 0 )
			continue;
		return entry;
	}
}

int closedir( DIR *stream )
{
	ps5_dir_t *dir = (ps5_dir_t *)stream;
	if( !dir )
	{
		errno = EBADF;
		return -1;
	}
	if( dir->fd >= 0 )
		sceKernelClose( dir->fd );
	free( dir->synthetic );
	free( dir );
	return 0;
}
