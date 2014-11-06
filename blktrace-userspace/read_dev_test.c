#include<stdlib.h>
#include<stdio.h>
#include<sys/types.h>
#include<fcntl.h>
#include<sys/stat.h>

#define BLOCK_SIZE 4096

int main(int argc, char** argv) {
	int fd;
	int i;
	char buf[BLOCK_SIZE];

	if(argc < 2) {
		printf("para error, please input file name.\n");
		return -1;	
	}
	fd = open(argv[1], O_RDONLY);
	for(i = 0; i < 10; i++) {	
		read(fd, buf, BLOCK_SIZE);
		printf("%s\n", buf);
	}
}
