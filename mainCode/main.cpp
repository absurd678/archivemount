#include "Common.hpp"
#include "ArchiveFS.hpp"
#include "FuseWrapper.hpp"


int main(int argc, char ** argv){
    
    FuseWrapper mounter{};
    const char* initArchivePath; // Путь к архиву
    const char* initMountPath; // Путь к точке монтирования
    char subTree[1024];   // Поддерево
    
    if (parseInput(argc, argv, mounter, 
      initArchivePath, initMountPath, subTree)!=0) // Парсинг
        std::cout<<"Error on parse"<<std::endl;

	  if (mounter.mount(initArchivePath, initMountPath, subTree) != 0) // МОнтирование
		  std::cout<<"Something went wrong while mount"<<std::endl;
    
    if (mounter.unmount() != 0)   // Размонтирование
      std::cout<<"Something went wrong while unmount"<<std::endl;
    
    return 0;
}