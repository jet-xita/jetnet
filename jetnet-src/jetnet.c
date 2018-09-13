#include <stdio.h>
#include <stdlib.h>
#include "jetnet_server.h"

int main(int argc, char*argv[]){
	if(argc < 3){
		printf("usage: jetnet configfilename cellname\n");
		return 0;
	}	
	if(!jetnet_server_init(argv[1], argv[2])){
		printf("init jetnet fail!\n");
		return 0;
	}
	printf("init jetnet succeed!\n");
	jetnet_server_loop();
	jetnet_server_release();
	return 0;
}