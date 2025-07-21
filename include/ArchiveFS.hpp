//#ifndef ARCHIVEFS_HPP
//#define ARCHIVEFS_HPP
#pragma once
#include "Common.hpp"
static struct termios noEcho() {
	struct termios orig, t;
	tcgetattr(0, &orig);
	t = orig;
	t.c_lflag &= ~ECHO;
	tcsetattr(0, TCSANOW, &t);
	return orig;
}

static ssize_t getPassphrase(char ** lineptr, size_t * n, FILE * stream) {
	ssize_t ret = getline(lineptr, n, stream);
	/* Strip newline off the end */
	if(ret > 0 && (*lineptr)[ret - 1] == '\n') {
		(*lineptr)[--ret] = '\0';
	}
	return ret;
}

class ArchiveFS {
    public:     // Доступ только для тестирования
        
        ArchiveFS();      // Путь к архиву, флаги
        ~ArchiveFS();
        
        //void mount(const std::string& mountPoint);      
        
        /*// Методы для тестирования
        const Node* getRoot() const { return root_; }
        bool isMounted() const { return mounted_; }*/
    
            // Логика деревьев и т д

        //----------------------Поля--------------------------------

        int archiveFd;      // Дескриптор архива
        options optionsInstance;   // выбранные режимы работы
        NODE * root;
        const char * mtpt;  // Путь к точке монтирования 
        const char * archiveFile;		// Путь к архиву каталога
        char * user_passphrase;      
        bool archiveWriteable; 
        uint64_t archiveFileSize; 
        
        char * tmpdir_for_nodes;
        uint64_t tmpdir_for_nodes_children;
        static thread_local char temp_io_buf[64 * 1024];

        last_open_node_struct last_open_node;
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
        int insert_by_path(NODE * node);
        
        bool archive_prepopen(struct archive * archiveInstance);
        uint64_t total_entry_size_in_archive(NODE * node);
        uint64_t total_entry_size_in_archive(){ // Версия по умолчанию
            return total_entry_size_in_archive(root);
        }
        void redistribute_entry_size_in_archive(double scale, NODE * node);
        void redistribute_entry_size_in_archive(double scale){ // Версия по умолчанию
            redistribute_entry_size_in_archive(scale, root);
        }
        int build_tree(mode_t mtpt_mode);
        NODE * find_modified_node(NODE * start);
        void correct_hardlinks_to_node(const char * old_name, const char * new_name, NODE * from);
        void correct_hardlinks_to_node(const char * old_name, const char * new_name){ // Версия по умолчанию
            correct_hardlinks_to_node(old_name, new_name, root);
        }
        NODE * firstchild(NODE * node);
        void correct_name_in_entry(NODE * node);
        NODE * get_node_for_path(NODE * start, const char * path);
        NODE * get_node_for_entry_inner(NODE * under, const char * path);
        NODE * get_node_for_entry(NODE * under, struct archive_entry * entry);
        int rename_recursively(NODE * under, const char * from, const char * to);

               // Унести в поля
        int get_temp_file(char ** location, mode_t mode, bool directory);
        int get_temp_node(char ** location, mode_t mode, dev_t dev);
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
        void nosave(NODE * node);
        void nosave(){
            nosave(root);
        }

};