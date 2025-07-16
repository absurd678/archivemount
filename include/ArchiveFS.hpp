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


//-----------------------С Т Р У К Т У Р Ы--------------------------------------------------

// Элемент файловой системы
typedef struct node {
	// ^ must be first
	struct node * parent;
	char * name;                                 /* fully qualified with prepended '/' */
	std::string_view basename;                   /* every after the last '/'; substring of name */
	char * location;                             /* location on disk for new/modified files, else NULL */
	struct archive_entry * entry;                /* libarchive header data */
	off_t entry_size_in_archive;                 /* for st_blocks */
	std::map<std::string_view, node *> children; /* basename -> node */
	bool namechanged;                            /* true when file was renamed */
	bool modified;                               /* true when node was modified */
} NODE;

// V1 UNIX-style caching: there's only one inode open, globally, at a time, at most
// This still lets us service most reads linearly
static struct {
	NODE * node;
	struct archive * archive;
	off_t offset_in_archive_file;  // -1 = not yet found; -2 = poison
} last_open_node;

struct Options {    // флаги
    bool readonly;
    bool password;
    bool nosave;
    std::string subtree;
};


class ArchiveFS {
    public:     // Доступ только для тестирования
        
        ArchiveFS(){};      // Путь к архиву, флаги
        ~ArchiveFS(){};
        
        //void mount(const std::string& mountPoint);      
        
        /*// Методы для тестирования
        const Node* getRoot() const { return root_; }
        bool isMounted() const { return mounted_; }*/
    
    private:        // Логика деревьев и т д

        //----------------------Поля--------------------------------

        int archiveFd;      // Дескриптор архива
        Options& options;   // выбранные режимы работы
        NODE * root;
        const char * mtpt;  // Путь к точке монтирования 
        const char * archiveFile;		// Путь к архиву каталога
        char * user_passphrase;         

        //--------------------------Методы----------------------------
        
        void usage(const char * progname);
        
    

        //-----------------------Дерево-------------------------

        NODE * init_node();
        void free_node(NODE * node);
        void remove_child(NODE * node);
        void insert_as_child(NODE * node, NODE * parent);

        /*
        * inserts "node" into tree starting at "root" according to the path
        * specified in node->name
        * @return 0 on success, 0-errno else (ENOENT or ENOTDIR)
        */
        int insert_by_path(NODE * root, NODE * node);
        size_t count_nodes(NODE * node = root);
        bool archive_prepopen(struct archive * archive);
        uint64_t total_entry_size_in_archive(NODE * node = root);
        void redistribute_entry_size_in_archive(double scale, NODE * node = root);
        int build_tree(mode_t mtpt_mode);
        NODE * find_modified_node(NODE * start);
        void correct_hardlinks_to_node(const char * old_name, const char * new_name, NODE * from = root);
        NODE * firstchild(NODE * node);
        void correct_name_in_entry(NODE * node);
        NODE * get_node_for_path(NODE * start, const char * path);
        NODE * get_node_for_entry_inner(NODE * under, const char * path);
        NODE * get_node_for_entry(NODE * under, struct archive_entry * entry);
        int rename_recursively(NODE * under, const char * from, const char * to);

               // Унести в поля
        int get_temp_file(char ** location, mode_t mode, bool directory);

        /**
         * Updates given nodes node->entry by stat'ing node->location. Does not update
         * the name!
         */
        int update_entry_stat(NODE * node);
        /*
        * write a new or modified file to the new archive; used from save()
        */
                                   // Унести в поля
        void write_new_modded_file(NODE * node, struct archive_entry * wentry, struct archive * newarc);
        int save(const char * archiveFile);
        void nosave(NODE * node = root_);

    };