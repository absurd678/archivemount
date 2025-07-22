#include "Common.hpp"
#include "ArchiveFS.hpp"
#include "FuseWrapper.hpp"


int main(int argc, char ** argv){
    
    FuseWrapper mounter{};
    const char* initArchivePath; 
    const char* initMountPath; 
    char subTree[1024];
    
    
    
    if (parseInput(argc, argv, mounter, 
      initArchivePath, initMountPath, subTree)!=0) 
        std::cout<<"Error on parse"<<std::endl;

	  if (mounter.mount(initArchivePath, initMountPath, subTree) != 0) 
		  std::cout<<"Something went wrong while mount"<<std::endl;
    
    if (mounter.unmount() != 0)
      std::cout<<"Something went wrong while unmount"<<std::endl;
    
    return 0;
}