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





class FuseWrapper{

public:

    FuseWrapper(){}
    ~FuseWrapper(){}
    
    
    
    int mount(int argc, char* argv[]);

    static const fuse_opt ar_opts[]; 
    static const struct fuse_operations ar_oper;

    
};

 
