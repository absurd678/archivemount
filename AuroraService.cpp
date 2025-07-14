//#include <iostream>
#include "archivemount.hpp"

int main(int argc, char ** argv){
    Archivemounter a{};

	if (a.doArchivemount(argc, argv) != 0) 
		std::cout<<"Something went wrong"<<std::endl;
}