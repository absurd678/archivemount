#pragma once
#ifndef COMMON_HPP
#define COMMON_HPP

// Версия FUSE
#define FUSE_USE_VERSION 30

// Константы
#define BLOCK_SIZE 10240

// Системные заголовки
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
#include <iostream>

// Использование литералов
using namespace std::literals;

// Макросы для логирования
#ifdef NDEBUG
    #define log(format, ...)
#else
    #define log(format, ...) fprintf(stderr, "l. %4d: " format "\n", __LINE__, ##__VA_ARGS__)
#endif

#define lerr(format, ...) fprintf(stderr, "archivemount: %s: " format "\n", __func__, ##__VA_ARGS__)
#define lerrnum(err) lerr("%s", strerror(err))
#define lerrno() lerrnum(errno)

// Специфичные для платформы определения
#if __APPLE__
    #define st_mtim st_mtimespec
#endif

// Объявление структуры NODE
struct NODE {
    NODE* parent;
    char* name;
    std::string_view basename;
    char* location;
    archive_entry* entry;
    off_t entry_size_in_archive;
    std::map<std::string_view, NODE*> children;
    bool namechanged;
    bool modified;
};

// Глобальные переменные (объявления)
struct last_open_node_struct{
    NODE* node;
    archive* archiveInstance;
    off_t offset_in_archive_file;
};

struct options {
    int readonly;
    int password;
    int nobackup;
    int nosave;
    char* subtree_filter;
    int formatraw;
};

// Перечисления и макросы
enum {
    KEY_VERSION,
    KEY_HELP,
};

#endif // COMMON_DEFS_HPP