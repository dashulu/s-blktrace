#include<stdlib.h>
#include<stdio.h>
#include<sys/types.h>
#include<fcntl.h>
#include<sys/stat.h>

#include "stat-disk.h"
#define BLOCK_SIZE 4096


extern int init_ext_introspection(int fd, int fs);


int main(int argc, char** argv) {
	int fd;
	int i;
	char buf[BLOCK_SIZE];

	if(argc < 2) {
		printf("para error, please input file name.\n");
		return -1;	
	}
	fd = open(argv[1], O_RDONLY);

	if(fd <=0 ) {
		printf("open %s error\n", argv[1]);
	} else {
		printf("open %s success\n", argv[1]);
	}
/*	for(i = 0; i < 10; i++) {	
		read(fd, buf, BLOCK_SIZE);
		printf("%s\n", buf);
	}
*/
	init_ext_introspection(fd, 1);
	return 0;
}
