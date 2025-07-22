#include "ArchiveFS.hpp"

ArchiveFS::ArchiveFS() 
    : root(nullptr),
    archiveFd(-1),
    mtpt(nullptr),
    archiveFile(nullptr),
    user_passphrase(nullptr),
    tmpdir_for_nodes(nullptr),
    last_open_node{nullptr, nullptr, 0}{
		memset(&optionsInstance, 0, sizeof(optionsInstance));
	}

ArchiveFS::~ArchiveFS() {
    if (archiveFile) {
        //free(archiveFile);
        archiveFile = nullptr;
    }
    if (mtpt) {
        //free(mtpt);
        mtpt = nullptr;
    }
}

void ArchiveFS::usage(const char * progname) {
	fprintf(stderr,
	        "usage: %s archivepath mountpoint [options]\n"
	        "\n"
	        "general options:\n"
	        "    -o opt[,opt]...	    mount options\n"
	        "    -h   --help		    print help\n"
	        "    -V   --version	    print version\n"
	        "\n"
	        "archivemount options:\n"
	        "    -o readonly, -o ro, -r  disable write support\n"
	        "    -o password		    prompt for a password.\n"
	        "    -o nobackup		    remove archive file backups\n"
	        "    -o nosave		    do not save changes upon unmount.\n"
	        "			    Good if you want to change something\n"
	        "			    and save it as a diff,\n"
	        "			    or use a format for saving which is\n"
	        "			    not supported by archivemount.\n"
	        "\n"
	        "    -o subtree=regexp       use only subtree matching ^\\.\\?regexp from archive\n"
	        "			    it implies readonly\n"
	        "\n"
	        "    -o formatraw	    treat input as a single element archive\n"
	        "			    it implies readonly\n",
	        progname);
}

NODE * ArchiveFS::init_node() { // Y
	NODE * node;

	if((node = (NODE *)malloc(sizeof(NODE))) == NULL) {
		lerrno();
		return NULL;
	}

	node = new(node) NODE{.entry = archive_entry_new()};

	if(node->entry == NULL) {
		lerrno();
		//node->~node();
		free(node);
		return NULL;
	}

	return node;
}

void ArchiveFS::free_node(NODE * node) { // Y
	free(node->name);
	archive_entry_free(node->entry);
	//node->~node();
	free(node);
}

void ArchiveFS::remove_child(NODE * node) { // Y
	if(node->parent) {
		node->parent->children.erase(node->basename);
		log("removed '%s' from parent '%s'", node->name, node->parent->name);
	} else {
		root = NULL;
	}
}

void ArchiveFS::insert_as_child(NODE * node, NODE * parent) { 
	node->parent = parent;
	parent->children.insert({node->basename, node});
	log("inserted '%s' as child of '%s'", node->name, parent->name);
}

int ArchiveFS::insert_by_path(NODE * node) { 
	char * temp;
	NODE * cur = root;
	char * key = node->name;

	key++;
	while((temp = strchr(key, '/'))) {
		size_t namlen = temp - key;
		NODE * last   = cur;

		std::string_view basename{key, namlen};
		if(auto found = cur->children.find(basename); found != std::end(cur->children)) {
			cur = found->second;
		} else {
			/* parent path not found, create a temporary one */
			NODE * tempnode;
			if((tempnode = init_node()) == NULL)
				return -ENOMEM;

			if(asprintf(&tempnode->name, "%s/%.*s", last != root ? last->name : "", (int)basename.size(), basename.data()) == -1) {
				lerrno();
				return -errno;
			}
			tempnode->basename = strrchr(tempnode->name, '/') + 1;

			archive_entry_free(tempnode->entry);

			if((tempnode->entry = archive_entry_clone(root->entry)) == NULL) {
				lerrnum(ENOMEM);
				return -ENOMEM;
			}
			/* insert it recursively */
			insert_by_path(tempnode);
			/* now inserting node should work, correct cur for it */
			cur = tempnode;
		}
		/* iterate */
		key = temp + 1;
	}
	if(S_ISDIR(archive_entry_mode(cur->entry))) {
		/* check if a child of this name already exists */
		auto found = cur->children.find(node->basename);

		if(found != std::end(cur->children)) {
			/* this is a dupe due to a temporarily inserted
			   node, just update the entry */
			archive_entry_free(node->entry);
			if((node->entry = archive_entry_clone(found->second->entry)) == NULL) {
				lerrno();
				return -errno;
			}
		} else {
			insert_as_child(node, cur);
		}
	} else {
		return -ENOTDIR;
	}
	return 0;
}

bool ArchiveFS::archive_prepopen(struct archive * archiveInstance) { // Y
	if(archive_read_support_filter_all(archiveInstance) != ARCHIVE_OK) {
	err:
		lerr("%s", archive_error_string(archiveInstance));
		return false;
	}
	if((optionsInstance.formatraw ? archive_read_support_format_raw : archive_read_support_format_all)(archiveInstance) != ARCHIVE_OK)
		goto err;
	if(optionsInstance.password && archive_read_add_passphrase(archiveInstance, user_passphrase) != ARCHIVE_OK)
		goto err;
	if(archive_read_open_fd(archiveInstance, archiveFd, BLOCK_SIZE) != ARCHIVE_OK)
		goto err;
	return true;
}

 uint64_t ArchiveFS::total_entry_size_in_archive(NODE * node) { // Y?
	if (node == nullptr) node = root;
	uint64_t ret = node->entry_size_in_archive;
	for(auto && [_, child] : node->children)
		ret += total_entry_size_in_archive(child);
	return ret;
}
 void ArchiveFS::redistribute_entry_size_in_archive(double scale, NODE * node) { // Y?
	if (node == nullptr) node = root;
	node->entry_size_in_archive = node->entry_size_in_archive * scale;
	for(auto && [_, child] : node->children)
		redistribute_entry_size_in_archive(scale, child);
}
 int ArchiveFS::build_tree(mode_t mtpt_mode) { // Y
	archive * archiveInstance;
	struct stat st;
	int format;
	int compression;
	NODE * cur;
	char * subtree_filter = NULL;
	regex_t subtree;
	int regex_error;
	regmatch_t regmatch;
	
	if (!root) {
        root = init_node();
        root->name = strdup("/");
        root->basename = root->name;
    }


#define PREFIX "^\\.\\?"
	if(optionsInstance.subtree_filter) {
		if(asprintf(&subtree_filter, PREFIX "%s", optionsInstance.subtree_filter) == -1) {
			lerrno();
			return -errno;
		}
		/* \? is only a special char on Mac if REG_ENHANCED is specified  */
#ifndef REG_ENHANCED
#define REG_ENHANCED 0
#endif
		if((regex_error = regcomp(&subtree, subtree_filter, REG_ENHANCED))) {
			int es  = regerror(regex_error, &subtree, NULL, 0);
			auto eb = (char *)malloc(es);
			if(eb)
				regerror(regex_error, &subtree, eb, es);
			lerr("regex error%s%s\n", eb ? ": " : "", eb);
			free(eb);
			return -EINVAL;
		}
		free(subtree_filter);
		optionsInstance.readonly = 1;
	}
	/* open archive */
	if((archiveInstance = archive_read_new()) == NULL) {
		lerrnum(ENOMEM);
		return -ENOMEM;
	}

	printf("Building tree for archive: %s\n", archiveFile);
	printf("Root node: %p\n", root);
	printf("Archive fd: %d\n", archiveFd);
	printf("Archive ptr: %p\n", archiveInstance);

	if(!archive_prepopen(archiveInstance)){
		printf("!archive_prepopen(archiveInstance)");
		return archive_errno(archiveInstance);
	}
		
	/* check if format or compression prohibits writability */
	format = archive_format(archiveInstance);
	log("mounted archive format is %s (0x%x)", archive_format_name(archiveInstance), format);
	compression = archive_filter_code(archiveInstance, 0);
	log("mounted archive compression is %s (0x%x)", archive_filter_name(archiveInstance, 0), compression);
	if(format & ARCHIVE_FORMAT_ISO9660 || format & ARCHIVE_FORMAT_ISO9660_ROCKRIDGE || format & ARCHIVE_FORMAT_ZIP ||
	   compression == ARCHIVE_COMPRESSION_COMPRESS) {
		archiveWriteable = false;
	}
	/* create root node */
	if((root = init_node()) == NULL)
		return -ENOMEM;

	root->name     = strdup("/");
	root->basename = &root->name[1];

	/* fill root->entry */
	fstat(archiveFd, &st);
	archive_entry_set_gid(root->entry, getgid());
	archive_entry_set_uid(root->entry, getuid());
	archive_entry_set_mtime(root->entry, st.st_mtim.tv_sec, st.st_mtim.tv_nsec);
	archive_entry_set_pathname(root->entry, "/");
	archive_entry_set_size(root->entry, st.st_size);
	archive_entry_set_mode(root->entry, mtpt_mode);

	if((cur = init_node()) == NULL) {
		return -ENOMEM;
	}

	/* read all entries in archive, create node for each */
	//off_t *lastpos;
	off_t pos = archive_read_header_position(archiveInstance), *lastpos{};		// Инициализация смещения по первому заголовку
	
	while(archive_read_next_header2(archiveInstance, cur->entry) == ARCHIVE_OK) {		// Пока можно прочитать заголовок следующего элемента
		off_t curpos = archive_read_header_position(archiveInstance);
		
		if(lastpos)
			*lastpos = curpos - pos;
		lastpos = &cur->entry_size_in_archive;
		pos     = curpos;

		const char * name = archive_entry_pathname(cur->entry);
		if(memcmp(name, "./", sizeof("./")) == 0) {
			/* special case: the directory "./" must be skipped! */
			continue;
		}
		if(optionsInstance.subtree_filter) {
			if(regexec(&subtree, name, 1, &regmatch, REG_NOTEOL) == REG_NOMATCH)
				continue;

			/* strip subtree from name */
			name += regmatch.rm_eo;
		}
		/* create node and clone the entry */
		/* normalize the name to start with "/" */
		if(strncmp(name, "./", 2) == 0) {
			/* remove the "." of "./" */
			cur->name = strdup(name + 1);
		} else if(name[0] != '/') {
			/* prepend a '/' to name */
			if(asprintf(&cur->name, "/%s", name) == -1)
				cur->name = NULL;
		} else {
			/* just set the name */
			cur->name = strdup(name);
		}
		if(!cur->name) {
			lerrno();
			return -errno;
		}
		//printf("\n cur->name: %s\n", cur->name);
		auto len = strlen(cur->name);
		len      = std::unique(cur->name, cur->name + len, [](char l, char r) { return l == '/' && r == '/'; }) - cur->name;
		if(cur->name[len - 1] == '/')
			--len;
		cur->name[len] = '\0';
		if(len > 0) {
			/* remove trailing '/' for directories */
			cur->basename = strrchr(cur->name, '/') + 1;

			// ar archives have S_IFMT bits clear (https://github.com/libarchive/libarchive/issues/2241)
			if(auto mode = archive_entry_mode(cur->entry); (mode & S_IFMT) == 0)
				archive_entry_set_mode(cur->entry, mode | S_IFREG);

			/* references */
			if(int err; (err = insert_by_path(cur))) {
				lerr("ERROR: could not insert %s into tree: %s", cur->name, strerror(-err));
				return -ENOENT;
			}
			printf("\nroot->children: \n");
			

			if((cur = init_node()) == NULL)
				return -ENOMEM;
		} else {
			/* this is the directory the subtree filter matches, or a root directory, do not respect it */
		}

		archive_read_data_skip(archiveInstance);
	}
	off_t curpos = archive_read_header_position(archiveInstance);
	if(lastpos)
		*lastpos = curpos - pos;
	/* free the last unused NODE */
	free_node(cur);

	archiveFileSize = archive_filter_bytes(archiveInstance, -1);
	// Right now entry_size_in_archive is /uncompressed/: this is what archive_read_header_position() returns
	// Accounting by archive_filter_bytes(_, -1) would yield 1234000, 0, 0, 0, 0, 1234000, 0, 0, 0, 0
	// Redistribute proportionally. Not perfect
	auto total = total_entry_size_in_archive();
	redistribute_entry_size_in_archive((double)archiveFileSize / (double)total);

	/* close archive */
	archive_read_free(archiveInstance);
	lseek(archiveFd, 0, SEEK_SET);
	if(optionsInstance.subtree_filter)
		regfree(&subtree);
	return 0;
}

 NODE * ArchiveFS::find_modified_node(NODE * start) {
	if(start->modified)
		return start;

	for(auto && [_, child] : start->children) {
		if(auto ret = find_modified_node(child))
			return ret;
	}
	return NULL;
}

 void ArchiveFS::correct_hardlinks_to_node(const char * old_name, const char * new_name, NODE * from) {
	if (from == nullptr) from = root;
	for(auto && [_, child] : from->children) {
		const char * tmp = archive_entry_hardlink(child->entry);
		if(tmp && strcmp(tmp, old_name) == 0) {
			/* the child in "child" is a hardlink to "child", correct the path */
			// log("correcting hardlink '%s' from '%s' to '%s'", child->name, old_name, new_name);
			archive_entry_set_hardlink(child->entry, new_name);
		}

		correct_hardlinks_to_node(old_name, new_name, child);
	}
}

 NODE * ArchiveFS::firstchild(NODE * node) { // Y?
	return std::begin(node->children)->second;
}

 void ArchiveFS::correct_name_in_entry(NODE * node) { //
	if(!root->children.empty() && node->name[0] == '/' && archive_entry_pathname(firstchild(root)->entry)[0] != '/') {
		log("correcting name in entry to '%s'", node->name + 1);
		archive_entry_set_pathname(node->entry, node->name + 1);
	} else {
		log("correcting name in entry to '%s'", node->name);
		archive_entry_set_pathname(node->entry, node->name);
	}
}

 NODE * ArchiveFS::get_node_for_path(NODE * start, const char * path) { // Y?
	// log("get_node_for_path path: '%s' start: '%s'", path, start->name);

	/* Check if start is a perfect match */
	if(strcmp(path, start->name + (*path == '/' ? 0 : 1)) == 0) {
		// log("  get_node_for_path path: '%s' start: '%s' return: '%s'", path, start->name, start->name);
		return start;
	}

	/* Check if one of the children match */
	if(!start->children.empty()) {
		/* Find the part of the path we are now looking for */
		const char * basename = path + strlen(start->name) - (*path == '/' ? 0 : 1);
		if(basename[0] == '/')
			basename++;

		std::string_view bname{basename};
		if(auto idx = bname.find('/'); idx != std::string_view::npos)
			bname.remove_suffix(bname.size() - idx);

		// log("get_node_for_path path: '%s' start: '%s' basename: '%s' len: %ld", path, start->name, basename, baseend - basename);
		auto found = start->children.find(bname);
		if(found != std::end(start->children))
			return get_node_for_path(found->second, path);
	}

	// log("  get_node_for_path path: '%s' start: '%s' return: '%s'", path, start->name, ret == NULL ? "(null)" : ret->name);
	return NULL;
}

 NODE * ArchiveFS::get_node_for_entry_inner(NODE * under, const char * path) {
	for(auto && [_, child] : under->children) {
		const char * name = archive_entry_pathname(child->entry);
		if(*name == '/')
			++name;

		if(!strcmp(path, name))
			return child;

		if(auto ret = get_node_for_entry_inner(child, path))
			return ret;
	}
	return NULL;
}
 NODE * ArchiveFS::get_node_for_entry(NODE * under, struct archive_entry * entry) {
	const char * path = archive_entry_pathname(entry);
	if(*path == '/')
		++path;

	return get_node_for_entry_inner(under, path);
}

 int ArchiveFS::rename_recursively(NODE * under, const char * from, const char * to) {
	char * individualName;
	char * newName;
	int ret = 0;
	/* removing and re-inserting nodes while iterating through
	   the hashtable is a bad idea, so we copy all node ptrs
	   into an array first and iterate over that instead */
	size_t count = under->children.size();
	log("%s has %zu items", under->name, count);

	auto nodes = (NODE **)alloca(sizeof(NODE *) * count), itr = nodes;
	for(auto && [_, child] : under->children)
		*itr++ = child;

	for(size_t i = 0; i < count; ++i) {
		NODE * node = nodes[i];
		if(!node->children.empty())
			ret = rename_recursively(node, from, to);

		remove_child(node);
		/* change node name */
		individualName = node->name + strlen(from);
		if(asprintf(&newName, "%s%s%s", *to != '/' ? "/" : "", to, individualName) == -1) {
			lerrno();
			return -errno;
		}
		log("new name: '%s'", newName);
		correct_hardlinks_to_node(node->name, newName);
		free(node->name);
		node->name        = newName;
		node->basename    = strrchr(node->name, '/') + 1;
		node->namechanged = true;
		insert_by_path(root);
	}
	return ret;
}

 const char * const tmpdir = getenv("TMPDIR") ?: P_tmpdir;
 int ArchiveFS::get_temp_file(char ** location, mode_t mode, bool directory) {
	int fh{};
	/* create name for temp file */
	if(asprintf(location, "%s/archivemount.XXXXXXXXXX", tmpdir) == -1)
		return -errno;
	if(directory) {
		if(!mkdtemp(*location))
			goto err;
	} else {
		if((fh = mkstemp(*location)) == -1) {
		err:
			lerr("%s: %s", *location, strerror(errno));
			free(*location);
			*location = NULL;
			return -errno;
		}
	}
	if(mode != (mode_t)-1 && chmod(*location, mode) == -1)
		goto err;
	return fh;
}
 
 int ArchiveFS::get_temp_node(char ** location, mode_t mode, dev_t dev) {
	if(!tmpdir_for_nodes)
		if(int err = this->get_temp_file(&tmpdir_for_nodes, (mode_t)-1, true))
			return err;

	if(asprintf(location, "%s/%" PRIu64 "", tmpdir_for_nodes, tmpdir_for_nodes_children++) == -1)
		return -errno;
	if(mknod(*location, mode, dev) == -1) {
		lerr("%s: %s", *location, strerror(errno));
		free(*location);
		*location = NULL;
		return -errno;
	}
	return 0;
}

/**
 * Updates given nodes node->entry by stat'ing node->location. Does not update
 * the name!
 */
 int ArchiveFS::update_entry_stat(NODE * node) {
	struct stat st;
	struct passwd * pwd;
	struct group * grp;

	if(lstat(node->location, &st) != 0) {
		return -errno;
	}
	archive_entry_set_gid(node->entry, st.st_gid);
	archive_entry_set_uid(node->entry, st.st_uid);
	archive_entry_set_mtime(node->entry, st.st_mtime, 0);
	archive_entry_set_size(node->entry, st.st_size);
	archive_entry_set_mode(node->entry, st.st_mode);
	archive_entry_set_rdev(node->entry, st.st_rdev);
	pwd = getpwuid(st.st_uid);
	if(pwd)
		archive_entry_set_uname(node->entry, pwd->pw_name);
	grp = getgrgid(st.st_gid);
	if(grp)
		archive_entry_set_gname(node->entry, grp->gr_name);
	return 0;
}

/*
 * write a new or modified file to the new archive; used from save()
 */
// Определение thread-local переменной
thread_local char ArchiveFS::temp_io_buf[64 * 1024];
 void ArchiveFS::write_new_modded_file(NODE * node, struct archive_entry * wentry, struct archive * newarc) {
	if(node->location) {
		struct stat st;
		int fh       = -1;
		off_t offset = 0;
		ssize_t len  = 0;
		/* copy stat info */
		if(lstat(node->location, &st) != 0) {
			lerr("Could not lstat temporary file %s: %s", node->location, strerror(errno));
			return;
		}
		archive_entry_copy_stat(wentry, &st);
		/* write header */
		archive_write_header(newarc, wentry);
		if(S_ISREG(st.st_mode)) {
			/* open temporary file */
			fh = open(node->location, O_RDONLY | O_CLOEXEC);
			if(fh == -1) {
				lerr("Fatal error opening modified file %s at location %s, giving up", node->name, node->location);
				return;
			}
			/* regular file, copy data */
			while((len = pread(fh, temp_io_buf, sizeof(temp_io_buf), offset)) > 0) {
				archive_write_data(newarc, temp_io_buf, len);
				offset += len;
			}
			close(fh);
		}
		if(len == -1) {
			lerr("Error reading temporary file %s for file %s: %s", node->location, node->name, strerror(errno));
			return;
		}
	} else {
		/* no data, only write header (e.g. when node is a link!) */
		// log("writing header for file %s", archive_entry_pathname(wentry));
		archive_write_header(newarc, wentry);
	}
	/* mark file as written */
	node->modified = false;
}

 int ArchiveFS::save(const char * archiveFile) { // Y?
	struct archive * oldarc;
	struct archive * newarc;
	struct archive_entry * entry;
	int tempfile;
	int format;
	int compression;
	char * oldfilename;
	NODE * node;

	/* unfortunately libarchive does not support modification of
	 * compressed archives, so a new archive has to be written */
	/* rename old archive */
	if(asprintf(&oldfilename, "%s.orig", archiveFile) == -1) {
		lerrno();
		return -errno;
	}
	if(last_open_node.archiveInstance) {
		archive_read_free(last_open_node.archiveInstance);
		last_open_node.archiveInstance = NULL;
	}
	close(archiveFd);
	if(rename(archiveFile, oldfilename) == -1) {
		int err    = errno;
		char * buf = getcwd(NULL, 0);
		log("Could not rename old archive file (%s/%s): %s", buf ?: "<unknown>", archiveFile, strerror(err));
		free(buf);
		archiveFd = open(archiveFile, O_RDONLY | O_CLOEXEC);
		return -err;
	}
	archiveFd = open(oldfilename, O_RDONLY | O_CLOEXEC);
	free(oldfilename);
	/* open old archive */
	if((oldarc = archive_read_new()) == NULL) {
		lerrnum(ENOMEM);
		return -ENOMEM;
	}
	if(!archive_prepopen(oldarc))
		return archive_errno(oldarc);
	/* Read first header of oldarc so that archive format is set. */
	if(archive_read_next_header(oldarc, &entry) != ARCHIVE_OK) {
		lerr("%s", archive_error_string(oldarc));
		return archive_errno(oldarc);
	}
	format      = archive_format(oldarc);
	compression = archive_filter_code(oldarc, 0);
	log("mounted archive format is %s (0x%x)", archive_format_name(oldarc), format);
	log("mounted archive compression is %s (0x%x)", archive_filter_name(oldarc, 0), compression);
	/* open new archive */
	if((newarc = archive_write_new()) == NULL) {
		lerrnum(ENOMEM);
		return -ENOMEM;
	}
	archive_write_add_filter(newarc, compression);
	if(archive_write_set_format(newarc, format) != ARCHIVE_OK) {
		lerr("writing archives of format %d (%s) is not supported", format, archive_format_name(oldarc));
		return -ENOTSUP;
	}
	tempfile = open(archiveFile, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
	if(tempfile == -1) {
		lerr("could not open new archive file for writing: %s", strerror(errno));
		return -errno;
	}
	if(optionsInstance.password) {
		/* When libarchive gains support for multiple kinds of encryption and
		 * an API to say which kind is in use, this should use copy oldarc's
		 * encryption settings.  For now, just set the one kind of encryption
		 * that libarchive supports. */
		if(archive_write_set_options(newarc, "zip:encryption=aes256") != ARCHIVE_OK) {
			lerr("Could not set encryption for new archive: %s", archive_error_string(newarc));
			return archive_errno(newarc);
		}
		if(archive_write_set_passphrase(newarc, user_passphrase) != ARCHIVE_OK) {
			lerr("Could not set passphrase for new archive: %s", archive_error_string(newarc));
			return archive_errno(newarc);
		}
	}
	if(archive_write_open_fd(newarc, tempfile) != ARCHIVE_OK) {
		lerr("%s", archive_error_string(newarc));
		return archive_errno(newarc);
	}
	do {
		off_t offset;
		const void * buf;
		struct archive_entry * wentry;
		size_t len;
		const char * name;
		/* find corresponding node */
		name = archive_entry_pathname(entry);
		node = get_node_for_entry(root, entry);
		if(!node) {
			log("WARNING: no such node for '%s'", name);
			archive_read_data_skip(oldarc);
			continue;
		}
		/* create new entry, copy metadata */
		if((wentry = archive_entry_new()) == NULL) {
			lerrnum(ENOMEM);
			return -ENOMEM;
		}
		if(archive_entry_gname_w(node->entry)) {
			archive_entry_copy_gname_w(wentry, archive_entry_gname_w(node->entry));
		}
		if(archive_entry_hardlink(node->entry)) {
			archive_entry_copy_hardlink(wentry, archive_entry_hardlink(node->entry));
		}
		if(archive_entry_hardlink_w(node->entry)) {
			archive_entry_copy_hardlink_w(wentry, archive_entry_hardlink_w(node->entry));
		}
		archive_entry_copy_stat(wentry, archive_entry_stat(node->entry));
		if(archive_entry_symlink_w(node->entry)) {
			archive_entry_copy_symlink_w(wentry, archive_entry_symlink_w(node->entry));
		}
		if(archive_entry_uname_w(node->entry)) {
			archive_entry_copy_uname_w(wentry, archive_entry_uname_w(node->entry));
		}
		/* set correct name */
		if(node->namechanged) {
			if(*name == '/') {
				archive_entry_set_pathname(wentry, node->name);
			} else {
				archive_entry_set_pathname(wentry, node->name + 1);
			}
		} else {
			archive_entry_set_pathname(wentry, name);
		}
		/* write header and copy data */
		if(node->modified) {
			/* file was modified */
			write_new_modded_file(node, wentry, newarc);
		} else {
			/* file was not modified */
			archive_entry_copy_stat(wentry, archive_entry_stat(node->entry));
			archive_write_header(newarc, wentry);
			while(archive_read_data_block(oldarc, &buf, &len, &offset) == ARCHIVE_OK) {
				archive_write_data(newarc, buf, len);
			}
		}
		/* clean up */
		archive_entry_free(wentry);
	} while(archive_read_next_header(oldarc, &entry) == ARCHIVE_OK);
	/* find new files to add (those do still have modified flag set */
	while((node = find_modified_node(root))) {
		if(node->namechanged) {
			correct_name_in_entry(node);
		}
		write_new_modded_file(node, node->entry, newarc);
	}
	/* clean up, re-open the new archive for reading */
	archive_read_free(oldarc);
	archive_write_free(newarc);
	close(tempfile);
	close(archiveFd);
	archiveFd = open(archiveFile, O_RDONLY | O_CLOEXEC);
	if(optionsInstance.nobackup) {
		if(unlink(oldfilename) == -1) {
			lerr("Could not remove .orig archive file (%s): %s", oldfilename, strerror(errno));
			return -errno;
		}
	}
	return 0;
}

// Kill temporary files
 void ArchiveFS::nosave(NODE * node) { // Y?
	
	if(node->location) {
		auto st = archive_entry_stat(node->entry);
		if(S_ISDIR(st->st_mode)) {
			if(rmdir(node->location) == -1)
				lerr("WARNING: rmdir '%s' failed: %s", node->location, strerror(errno));
		} else {
			if(unlink(node->location) == -1)
				lerr("WARNING: unlinking '%s' failed: %s", node->location, strerror(errno));
			if(tmpdir_for_nodes && !S_ISREG(st->st_mode))
				rmdir(tmpdir_for_nodes);
		}
	}
	for(auto && [_, child] : node->children)
		nosave(child);
}

size_t ArchiveFS::count_nodes(NODE * node) {
	size_t ret = 1;
	for(auto && [_, child] : node->children)
		ret += count_nodes(child);
	return ret;
}