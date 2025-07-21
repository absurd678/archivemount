//#ifndef FUSEWRAPPER_HPP
#pragma once
#define FUSEWRAPPER_HPP

#include "Common.hpp"
#include "ArchiveFS.hpp"
#include <csignal>

#if __APPLE__
#define st_mtim st_mtimespec
#endif



#define AR_OPT(t, p, v) {t, offsetof(struct options, p), v}


struct FORMATRAW_CACHE {
	off_t st_size;
} ;


class FuseWrapper{

public:
// --------------------Поля-------------------------
    FORMATRAW_CACHE rawcache;
    bool archiveModified;
    pthread_mutex_t lock;

    
    
    ArchiveFS fs;   // объект класса дерева файловой структуры


    FuseWrapper(ArchiveFS& obj);

    
    int ar_opt_proc(void *, const char * arg, int key, struct fuse_args * outargs);

    void _ar_open_raw(); //_ar_open_raw(const char *path, struct fuse_file_info *fi)
    int _ar_read_archive_found_common(char * buf, size_t size, off_t offset);
    int _ar_read_raw(const char * path, char * buf, size_t size, off_t offset, struct fuse_file_info *);
    int _ar_read(const char * path, char * buf, size_t size, off_t offset, struct fuse_file_info * fi);
    int ar_read(const char * path, char * buf, size_t size, off_t offset, struct fuse_file_info * fi);
    
    off_t _ar_getsizeraw(const char * path);
    
    int _ar_getattr(const char * path, struct stat * stbuf);
    int ar_getattr(const char * path, struct stat * stbuf
#if FUSE_MAJOR_VERSION >= 3
                      ,
                      struct fuse_file_info *
#endif
    );

    /*
     * mkdir is nearly identical to mknod...
     */
    int ar_mkdir(const char * path, mode_t mode);

    /*
     * ar_rmdir is called for directories only and does not need to do any
     * recursive stuff
     */
    int ar_rmdir(const char * path);

    int ar_symlink(const char * from, const char * to);
    int ar_link(const char * from, const char * to);
    
    int realise_archived_file(const char * path, char ** location, struct fuse_file_info * fi, off_t entry_size, off_t max_size);
    
    int _ar_truncate(const char * path, off_t size);
    int ar_truncate(const char * path, off_t size
#if FUSE_MAJOR_VERSION >= 3
                       ,
                       struct fuse_file_info *
#endif
    );
    
    int _ar_write(const char * path, const char * buf, size_t size, off_t offset, struct fuse_file_info * fi);
    int ar_write(const char * path, const char * buf, size_t size, off_t offset, struct fuse_file_info * fi);
    
    int ar_mknod(const char * path, mode_t mode, dev_t rdev);
    
    int _ar_unlink(const char * path);
    int ar_unlink(const char * path);
    
    int _ar_chmod(const char * path, mode_t mode);
    int ar_chmod(const char * path, mode_t mode
#if FUSE_MAJOR_VERSION >= 3
                    ,
                    struct fuse_file_info *
#endif
    );
    
    int _ar_chown(const char * path, uid_t uid, gid_t gid);
    int ar_chown(const char * path, uid_t uid, gid_t gid
#if FUSE_MAJOR_VERSION >= 3
                    ,
                    struct fuse_file_info *
#endif
    );
    
    int _ar_utime(const char * path, const struct timespec tv[2]);
    int ar_utimens(const char * path, const struct timespec tv[2]
#if FUSE_MAJOR_VERSION >= 3
                      ,
                      struct fuse_file_info *
#endif
    );
    
    size_t count_nodes(NODE * node);
    size_t count_nodes(){   // Версия по умолчанию
        return count_nodes(fs.root);
    }
    int ar_statfs(const char *, struct statvfs * stbuf);
    
    int ar_rename(const char * from, const char * to
#if FUSE_MAJOR_VERSION >= 3
                     ,
                     unsigned flags
#endif
    );
    
    int ar_readlink(const char * path, char * buf, size_t size);
    int ar_open(const char * path, struct fuse_file_info * fi);
    int ar_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t, struct fuse_file_info *
#if FUSE_MAJOR_VERSION >= 3
                      ,
                      enum fuse_readdir_flags
#endif
    );
    
    int ar_create(const char * path, mode_t mode, struct fuse_file_info *);
    
    int run(int argc, char* argv[]);

    static const fuse_opt ar_opts[]; 
    static const struct fuse_operations ar_oper;

    //-----------------Статические функции-обертки--------------------------
    // Статические обертки для FUSE операций
    static int static_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi);
    static int static_readlink(const char* path, char* buf, size_t size);
    static int static_mknod(const char* path, mode_t mode, dev_t rdev);
    static int static_mkdir(const char* path, mode_t mode);
    static int static_unlink(const char* path);
    static int static_rmdir(const char* path);
    static int static_symlink(const char* from, const char* to);
    static int static_rename(const char* from, const char* to, unsigned int flags);
    static int static_link(const char* from, const char* to);
    static int static_chmod(const char* path, mode_t mode, struct fuse_file_info* fi);
    static int static_chown(const char* path, uid_t uid, gid_t gid, struct fuse_file_info* fi);
    static int static_truncate(const char* path, off_t size, struct fuse_file_info* fi);
    static int static_open(const char* path, struct fuse_file_info* fi);
    static int static_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    static int static_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    static int static_statfs(const char* path, struct statvfs* stbuf);
    static int static_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi 
#if FUSE_MAJOR_VERSION >= 3
        ,
        enum fuse_readdir_flags
#endif
    );
    static int static_create(const char* path, mode_t mode, struct fuse_file_info* fi);
    static int static_utimens(const char* path, const struct timespec tv[2], struct fuse_file_info* fi);
    static int static_opt_proc(void* data, const char* arg, int key, struct fuse_args* outargs);
};

 
