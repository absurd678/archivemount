#include "FuseWrapper.hpp"

FuseWrapper::FuseWrapper(ArchiveFS& fs) (fs);

int FuseWrapper::ar_opt_proc(void *, const char * arg, int key, struct fuse_args * outargs) { // Менять не надо
	struct fuse_operations faux_oper;
	switch(key) {
		case FUSE_OPT_KEY_OPT:
			printf("FUSE_OPT_KEY_OPT");
			return 1;

		case FUSE_OPT_KEY_NONOPT:
			if(!fs.archiveFile) {
				fs.archiveFile = arg;
				printf("FUSE_OPT_KEY_NONOPT !archiveFile");
				return 0;
			} else if(!fs.mtpt) {
				fs.mtpt = arg;
				printf("FUSE_OPT_KEY_NONOPT !mtpt");
				return 1;
			} else {
				usage(outargs->argv[0]);
				printf("FUSE_OPT_KEY_NONOPT else");
				exit(1);
			}

		case KEY_HELP:
			usage(outargs->argv[0]);
			printf("KEY_HELP");
			exit(0);

		case KEY_VERSION:
			fprintf(stdout, "archivemount version " VERSION "\n%s (header " ARCHIVE_VERSION_ONLY_STRING ")\n", archive_version_details());
			fuse_opt_add_arg(outargs, "--version");
			fuse_main(outargs->argc, outargs->argv, &faux_oper, NULL);
			exit(0);

		default:
			__builtin_unreachable();
	}
}

 void FuseWrapper::_ar_open_raw()
//_ar_open_raw(const char *path, struct fuse_file_info *fi)
{
	// open archive and search first entry
	printf("_ar_open_raw IS CALLABLE!");
	NODE * node = firstchild(fs.root);
	log("_ar_open_raw called, path: '%s'", node->name);

	last_open_node.node = node;

	if(last_open_node.archive) {
		archive_read_free(last_open_node.archive);
		lseek(fs.archiveFd, 0, SEEK_SET);
	}

	//	struct archive *archive;
	int archive_ret;
	/* search file in archive */
	if((last_open_node.archive = archive_read_new()) == NULL) {
		lerrnum(ENOMEM);
		return;
	}
	last_open_node.offset_in_archive_file = -2;
	if(!archive_prepopen(last_open_node.archive))
		return;

	struct archive_entry * entry;
	const char * realpath = archive_entry_pathname(node->entry);
	/* search for file to read - "/data" must be the first entry */
	while((archive_ret = archive_read_next_header(last_open_node.archive, &entry)) == ARCHIVE_OK)
		if(strcmp(realpath, archive_entry_pathname(entry)) == 0){
			printf("_ar_open_raw | realpath = %s", realpath); // I
			break;
		}

	last_open_node.offset_in_archive_file = 0;
}

 int FuseWrapper::_ar_read_archive_found_common(char * buf, size_t size, off_t offset) {
	auto & archive = last_open_node.archive;
	int ret;

	// we know last_open_node.offset_in_archive_file < offset (otherwise we reopened)
	offset -= last_open_node.offset_in_archive_file;

	while(offset > 0) {
		int skip = offset > (off_t)sizeof(temp_io_buf) ? (off_t)sizeof(temp_io_buf) : offset;
		ret      = archive_read_data(archive, temp_io_buf, skip);
		if(ret == ARCHIVE_FATAL || ret == ARCHIVE_WARN || ret == ARCHIVE_RETRY) {
			log("ar_read (skipping offset): %s", archive_error_string(archive));
			errno = archive_errno(archive);
			ret   = -1;
			break;
		}
		offset -= skip;
		last_open_node.offset_in_archive_file += skip;
	}
	if(offset)
		goto err;

	/* read data */
	ret = archive_read_data(archive, buf, size);
	if(ret == ARCHIVE_FATAL || ret == ARCHIVE_WARN || ret == ARCHIVE_RETRY) {
	err:
		log("ar_read (reading data): %s", archive_error_string(archive));
		errno                                 = archive_errno(archive);
		last_open_node.offset_in_archive_file = -2;
		ret                                   = -1;
	} else
		last_open_node.offset_in_archive_file += ret;

	return ret;
}

 int FuseWrapper::_ar_read_raw(const char * path, char * buf, size_t size, off_t offset, struct fuse_file_info *) {
	log("_ar_read_raw called, path: '%s'", path);
	/* find node */
	NODE * node = get_node_for_path(root, path);
	if(!node) {
		return -ENOENT;
	}

	if(last_open_node.node == node && last_open_node.offset_in_archive_file <= offset && last_open_node.offset_in_archive_file != -2)
		;
	else
		_ar_open_raw();

	return _ar_read_archive_found_common(buf, size, offset);
}

 int FuseWrapper::_ar_read(const char * path, char * buf, size_t size, off_t offset, struct fuse_file_info * fi) {
	int ret = -1;
	const char * realpath;
	log("_ar_read called, path: '%s'", path);
	/* find node */
	NODE * node = get_node_for_path(root, path);
	if(!node) {
		return -ENOENT;
	}
	if(archive_entry_hardlink(node->entry)) {
		/* file is a hardlink, recurse into it */
		return _ar_read(archive_entry_hardlink(node->entry), buf, size, offset, fi);
	}
	if(archive_entry_symlink(node->entry)) {
		/* file is a symlink, recurse into it */
		return _ar_read(archive_entry_symlink(node->entry), buf, size, offset, fi);
	}
	if(node->modified) {
		/* the file is new or modified, read temporary file instead */
		int fh;
		fh = open(node->location, O_RDONLY | O_CLOEXEC);
		if(fh == -1) {
			log("Fatal error opening modified file '%s' at location '%s', giving up", path, node->location);
			return -errno;
		}
		/* copy data */
		if((ret = pread(fh, buf, size, offset)) == -1) {
			log("Error reading temporary file '%s': %s", node->location, strerror(errno));
			close(fh);
			ret = -errno;
		}
		/* clean up */
		close(fh);
	} else {
		auto & archive = last_open_node.archive;
		struct archive_entry * entry;
		int archive_ret;

		if(last_open_node.node == node && last_open_node.offset_in_archive_file <= offset && last_open_node.offset_in_archive_file != -2)
			goto ready;

		log("reopening: last_open_node.node = %p; node = %p; last_open_node.offset_in_archive_file = %ld; offset = %ld", last_open_node.node, node,
		    last_open_node.offset_in_archive_file, offset);
		last_open_node.node                   = node;
		last_open_node.offset_in_archive_file = -2;

		if(archive) {
			archive_read_free(archive);
			lseek(archiveFd, 0, SEEK_SET);
		}
		archive = archive_read_new();
		if(!archive) {
			log("Out of memory");
			return -ENOMEM;
		}

		if(!archive_prepopen(archive))
			return -EIO;

		last_open_node.offset_in_archive_file = -1;

	ready:
		if(last_open_node.offset_in_archive_file == -1) {
			log("skipping");
			realpath = archive_entry_pathname(node->entry);

			/* search for file to read */
			while((archive_ret = archive_read_next_header(archive, &entry)) == ARCHIVE_OK) {
				const char * name;
				name = archive_entry_pathname(entry);
				if(strcmp(realpath, name) == 0) {
					last_open_node.offset_in_archive_file = 0;
					break;
				}
				archive_read_data_skip(archive);
			}
		}

		return _ar_read_archive_found_common(buf, size, offset);
	}
	return ret;
}

 int FuseWrapper::ar_read(const char * path, char * buf, size_t size, off_t offset, struct fuse_file_info * fi) {
	log("ar_read called, path: '%s'", path);
	int ret = pthread_mutex_lock(&lock);
	if(ret) {
		log("failed to get lock: %s\n", strerror(ret));
		return -EIO;
	} else {
		if(options.formatraw) {
			ret = _ar_read_raw(path, buf, size, offset, fi);
		} else {
			ret = _ar_read(path, buf, size, offset, fi);
		}
		pthread_mutex_unlock(&lock);
	}
	return ret;
}

 off_t _ar_getsizeraw(const char * path) {
	off_t offset = 0, ret;
	NODE * node;
	const char * realpath;

	log("ar_getsizeraw called, path: '%s'", path);
	/* find node */
	node = get_node_for_path(root, path);
	if(!node) {
		return -ENOENT;
	}

	struct archive * archive;
	struct archive_entry * entry;
	int archive_ret;
	/* search file in archive */
	realpath = archive_entry_pathname(node->entry);

	if((archive = archive_read_new()) == NULL) {
		log("Out of memory");
		return -ENOMEM;
	}

	if(!archive_prepopen(archive))
		return -EIO;

	/* search for file to read */
	while((archive_ret = archive_read_next_header(archive, &entry)) == ARCHIVE_OK) {
		const char * name = archive_entry_pathname(entry);
		if(strcmp(realpath, name) == 0) {
			/* read until no more data */
			while((ret = archive_read_data(archive, temp_io_buf, sizeof(temp_io_buf))) != 0) {
				if(ret == ARCHIVE_FATAL || ret == ARCHIVE_WARN || ret == ARCHIVE_RETRY) {
					log("ar_read (skipping offset): %s", archive_error_string(archive));
					errno = archive_errno(archive);
					ret   = -1;
					break;
				}
				offset += ret;
				// log("tmp offset =%ld (%ld)",offset,offset/1024/1024);
			}
			break;
		}
		archive_read_data_skip(archive);
	}  // end of search for file to read
	/* close archive */
	archive_read_free(archive);
	lseek(archiveFd, 0, SEEK_SET);

	return offset;
}

 int FuseWrapper::_ar_getattr(const char * path, struct stat * stbuf) {
	NODE * node;
	int ret;
	off_t size;

	// log("_ar_getattr called, path: '%s'", path);
	node = get_node_for_path(root, path);
	if(!node) {
		return -ENOENT;
	}
	if(archive_entry_hardlink(node->entry)) {
		/* a hardlink, recurse into it */
		ret = _ar_getattr(archive_entry_hardlink(node->entry), stbuf);
		return ret;
	}
	if(options.formatraw && node->children.empty()) {
		fstat(archiveFd, stbuf);
		size = rawcache.st_size;
		if(size < 0)
			return -1;
		stbuf->st_size = size;
	} else {
		*stbuf = *archive_entry_stat(node->entry);
		if(options.formatraw && !node->children.empty())
			stbuf->st_size = 4096;
	}
	stbuf->st_blocks  = (node->entry_size_in_archive + 511) / 512;
	stbuf->st_blksize = sizeof(temp_io_buf);
	/* when sharing via Samba nlinks have to be at
	   least 2 for directories or directories will
	   be shown as files, and 1 for files or they
	   cannot be opened */
	if(S_ISDIR(archive_entry_mode(node->entry))) {
		if(stbuf->st_nlink < 2) {
			stbuf->st_nlink = 2;
		}
	} else {
		if(stbuf->st_nlink < 1) {
			stbuf->st_nlink = 1;
		}
	}

	if(options.readonly) {
		stbuf->st_mode &= ~0222;
	}

	return 0;
}

 int FuseWrapper::ar_getattr(const char * path, struct stat * stbuf
#if FUSE_MAJOR_VERSION >= 3
                      ,
                      struct fuse_file_info *
#endif
) {
	// log("ar_getattr called, path: '%s'", path);
	int ret = pthread_mutex_lock(&lock);
	if(ret) {
		log("failed to get lock: %s\n", strerror(ret));
		return -EIO;
	} else {
		ret = _ar_getattr(path, stbuf);
		pthread_mutex_unlock(&lock);
	}
	return ret;
}

/*
 * mkdir is nearly identical to mknod...
 */
 int FuseWrapper::ar_mkdir(const char * path, mode_t mode) {
	NODE * node;
	char * location;
	int tmp;
	printf("\nar_mkdir callable!\n");
	
	log("ar_mkdir called, path '%s', mode %o", path, mode);
	if(!archiveWriteable || options.readonly) {
		return -EROFS;
	}
	pthread_mutex_lock(&lock);
	/* check for existing node */
	node = get_node_for_path(root, path);
	if(node) {
		pthread_mutex_unlock(&lock);
		return -EEXIST;
	}
	/* create temp dir */
	if((tmp = get_temp_file(&location, mode, true)) < 0) {
		pthread_mutex_unlock(&lock);
		return tmp;
	}
	/* build node */
	if((node = init_node()) == NULL) {
		pthread_mutex_unlock(&lock);
		return -ENOMEM;
	}
	node->location    = location;
	node->modified    = 1;
	node->name        = strdup(path);
	node->basename    = strrchr(node->name, '/') + 1;
	node->namechanged = false;
	/* build entry */
	if(!root->children.empty() && node->name[0] == '/' && archive_entry_pathname(firstchild(root)->entry)[0] != '/') {
		archive_entry_set_pathname(node->entry, node->name + 1);
	} else {
		archive_entry_set_pathname(node->entry, node->name);
	}
	if((tmp = update_entry_stat(node)) < 0) {
		log("mkdir: error stat'ing dir %s: %s", node->location, strerror(-tmp));
		rmdir(location);
		free(location);
		free_node(node);
		pthread_mutex_unlock(&lock);
		return tmp;
	}
	/* add node to tree */
	if(insert_by_path(root, node) != 0) {
		log("ERROR: could not insert %s into tree", node->name);
		rmdir(location);
		free(location);
		free_node(node);
		pthread_mutex_unlock(&lock);
		return -ENOENT;
	}
	/* clean up */
	archiveModified = true;
	pthread_mutex_unlock(&lock);
	return 0;
}

/*
 * ar_rmdir is called for directories only and does not need to do any
 * recursive stuff
 */
 int FuseWrapper::ar_rmdir(const char * path) {
	NODE * node;

	log("ar_rmdir called, path '%s'", path);
	if(!archiveWriteable || options.readonly) {
		return -EROFS;
	}
	pthread_mutex_lock(&lock);
	node = get_node_for_path(root, path);
	if(!node) {
		pthread_mutex_unlock(&lock);
		return -ENOENT;
	}
	if(!node->children.empty()) {
		pthread_mutex_unlock(&lock);
		return -ENOTEMPTY;
	}
	if(node->basename == "."sv || node->basename == ".."sv) {
		pthread_mutex_unlock(&lock);
		return -EINVAL;
	}
	if(!S_ISDIR(archive_entry_mode(node->entry))) {
		pthread_mutex_unlock(&lock);
		return -ENOTDIR;
	}
	if(node->location) {
		/* remove temp directory */
		if(rmdir(node->location) == -1) {
			int err = errno;
			log("ERROR: removing temp directory %s failed: %s", node->location, strerror(err));
			pthread_mutex_unlock(&lock);
			return err;
		}
		free(node->location);
	}
	remove_child(node);
	free_node(node);
	archiveModified = true;
	pthread_mutex_unlock(&lock);
	return 0;
}

 int FuseWrapper::ar_symlink(const char * from, const char * to) {
	NODE * node;
	struct stat st;
	struct passwd * pwd;
	struct group * grp;

	log("symlink called, %s -> %s", from, to);
	if(!archiveWriteable || options.readonly) {
		return -EROFS;
	}
	pthread_mutex_lock(&lock);
	/* check for existing node */
	node = get_node_for_path(root, to);
	if(node) {
		pthread_mutex_unlock(&lock);
		return -EEXIST;
	}
	/* build node */
	if((node = init_node()) == NULL) {
		pthread_mutex_unlock(&lock);
		return -ENOMEM;
	}
	node->name     = strdup(to);
	node->basename = strrchr(node->name, '/') + 1;
	node->modified = true;
	/* build stat info */
	st.st_dev     = 0;
	st.st_ino     = 0;
	st.st_mode    = S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO;
	st.st_nlink   = 1;
	st.st_uid     = getuid();
	st.st_gid     = getgid();
	st.st_rdev    = 0;
	st.st_size    = strlen(from);
	st.st_blksize = sizeof(temp_io_buf);
	st.st_blocks  = 0;
	st.st_atime = st.st_ctime = st.st_mtime = time(NULL);
	/* build entry */
	if(!root->children.empty() && node->name[0] == '/' && archive_entry_pathname(firstchild(root)->entry)[0] != '/') {
		archive_entry_set_pathname(node->entry, node->name + 1);
	} else {
		archive_entry_set_pathname(node->entry, node->name);
	}
	archive_entry_copy_stat(node->entry, &st);
	archive_entry_set_symlink(node->entry, strdup(from));
	/* get user/group name */
	pwd = getpwuid(st.st_uid);
	if(pwd) {
		/* a name was found for the uid */
		archive_entry_set_uname(node->entry, strdup(pwd->pw_name));
	} else {
		if(errno == EINTR || errno == EIO || errno == EMFILE || errno == ENFILE || errno == ENOMEM || errno == ERANGE) {
			log("ERROR calling getpwuid: %s", strerror(errno));
			free_node(node);
			pthread_mutex_unlock(&lock);
			return -errno;
		}
		/* on other errors the uid just could
		   not be resolved into a name */
	}
	grp = getgrgid(st.st_gid);
	if(grp) {
		/* a name was found for the uid */
		archive_entry_set_gname(node->entry, strdup(grp->gr_name));
	} else {
		if(errno == EINTR || errno == EIO || errno == EMFILE || errno == ENFILE || errno == ENOMEM || errno == ERANGE) {
			log("ERROR calling getgrgid: %s", strerror(errno));
			free_node(node);
			pthread_mutex_unlock(&lock);
			return -errno;
		}
		/* on other errors the gid just could
		   not be resolved into a name */
	}
	/* add node to tree */
	if(insert_by_path(root, node) != 0) {
		log("ERROR: could not insert symlink %s into tree", node->name);
		free_node(node);
		pthread_mutex_unlock(&lock);
		return -ENOENT;
	}
	/* clean up */
	archiveModified = true;
	pthread_mutex_unlock(&lock);
	return 0;
}

 int FuseWrapper::ar_link(const char * from, const char * to) {
	NODE * node;
	NODE * fromnode;
	struct stat st;
	struct passwd * pwd;
	struct group * grp;

	log("ar_link called, %s -> %s", from, to);
	if(!archiveWriteable || options.readonly) {
		return -EROFS;
	}
	pthread_mutex_lock(&lock);
	/* find source node */
	fromnode = get_node_for_path(root, from);
	if(!fromnode) {
		pthread_mutex_unlock(&lock);
		return -ENOENT;
	}
	/* check for existing target */
	node = get_node_for_path(root, to);
	if(node) {
		pthread_mutex_unlock(&lock);
		return -EEXIST;
	}
	/* extract originals stat info */
	_ar_getattr(from, &st);
	/* build new node */
	if((node = init_node()) == NULL) {
		pthread_mutex_unlock(&lock);
		return -ENOMEM;
	}
	node->name     = strdup(to);
	node->basename = strrchr(node->name, '/') + 1;
	node->modified = true;
	/* build entry */
	if(node->name[0] == '/' && archive_entry_pathname(fromnode->entry)[0] != '/') {
		archive_entry_set_pathname(node->entry, node->name + 1);
	} else {
		archive_entry_set_pathname(node->entry, node->name);
	}
	archive_entry_copy_stat(node->entry, &st);
	archive_entry_set_hardlink(node->entry, strdup(from));
	/* get user/group name */
	pwd = getpwuid(st.st_uid);
	if(pwd) {
		/* a name was found for the uid */
		archive_entry_set_uname(node->entry, strdup(pwd->pw_name));
	} else {
		if(errno == EINTR || errno == EIO || errno == EMFILE || errno == ENFILE || errno == ENOMEM || errno == ERANGE) {
			log("ERROR calling getpwuid: %s", strerror(errno));
			free_node(node);
			pthread_mutex_unlock(&lock);
			return -errno;
		}
		/* on other errors the uid just could
		   not be resolved into a name */
	}
	grp = getgrgid(st.st_gid);
	if(grp) {
		/* a name was found for the uid */
		archive_entry_set_gname(node->entry, strdup(grp->gr_name));
	} else {
		if(errno == EINTR || errno == EIO || errno == EMFILE || errno == ENFILE || errno == ENOMEM || errno == ERANGE) {
			log("ERROR calling getgrgid: %s", strerror(errno));
			free_node(node);
			pthread_mutex_unlock(&lock);
			return -errno;
		}
		/* on other errors the gid just could
		   not be resolved into a name */
	}
	/* add node to tree */
	if(insert_by_path(root, node) != 0) {
		log("ERROR: could not insert hardlink %s into tree", node->name);
		free_node(node);
		pthread_mutex_unlock(&lock);
		return -ENOENT;
	}
	/* clean up */
	archiveModified = true;
	pthread_mutex_unlock(&lock);
	return 0;
}

 int FuseWrapper::realise_archived_file(const char * path, char ** location, struct fuse_file_info * fi, off_t entry_size, off_t max_size) {
	/* create new temp file */
	char * tmpbuf   = NULL;
	off_t tmpoffset = 0;
	int tmp, fh;
	if((fh = get_temp_file(location, (mode_t)-1, false)) < 0)
		return fh;

	/* copy original file to temporary file */
	if((tmpbuf = (char *)malloc(64 * 1024)) == NULL) {
		log("Out of memory");
		return -ENOMEM;
	}

	while(entry_size) {
		off_t len = entry_size > 64 * 1024 ? 64 * 1024 : entry_size;
		/* read */
		if((tmp = _ar_read(path, tmpbuf, len, tmpoffset, fi)) < 0) {
			log("ERROR reading while copying %s to temporary location %s: %s", path, *location, strerror(-tmp));
		err:
			close(fh);
			unlink(*location);
			free(tmpbuf);
			return tmp;
		}
		/* write */
		if(write(fh, tmpbuf, tmp) == -1) {
			tmp = -errno;
			log("ERROR writing while copying %s to temporary location %s: %s", path, *location, strerror(errno));
			goto err;
		}
		entry_size -= len;
		tmpoffset += len;
		if(max_size >= 0 && tmpoffset >= max_size) {
			/* copied enough, exit the loop */
			break;
		}
	}
	/* clean up */
	free(tmpbuf);
	return fh;
}

 int FuseWrapper::_ar_truncate(const char * path, off_t size) {
	NODE * node;
	char * location;
	int ret;
	int tmp;
	int fh;

	log("_ar_truncate called, path '%s'", path);
	if(!archiveWriteable || options.readonly) {
		return -EROFS;
	}
	node = get_node_for_path(root, path);
	if(!node) {
		return -ENOENT;
	}
	if(archive_entry_hardlink(node->entry)) {
		/* file is a hardlink, recurse into it */
		return _ar_truncate(archive_entry_hardlink(node->entry), size);
	}
	if(archive_entry_symlink(node->entry)) {
		/* file is a symlink, recurse into it */
		return _ar_truncate(archive_entry_symlink(node->entry), size);
	}
	if(node->location) {
		/* open existing temp file */
		location = node->location;
		if((fh = open(location, O_WRONLY | O_CLOEXEC)) == -1) {
			log("error opening temp file %s: %s", location, strerror(errno));
			unlink(location);
			return -errno;
		}
	} else {
		struct fuse_file_info fi;
		if((fh = realise_archived_file(path, &location, &fi, archive_entry_size(node->entry), size)) < 0)
			return fh;
	}
	/* truncate temporary file */
	if((ret = ftruncate(fh, size)) == -1) {
		tmp = -errno;
		log("ERROR truncating %s (temporary location %s): %s", path, location, strerror(errno));
		close(fh);
		unlink(location);
		return tmp;
	}
	/* record location, update entry */
	node->location = location;
	node->modified = true;
	if((tmp = update_entry_stat(node)) < 0) {
		log("write: error stat'ing file %s: %s", node->location, strerror(-tmp));
		close(fh);
		unlink(location);
		return tmp;
	}
	/* clean up */
	close(fh);
	archiveModified = true;
	return ret;
}

 int FuseWrapper::ar_truncate(const char * path, off_t size
#if FUSE_MAJOR_VERSION >= 3
                       ,
                       struct fuse_file_info *
#endif
) {
	int ret;
	log("ar_truncate called, path '%s'", path);
	pthread_mutex_lock(&lock);
	ret = _ar_truncate(path, size);
	pthread_mutex_unlock(&lock);
	return ret;
}

 int FuseWrapper::_ar_write(const char * path, const char * buf, size_t size, off_t offset, struct fuse_file_info * fi) {
	NODE * node;
	char * location;
	int ret;
	int tmp;
	int fh;

	log("_ar_write called, path '%s'", path);
	if(!archiveWriteable || options.readonly) {
		return -EROFS;
	}
	node = get_node_for_path(root, path);
	if(!node) {
		return -ENOENT;
	}
	if(S_ISLNK(archive_entry_mode(node->entry))) {
		/* file is a symlink, recurse into it */
		return _ar_write(archive_entry_symlink(node->entry), buf, size, offset, fi);
	}
	if(archive_entry_hardlink(node->entry)) {
		/* file is a hardlink, recurse into it */
		return _ar_write(archive_entry_hardlink(node->entry), buf, size, offset, fi);
	}
	if(archive_entry_symlink(node->entry)) {
		/* file is a symlink, recurse into it */
		return _ar_write(archive_entry_symlink(node->entry), buf, size, offset, fi);
	}
	if(node->location) {
		/* open existing temp file */
		location = node->location;
		if((fh = open(location, O_WRONLY | O_CLOEXEC)) == -1) {
			log("error opening temp file %s: %s", location, strerror(errno));
			unlink(location);
			return -errno;
		}
	} else {
		if((fh = realise_archived_file(path, &location, fi, archive_entry_size(node->entry), -1)) < 0)
			return fh;
	}
	/* write changes to temporary file */
	if((ret = pwrite(fh, buf, size, offset)) == -1) {
		tmp = -errno;
		log("ERROR writing changes to %s (temporary location %s): %s", path, location, strerror(errno));
		close(fh);
		unlink(location);
		return tmp;
	}
	/* record location, update entry */
	node->location = location;
	node->modified = true;
	if((tmp = update_entry_stat(node)) < 0) {
		log("write: error stat'ing file %s: %s", node->location, strerror(-tmp));
		close(fh);
		unlink(location);
		return tmp;
	}
	/* clean up */
	close(fh);
	archiveModified = true;
	return ret;
}

 int FuseWrapper::ar_write(const char * path, const char * buf, size_t size, off_t offset, struct fuse_file_info * fi) {
	int ret;
	log("ar_write called, path '%s'", path);
	pthread_mutex_lock(&lock);
	ret = _ar_write(path, buf, size, offset, fi);
	pthread_mutex_unlock(&lock);
	return ret;
}

 int FuseWrapper::ar_mknod(const char * path, mode_t mode, dev_t rdev) {
	NODE * node;
	char * location;
	int tmp;

	log("ar_mknod called, path %s", path);
	if(!archiveWriteable || options.readonly) {
		return -EROFS;
	}
	pthread_mutex_lock(&lock);
	/* check for existing node */
	node = get_node_for_path(root, path);
	if(node) {
		pthread_mutex_unlock(&lock);
		return -EEXIST;
	}
	/* create name for temp file */
	if((tmp = get_temp_node(&location, mode, rdev)) < 0) {
		pthread_mutex_unlock(&lock);
		return tmp;
	}
	/* build node */
	if((node = init_node()) == NULL) {
		pthread_mutex_unlock(&lock);
		return -ENOMEM;
	}
	node->location = location;
	node->modified = true;
	node->name     = strdup(path);
	node->basename = strrchr(node->name, '/') + 1;

	/* build entry */
	if(!root->children.empty() && node->name[0] == '/' && archive_entry_pathname(firstchild(root)->entry)[0] != '/') {
		archive_entry_set_pathname(node->entry, node->name + 1);
	} else {
		archive_entry_set_pathname(node->entry, node->name);
	}
	if((tmp = update_entry_stat(node)) < 0) {
		log("mknod: error stat'ing file %s: %s", node->location, strerror(0 - tmp));
		unlink(location);
		free(location);
		free_node(node);
		pthread_mutex_unlock(&lock);
		return tmp;
	}
	/* add node to tree */
	if(insert_by_path(root, node) != 0) {
		log("ERROR: could not insert %s into tree", node->name);
		unlink(location);
		free(location);
		free_node(node);
		pthread_mutex_unlock(&lock);
		return -ENOENT;
	}
	/* clean up */
	archiveModified = true;
	pthread_mutex_unlock(&lock);
	return 0;
}

 int FuseWrapper::_ar_unlink(const char * path) {
	NODE * node;

	log("_ar_unlink called, %s", path);
	if(!archiveWriteable || options.readonly) {
		return -EROFS;
	}
	node = get_node_for_path(root, path);
	if(!node) {
		return -ENOENT;
	}
	if(S_ISDIR(archive_entry_mode(node->entry))) {
		return -EISDIR;
	}
	if(node->location) {
		/* remove temporary file */
		if(unlink(node->location) == -1) {
			int err = errno;
			log("ERROR: could not unlink temporary file '%s': %s", node->location, strerror(err));
			return err;
		}
		free(node->location);
	}
	remove_child(node);
	free_node(node);
	archiveModified = true;
	return 0;
}

 int FuseWrapper::ar_unlink(const char * path) {
	log("ar_unlink called, path '%s'", path);
	int ret;
	pthread_mutex_lock(&lock);
	ret = _ar_unlink(path);
	pthread_mutex_unlock(&lock);
	return ret;
}

 int FuseWrapper::_ar_chmod(const char * path, mode_t mode) {
	NODE * node;

	log("_ar_chmod called, path '%s', mode: %o", path, mode);
	if(!archiveWriteable || options.readonly) {
		return -EROFS;
	}
	node = get_node_for_path(root, path);
	if(!node) {
		return -ENOENT;
	}
	if(archive_entry_hardlink(node->entry)) {
		/* file is a hardlink, recurse into it */
		return _ar_chmod(archive_entry_hardlink(node->entry), mode);
	}
	if(archive_entry_symlink(node->entry)) {
		/* file is a symlink, recurse into it */
		return _ar_chmod(archive_entry_symlink(node->entry), mode);
	}
#ifdef __APPLE__
	/* Make sure the full mode, including file type information, is used */
	mode = (0777000 & archive_entry_mode(node->entry)) | (0000777 & mode);
#endif  // __APPLE__
	archive_entry_set_mode(node->entry, mode);
	archiveModified = true;
	return 0;
}

 int FuseWrapper::ar_chmod(const char * path, mode_t mode
#if FUSE_MAJOR_VERSION >= 3
                    ,
                    struct fuse_file_info *
#endif
) {
	log("ar_chmod called, path '%s', mode: %o", path, mode);
	int ret;
	pthread_mutex_lock(&lock);
	ret = _ar_chmod(path, mode);
	pthread_mutex_unlock(&lock);
	return ret;
}

 int FuseWrapper::_ar_chown(const char * path, uid_t uid, gid_t gid) {
	NODE * node;

	log("_ar_chown called, %s", path);
	if(!archiveWriteable || options.readonly) {
		return -EROFS;
	}
	node = get_node_for_path(root, path);
	if(!node) {
		return -ENOENT;
	}
	if(archive_entry_hardlink(node->entry)) {
		/* file is a hardlink, recurse into it */
		return _ar_chown(archive_entry_hardlink(node->entry), uid, gid);
	}
	/* changing ownership of symlinks is allowed, however */
	archive_entry_set_uid(node->entry, uid);
	archive_entry_set_gid(node->entry, gid);
	archiveModified = true;
	return 0;
}

 int FuseWrapper::ar_chown(const char * path, uid_t uid, gid_t gid
#if FUSE_MAJOR_VERSION >= 3
                    ,
                    struct fuse_file_info *
#endif
) {
	log("ar_chown called, %s", path);
	int ret;
	pthread_mutex_lock(&lock);
	ret = _ar_chown(path, uid, gid);
	pthread_mutex_unlock(&lock);
	return ret;
}

 int FuseWrapper::_ar_utime(const char * path, const struct timespec tv[2]) {
	NODE * node;

	log("_ar_utime called, %s", path);
	if(!archiveWriteable || options.readonly) {
		return -EROFS;
	}
	node = get_node_for_path(root, path);
	if(!node) {
		return -ENOENT;
	}
	if(archive_entry_hardlink(node->entry)) {
		/* file is a hardlink, recurse into it */
		return _ar_utime(archive_entry_hardlink(node->entry), tv);
	}
	if(archive_entry_symlink(node->entry)) {
		/* file is a symlink, recurse into it */
		return _ar_utime(archive_entry_symlink(node->entry), tv);
	}
	archive_entry_set_atime(node->entry, tv[0].tv_sec, tv[0].tv_nsec);
	archive_entry_set_mtime(node->entry, tv[1].tv_sec, tv[1].tv_nsec);
	archiveModified = true;
	return 0;
}

 int FuseWrapper::ar_utimens(const char * path, const struct timespec tv[2]
#if FUSE_MAJOR_VERSION >= 3
                      ,
                      struct fuse_file_info *
#endif
) {
	log("ar_utimens called, %s", path);
	int ret;
	pthread_mutex_lock(&lock);
	ret = _ar_utime(path, tv);
	pthread_mutex_unlock(&lock);
	return ret;
}

 size_t FuseWrapper::count_nodes(NODE * node = root) {
	size_t ret = 1;
	for(auto && [_, child] : node->children)
		ret += count_nodes(child);
	return ret;
}
 int FuseWrapper::ar_statfs(const char *, struct statvfs * stbuf) {
	log("ar_statfs called");

	stbuf->f_namemax = 255;  // seems to be enforced by fuse; matches Linux

	stbuf->f_frsize = stbuf->f_bsize = BLOCK_SIZE;
	stbuf->f_blocks                  = (archiveFileSize + (BLOCK_SIZE - 1)) / BLOCK_SIZE;

	stbuf->f_files = count_nodes();
	return 0;
}

 int FuseWrapper::ar_rename(const char * from, const char * to
#if FUSE_MAJOR_VERSION >= 3
                     ,
                     unsigned flags
#endif
) {
	NODE * from_node;
	int ret = 0;
	char * temp_name;

	log("ar_rename called, from: '%s', to: '%s', flags=%x", from, to, flags);
	if(!archiveWriteable || options.readonly)
		return -EROFS;
#if FUSE_MAJOR_VERSION >= 3
	if(flags)
		return -EOPNOTSUPP;
#endif
	pthread_mutex_lock(&lock);
	from_node = get_node_for_path(root, from);
	if(!from_node) {
		pthread_mutex_unlock(&lock);
		return -ENOENT;
	}
	{
		/* before actually renaming the from_node, we must remove
		 * the to_node if it exists */
		NODE * to_node;
		to_node = get_node_for_path(root, to);
		if(to_node) {
			ret = _ar_unlink(to_node->name);
			if(0 != ret) {
				return ret;
			}
		}
	}
	/* meta data is changed in save() */
	/* change from_node name */
	if(*to != '/') {
		if(asprintf(&temp_name, "/%s", to) == -1) {
			log("Out of memory");
			pthread_mutex_unlock(&lock);
			return -ENOMEM;
		}
	} else {
		if((temp_name = strdup(to)) == NULL) {
			log("Out of memory");
			pthread_mutex_unlock(&lock);
			return -ENOMEM;
		}
	}
	remove_child(from_node);
	correct_hardlinks_to_node(from_node->name, temp_name);
	free(from_node->name);
	from_node->name        = temp_name;
	from_node->basename    = strrchr(from_node->name, '/') + 1;
	from_node->namechanged = true;
	ret                    = insert_by_path(root, from_node);
	if(0 != ret) {
		log("failed to re-insert node %s", from_node->name);
	}
	if(!from_node->children.empty()) {
		/* it is a directory, recursive change of all from_nodes
		 * below it is required */
		ret = rename_recursively(from_node, from, to);
	}
	archiveModified = true;
	pthread_mutex_unlock(&lock);
	return ret;
}

 int FuseWrapper::ar_readlink(const char * path, char * buf, size_t size) {
	NODE * node;
	const char * tmp;

	log("ar_readlink called, path '%s'", path);
	int ret = pthread_mutex_lock(&lock);
	if(ret) {
		fprintf(stderr, "could not acquire lock for archive: %s\n", strerror(ret));
		return ret;
	}
	node = get_node_for_path(root, path);
	if(!node) {
		pthread_mutex_unlock(&lock);
		return -ENOENT;
	}
	if(!S_ISLNK(archive_entry_mode(node->entry))) {
		pthread_mutex_unlock(&lock);
		return -ENOLINK;
	}
	tmp = archive_entry_symlink(node->entry);
	snprintf(buf, size, "%s", tmp);
	pthread_mutex_unlock(&lock);

	return 0;
}

 int FuseWrapper::ar_open(const char * path, struct fuse_file_info * fi) {
	NODE * node;

	log("ar_open called, path '%s'", path);
	int ret = pthread_mutex_lock(&lock);
	if(ret) {
		fprintf(stderr, "could not acquire lock for archive: %s\n", strerror(ret));
		return ret;
	}
	node = get_node_for_path(root, path);
	if(!node) {
		pthread_mutex_unlock(&lock);
		return -ENOENT;
	}
	if((fi->flags & O_ACCMODE) != O_RDONLY && !archiveWriteable) {
		pthread_mutex_unlock(&lock);
		return -EROFS;
	}
	/* no need to recurse into links since function doesn't do anything */
	fi->fh = 0;
	if(options.formatraw)
		_ar_open_raw();
	pthread_mutex_unlock(&lock);
	return 0;
}

 int FuseWrapper::ar_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t, struct fuse_file_info *
#if FUSE_MAJOR_VERSION >= 3
                      ,
                      enum fuse_readdir_flags
#endif
) {
	NODE * node;

	// log("ar_readdir called, path: '%s' offset: %d", path, offset);
	int ret = -EIO;
	if(pthread_mutex_lock(&lock)) {
		log("could not acquire lock for archive: %s\n", strerror(ret));
		return ret;
	}
	node = get_node_for_path(root, path);
	if(!node) {
		log("path '%s' not found", path);
		pthread_mutex_unlock(&lock);
		return -ENOENT;
	}

	filler(buf, ".", NULL, 0
#if FUSE_MAJOR_VERSION >= 3
	       ,
	       FUSE_FILL_DIR_PLUS
#endif
	);
	filler(buf, "..", NULL, 0
#if FUSE_MAJOR_VERSION >= 3
	       ,
	       FUSE_FILL_DIR_PLUS
#endif
	);

	for(auto && [_, child] : node->children) {
		/* Make a copy so we can set blocks/blksize. These are not
		 * set by libarchive. https://github.com/libarchive/libarchive/issues/302 */
		struct stat st;
		off_t entry_size_in_archive;
		if(archive_entry_hardlink(child->entry)) {
			/* file is a hardlink, stat'ing it somehow does not
			 * work; stat the original instead */
			NODE * orig = get_node_for_path(root, archive_entry_hardlink(child->entry));
			if(!orig) {
				pthread_mutex_unlock(&lock);
				return -ENOENT;
			}
			st                    = *archive_entry_stat(orig->entry);
			entry_size_in_archive = orig->entry_size_in_archive;
		} else {
			st                    = *archive_entry_stat(child->entry);
			entry_size_in_archive = child->entry_size_in_archive;
		}
		st.st_blocks  = (entry_size_in_archive + 511) / 512;
		st.st_blksize = sizeof(temp_io_buf);

		if(filler(buf, child->basename.data(), &st, 0
#if FUSE_MAJOR_VERSION >= 3
		          ,
		          FUSE_FILL_DIR_PLUS
#endif
		          )) {
			pthread_mutex_unlock(&lock);
			return -ENOMEM;
		}
	}

	pthread_mutex_unlock(&lock);
	return 0;
}


 int FuseWrapper::ar_create(const char * path, mode_t mode, struct fuse_file_info *) {
	NODE * node;
	char * location;
	int tmp;

	/* the implementation of this function is mostly copy-paste from
	   mknod, with the exception that the temp file is created with
	   creat() instead of mknod() */
	log("ar_create called, path '%s'", path);
	if(!archiveWriteable || options.readonly) {
		return -EROFS;
	}
	pthread_mutex_lock(&lock);
	/* check for existing node */
	node = get_node_for_path(root, path);
	if(node) {
		pthread_mutex_unlock(&lock);
		return -EEXIST;
	}
	/* create temp file */
	if((tmp = get_temp_file(&location, mode, false)) < 0) {
		pthread_mutex_unlock(&lock);
		return tmp;
	}
	/* build node */
	if((node = init_node()) == NULL) {
		pthread_mutex_unlock(&lock);
		return -ENOMEM;
	}
	node->location = location;
	node->modified = true;
	node->name     = strdup(path);
	node->basename = strrchr(node->name, '/') + 1;

	/* build entry */
	correct_name_in_entry(node);
	if((tmp = update_entry_stat(node)) < 0) {
		log("mknod: error stat'ing file %s: %s", node->location, strerror(0 - tmp));
		unlink(location);
		free(location);
		free_node(node);
		pthread_mutex_unlock(&lock);
		return tmp;
	}
	/* add node to tree */
	if(insert_by_path(root, node) != 0) {
		log("ERROR: could not insert %s into tree", node->name);
		unlink(location);
		free(location);
		free_node(node);
		pthread_mutex_unlock(&lock);
		return -ENOENT;
	}
	/* clean up */
	archiveModified = true;
	pthread_mutex_unlock(&lock);
	return 0;
}

void FuseWrapper::run(int argc, char* argv[]){
    struct stat st;
	int oldwd             = -1;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	/* parse cmdline args */
	if(fuse_opt_parse(&args, &fs.options, this->ar_opts, ar_opt_proc) == -1)
		return -1;
	if(fs.archiveFile == NULL) {
		fs.usage(argv[0]);
		return (1);
	}
	if(fs.mtpt == NULL) {
		fs.usage(argv[0]);
		return (1);
	}

	/* check if mtpt is ok and writeable */
	if(stat(mtpt, &st) != 0) {
		lerr("%s: %s", mtpt, strerror(errno));
		return (1);
	}
	// https://github.com/libfuse/libfuse/commit/64e11073b9347fcf9c6d1eea143763ba9e946f70
	if(!strncmp(mtpt, "/dev/fd/", sizeof("/dev/fd/") - 1))	// mtpt имеет вид /dev/fd/N то 
		st.st_mode = (st.st_mode & ~S_IFMT) | S_IFDIR; //  заставляем линукс думать что это директория
	else if(!S_ISDIR(st.st_mode)) {		// Иначе проверяем чтоб mtpt был директорией
		lerr("%s: %s", mtpt, strerror(ENOTDIR));
		return (1);
	}

	if(options.password) { // Пока не оч разбирал 
		struct termios orig = noEcho();
		fputs("Enter passphrase: ", stderr);
		size_t user_passphrase_size;
		getPassphrase(&user_passphrase, &user_passphrase_size, stdin);
		fputs("\n", stderr);
		tcsetattr(0, TCSANOW, &orig);
	}

	if(options.formatraw)
		options.readonly = true;
	if(options.readonly)
		fuse_opt_add_arg(&args, "-r");	// Не разбирал
	else
		archiveWriteable = options.nosave || (access(archiveFile, W_OK) == 0);

	/* open archive and read meta data */
	archiveFd = open(archiveFile, O_RDONLY | O_CLOEXEC);
	if(archiveFd == -1) {
		lerr("%s: %s", archiveFile, strerror(errno));
		return 1;
	}
	if(build_tree(st.st_mode) != 0) {
		return (1);
	}
	if(options.formatraw) {
		/* create rawcache */
		rawcache.st_size = _ar_getsizeraw(firstchild(root)->name);
		// log("cache st_size = %ld",rawcache.st_size);
	}

#ifndef O_PATH
#define O_PATH O_RDONLY
#endif
	/* save directory this was started from */
	if(!options.readonly && !options.nosave)
		oldwd = open(".", O_PATH | O_CLOEXEC);		// Открывают текущую директорию, но зачем?

	/* Initialize the node tree lock */
	pthread_mutex_init(&lock, NULL);

	/* always use fuse in single-threaded mode
	 * multithreading is broken with libarchive :-( LOL
	 */
	fuse_opt_add_arg(&args, "-s");
	fuse_opt_add_arg(&args, "-o");
	fuse_opt_add_arg(&args, "default_permissions");
	
	fuse_main(args.argc, args.argv, &ar_oper, NULL);

	/* save changes if modified; must be in original directory (libarchive can chdir) */
	if(archiveModified) {
		if(!options.nosave) {
			if(fchdir(oldwd))
				fprintf(stderr, "fchdir() to old path failed, can't save new archive\n");
			else if(int err = save(archiveFile); err)
				fprintf(stderr, "Saving new archive failed: %s\n", strerror(-err));
		}

		nosave();
	}

	return 0;
}