/*

   Copyright (c) 2005 Andre Landwehr <andrel@cybernoia.de>

   This program can be distributed under the terms of the GNU GPL.
   See the file COPYING.

   Based on: fusexmp.c by Miklos Szeredi

*/

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#define _GNU_SOURCE 1
#endif

#define FUSE_USE_VERSION 22

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <wchar.h>
#include <archive.h>
#include <archive_entry.h>

/* macros */
#ifdef NDEBUG
#   define log(format, ...)
#else
#   define log(format, ...) \
{ \
	FILE *FH = fopen( "/tmp/archivemount.log", "a" ); \
	if( FH ) { \
		fprintf( FH, "l. %4d: " format "\n", __LINE__, ##__VA_ARGS__ ); \
		fclose( FH ); \
	} \
}
#endif

/* data structures */
typedef struct node {
	struct node *parent;
	struct node *prev; /* previous in same directory */
	struct node *next; /* next in same directory */
	struct node *child; /* first child for directories */
	char *name; /* fully qualified with prepended '/' */
	struct archive_entry *entry;
} NODE;

/* globals */
static char *archiveFile; /* name of archive file */
static NODE *root;

/* internal functions */
static void
init_node( NODE *node )
{
	node->child = NULL;
	node->prev = NULL;
	node->next = NULL;
	node->parent = NULL;
	node->entry = NULL;
	node->name = NULL;
}

static void
insert_as_child( NODE *node, NODE *parent )
{
	node->parent = parent;
	if( ! parent->child ) {
		parent->child = node;
	} else {
		/* find last child of parent, insert node behind it */
		NODE *b = parent->child;
		while( b->next ) {
			b = b->next;
		}
		b->next = node;
		node->prev = b;
	}
	//log( "inserted '%s' as child of '%s'", node->name, parent->name );
}

static void
build_tree( const char *mtpt )
{
	struct archive *archive;
	struct archive_entry *entry;
	NODE *old;
	struct stat st;

	/* open archive */
	archive = archive_read_new();
	if( archive_read_support_compression_all( archive ) != ARCHIVE_OK ) {
		log( "%s", archive_error_string( archive ) );
	}
	if( archive_read_support_format_all( archive ) != ARCHIVE_OK ) {
		log( "%s", archive_error_string( archive ) );
	}
	if( archive_read_open_file( archive, archiveFile, 1024 ) != ARCHIVE_OK ) {
		log( "%s", archive_error_string( archive ) );
	}
	/* create root node */
	root = malloc( sizeof( NODE ) );
	init_node( root );
	root->name = strdup( "/" );
	/* fill root->entry */
	root->entry = archive_entry_new();
	stat( archiveFile, &st );
	archive_entry_set_gid( root->entry, getgid() );
	archive_entry_set_gid( root->entry, getuid() );
	archive_entry_set_mode( root->entry, st.st_mtime );
	archive_entry_set_pathname( root->entry, "/" );
	archive_entry_set_size( root->entry, st.st_size );
	stat( mtpt, &st );
	archive_entry_set_mode( root->entry, st.st_mode );
	/* read all entries in archive, create node for each */
	old = root;
	while( archive_read_next_header( archive, &entry ) == ARCHIVE_OK ) {
		NODE *cur;
		const char *name;
		cur = malloc( sizeof( NODE ) );
		init_node( cur );
		/* set name and entry of node */
		name = archive_entry_pathname( entry );
		cur->entry = archive_entry_clone( entry );
		/* prepend a '/' to name if needed */
		if( *name != '/' ) {
			cur->name = malloc( strlen( name ) + 2 );
			sprintf( cur->name, "/%s", name );
		} else {
			cur->name = strdup( name );
		}
		/* remove trailing '/' for directories */
		if( cur->name[strlen(cur->name)-1] == '/' ) {
			cur->name[strlen(cur->name)-1] = '\0';
		}
		/* references */
		//log( "oldname: '%s'('%s'), curname: '%s' oldisdir: %d", old->name, archive_entry_pathname( old->entry ), cur->name, S_ISDIR( archive_entry_mode( old->entry ) ) );
		if( S_ISDIR( archive_entry_mode( old->entry ) ) &&
				strncmp( old->name, cur->name, strlen( old->name ) ) == 0 )
		{
			/* new entry is obviously a child of the old one */
			insert_as_child( cur, old );
		} else {
			/* new entry is somewhere else in the tree */
			NODE *par = old->parent;
			while( par ) {
				if( strncmp( cur->name, par->name,
							strlen( par->name ) ) == 0 )
				{
					insert_as_child( cur, par );
					break;
				}
				par = par->parent;
			}
			if( ! par ) {
				log( "ERROR: could not insert '%s' into tree!", cur->name );
			}
		}
		old = cur;
		archive_read_data_skip( archive );
	}
	/* close archive */
	archive_read_finish( archive );
}

static NODE *
get_node_for_path( NODE *start, const char *path )
{
	NODE *ret = NULL;
	NODE *run = start;

	while( run ) {
		if( strcmp( path, run->name ) == 0 ) {
			ret = run;
			break;
		}
		if( run->child &&
				strncmp( path, run->name, strlen( run->name ) ) == 0 )
		{
			if( ( ret = get_node_for_path( run->child, path ) ) ) {
				break;
			}
		}
		run = run->next;
	}
	return ret;
}

static int
persist( struct archive *archive )
{
	/* FIXME: save the (given, if parameter != NULL) archive */
	return -EROFS;
}

/* API functions */


static int
ar_mknod(const char *path, mode_t mode, dev_t rdev)
{
	return -EROFS;
}

static int
ar_mkdir(const char *path, mode_t mode)
{
	return -EROFS;
}

static int
ar_unlink(const char *path)
{
	return -EROFS;
}

static int
ar_rmdir(const char *path)
{
	return -EROFS;
}

static int
ar_symlink(const char *from, const char *to)
{
	return -EROFS;
}

static int
ar_link(const char *from, const char *to)
{
	return -ENOSYS;
}

static int
ar_chmod(const char *path, mode_t mode)
{
	return -EROFS;
}

static int
ar_chown(const char *path, uid_t uid, gid_t gid)
{
	return -EROFS;
}

static int
ar_truncate(const char *path, off_t size)
{
	return -EROFS;
}

static int
ar_utime(const char *path, struct utimbuf *buf)
{
	return -EROFS;
}

static int
ar_write(const char *path, const char *buf, size_t size,
		off_t offset, struct fuse_file_info *fi)
{
	return -EROFS;
}

static int
ar_statfs(const char *path, struct statfs *stbuf)
{
	return -ENOSYS;
}

static int
ar_fsync( const char *path, int isdatasync, struct fuse_file_info *fi )
{
	/* Just a stub.  This method is optional and can safely be left
	   unimplemented */
	( void )path;
	( void )isdatasync;
	( void )fi;
	return 0;
}

static int ar_release(const char *path, struct fuse_file_info *fi)
{
	return 0;
}

static int
ar_readlink( const char *path, char *buf, size_t size )
{
	NODE *node;

	node = get_node_for_path( root, path );
	if( ! node ) {
		return -ENOENT;
	}
	if( ! S_ISLNK( archive_entry_mode( node->entry ) ) ) {
		return -ENOLINK;
	}
	snprintf( buf, size, "%s", archive_entry_symlink( node->entry ) );
	return 0;
}

static int
ar_rename( const char *from, const char *to )
{
	NODE *node;
	wchar_t *wname;
	int ret;
	const char *src;

	/* FIXME: the code works as soon as persist() is implemented */
	return -EROFS;
	//log( "ar_rename: got from: '%s', to: '%s'", from, to );
	node = get_node_for_path( root, from );
	if( ! node ) {
		return -ENOENT;
	}
	wname = malloc( ( strlen( to ) + 2 ) * sizeof( wchar_t ) );
	src = strdup( to );
	if( mbsrtowcs( wname, &src, strlen( to ) + 1, NULL ) == -1 ) {
		free( wname );
		return 0 - errno;
	}
	archive_entry_copy_pathname_w( node->entry, wname );
	if( src ) {
		free( (char *)src );
	}
	free( wname );
	free( node->name );
	if( *to != '/' ) {
		node->name = malloc( strlen( to ) + 2 );
		sprintf( node->name, "/%s", to );
	} else {
		node->name = strdup( to );
	}
	return persist( NULL );
}

static int
ar_open( const char *path, struct fuse_file_info *fi )
{
	NODE *node;

	node = get_node_for_path( root, path );
	if( ! node ) {
		return -ENOENT;
	}
	if( fi->flags & O_WRONLY || fi->flags & O_RDWR ) {
		/* currently the filesystem is read only, patches welcome.. */
		return -EROFS;
	}
	/* no need to save a handle here since archives are stream based */
	fi->fh = 0;
	return 0;
}

static int
ar_read( const char *path, char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi )
{
	struct archive *archive;
	struct archive_entry *entry;
	int ret = -1;

	/* open archive */
	archive = archive_read_new();
	if( archive_read_support_compression_all( archive ) != ARCHIVE_OK ) {
		log( "%s", archive_error_string( archive ) );
	}
	if( archive_read_support_format_all( archive ) != ARCHIVE_OK ) {
		log( "%s", archive_error_string( archive ) );
	}
	if( archive_read_open_file( archive, archiveFile, 1024 ) != ARCHIVE_OK ) {
		log( "%s", archive_error_string( archive ) );
	}
	/* search for file to read */
	while( archive_read_next_header( archive, &entry ) == ARCHIVE_OK ) {
		const char *name;
		name = archive_entry_pathname( entry );
		if( *name == '/' ) {
			name++;
		}
		if( strcmp( path + 1, name ) == 0 ) {
			void *trash;
			trash = malloc( offset );
			ret = archive_read_data( archive, trash, offset );
			if( ret == ARCHIVE_FATAL
					|| ret == ARCHIVE_WARN
					|| ret == ARCHIVE_RETRY )
			{
				log( "ar_read (skipping offset): %s",
						archive_error_string( archive ) );
				errno = archive_errno( archive );
				ret = -1;
				free( trash );
				break;
			}
			free( trash );
			ret = archive_read_data( archive, buf, size );
			if( ret == ARCHIVE_FATAL
					|| ret == ARCHIVE_WARN
					|| ret == ARCHIVE_RETRY )
			{
				log( "ar_read (reading data): %s",
						archive_error_string( archive ) );
				errno = archive_errno( archive );
				ret = -1;
				break;
			}
		}
		archive_read_data_skip( archive );
	}
	/* close archive */
	archive_read_finish( archive );
	return ret;
}

static int
ar_getattr( const char *path, struct stat *stbuf )
{
	NODE *node;

	//log( "getattr got path: '%s'\n", path );
	node = get_node_for_path( root, path );
	if( ! node ) {
		return -ENOENT;
	}
	memcpy( stbuf,
			archive_entry_stat( node->entry ),
			sizeof( struct stat ) );
	return 0;
}

static int
ar_readdir( const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi )
{
	NODE *node;
	(void) offset;
	(void) fi;

	//log( "readdir got path: '%s'", path );
	node = get_node_for_path( root, path );
	if( ! node ) {
		return -ENOENT;
	}

	node = node->child;
	while( node ) {
		struct stat st;
		char *name;
		st.st_ino = archive_entry_ino( node->entry );
		st.st_mode = archive_entry_mode( node->entry );
		name = strrchr( node->name, '/' ) + 1;
		if( filler( buf, name, &st, 0 ) )
			break;
		node = node->next;
	}
	return 0;
}

static struct fuse_operations ar_oper = {
	.getattr        = ar_getattr,
	.readlink       = ar_readlink,
	.readdir        = ar_readdir,
	.mknod          = ar_mknod,
	.mkdir          = ar_mkdir,
	.symlink        = ar_symlink,
	.unlink         = ar_unlink,
	.rmdir          = ar_rmdir,
	.rename         = ar_rename,
	.link           = ar_link,
	.chmod          = ar_chmod,
	.chown          = ar_chown,
	.truncate       = ar_truncate,
	.utime          = ar_utime,
	.open           = ar_open,
	.read           = ar_read,
	.write          = ar_write,
	.statfs         = ar_statfs,
	.release        = ar_release,
	.fsync          = ar_fsync,
/*
#ifdef HAVE_SETXATTR
	.setxattr       = xmp_setxattr,
	.getxattr       = xmp_getxattr,
	.listxattr      = xmp_listxattr,
	.removexattr    = xmp_removexattr,
#endif
*/
};

void
showUsage()
{
	fprintf( stderr, "Usage: archivemount <options> <archive> <mountpoint>\n" );
}

int
main( int argc, char **argv )
{
	int i;
	int fuse_ret;
	struct stat status;
	struct archive_entry *entry;
	char *mtpt;

	/* parse cmdline args */
	if( argc < 3 ) {
		showUsage();
		exit( EXIT_FAILURE );
	}
	mtpt = argv[--argc];
	archiveFile = argv[--argc];
	argv[argc] = argv[++argc];

	/* check if mtpt is ok */
	if( stat( mtpt, &status ) != 0 ) {
		perror( "Error stat'ing mountpoint" );
		exit( EXIT_FAILURE );
	}
	if( ! S_ISDIR( status.st_mode ) ) {
		fprintf( stderr, "Problem with mountpoint: %s\n", strerror( ENOTDIR ) );
		exit( EXIT_FAILURE );
	}

	/* check if archive file is ok */
	if( stat( archiveFile, &status ) != 0 ) {
		perror( "Error stat'ing archiveFile" );
		exit( EXIT_FAILURE );
	}
	if( ! ( status.st_mode & S_IRUSR ) ) {
		fprintf( stderr, "Problem reading archiveFile: %s\n", strerror( EPERM ) );
		exit( EXIT_FAILURE );
	}

	/* build up internal tree of metadata */
	build_tree( mtpt );

	/* now do the real mount */
	fuse_ret = fuse_main(argc, argv, &ar_oper);

	return EXIT_SUCCESS;
}

