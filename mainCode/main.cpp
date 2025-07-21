#include "Common.hpp"
#include "ArchiveFS.hpp"
#include "FuseWrapper.hpp"


int main(int argc, char ** argv){
    ArchiveFS fs{};
    FuseWrapper mounter{fs};
    printf("Called from main!");
	if (mounter.run(argc, argv) != 0) 
		std::cout<<"Something went wrong"<<std::endl;
}