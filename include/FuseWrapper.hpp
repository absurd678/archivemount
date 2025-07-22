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

    struct fuse_args args;
    FuseWrapper(){}
    ~FuseWrapper(){};
    
    
    
    int mount(const char* initArchivePath, const char* initMountPath, char* subTree);
    int unmount();
};

int parseInput(int argc, char** argv, FuseWrapper& fw,
    const char*& initArchivePath, const char*& initMountPath, char* subTree);

