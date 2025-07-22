#include "Common.hpp"
#include "ArchiveFS.hpp"
#include "FuseWrapper.hpp"


int main(int argc, char ** argv){
    
    FuseWrapper mounter{};
    printf("Called from main!");
	if (mounter.mount(argc, argv) != 0) 
		std::cout<<"Something went wrong"<<std::endl;
}