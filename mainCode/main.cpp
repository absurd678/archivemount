#define FUSE_USE_VERSION 30

#define BLOCK_SIZE 10240

#include <algorithm>
#include <archive.h>
#include <archive_entry.h>
#include <cinttypes>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <fuse_opt.h>
#include <grp.h>
#include <map>
#include <new>
#include <pthread.h>
#include <pwd.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string_view>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <wchar.h>
#include "ArchiveFS.hpp"
#include "FuseWrapper.hpp"



#include <iostream>
using namespace std::literals;

#ifdef NDEBUG
#define log(format, ...)
#else
#define log(format, ...) fprintf(stderr, "l. %4d: " format "\n", __LINE__, ##__VA_ARGS__)
#endif
#define lerr(format, ...) fprintf(stderr, "archivemount: %s: " format "\n", __func__, ##__VA_ARGS__)
#define lerrnum(err) lerr("%s", strerror(err))
#define lerrno() lerrnum(errno)

#if __APPLE__
#define st_mtim st_mtimespec
#endif

int main(int argc, char ** argv){
    ArchiveFS fs{};
    FuseWrapper mounter{fs};

	if (mounter.run(argc, argv) != 0) 
		std::cout<<"Something went wrong"<<std::endl;
}