/*
 *  Compilation: gcc -Wall ex1.c -o ex1 -lclamav
 *
 *  Copyright (C) 2007 - 2009 Sourcefire, Inc.
 *  Author: Tomasz Kojm <tkojm@clamav.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <clamav.h>

/*
 * Exit codes:
 *  0: clean
 *  1: infected
 *  2: error
 */

 struct cl_engine *engine;

int scan_virus(char* filename) {
	int fd, ret;
	unsigned long int size = 0;
	long double mb;
	const char *virname;
	
	
	if((fd = open(filename, O_RDONLY)) == -1) {
		printf("Can't open file %s\n", filename);
		return 2;
    }

    /* scan file descriptor */
    if((ret = cl_scandesc(fd, &virname, &size, engine, CL_SCAN_STDOPT)) == CL_VIRUS) {
		printf("Virus detected: %s\n", virname);
    } else {
		if(ret == CL_CLEAN) {
		    printf("No virus detected.\n");
		} else {
		    printf("Error: %s\n", cl_strerror(ret));
		    close(fd);
		    return 2;
		}
    }
    close(fd);

    /* calculate size of scanned data */
    mb = size*1.0 * (CL_COUNT_PRECISION / 1024);
    printf("Data scanned: %2.2Lf KB  size:%lu\n", mb, size);
    return 0;
}


int main(int argc, char **argv)
{
	char buf[10001];
	int buf_len;
	char filename[50] = "/home/dashu/ssvd/";
	int file_len;
	int length;
	int ret;
	unsigned int sigs = 0;
//	struct timeval tv_start, tv_end, tv_accept_end, tv_receive_end;
	FILE* in_file;


    if((ret = cl_init(CL_INIT_DEFAULT)) != CL_SUCCESS) {
		printf("Can't initialize libclamav: %s\n", cl_strerror(ret));
		return 2;
    }

    if(!(engine = cl_engine_new())) {
		printf("Can't create new engine\n");
		return 2;
    }

    if((ret = cl_load(cl_retdbdir(), engine, &sigs, CL_DB_STDOPT)) != CL_SUCCESS) {
		printf("cl_load: %s\n", cl_strerror(ret));
  		cl_engine_free(engine);
		return 2;
    }

    printf("Loaded %u signatures.\n", sigs);


    if((ret = cl_engine_compile(engine)) != CL_SUCCESS) {
		printf("Database initialization error: %s\n", cl_strerror(ret));;
         cl_engine_free(engine);
		return 2;
    }

	printf("server is run\n");
	ret = mkfifo("/tmp/my_fifo", 0777);  
    if (ret == 0)  
    {  
        printf("FIFO created/n");  
    }
	//如果建立连接，将产生一个全新的套接字
	if((in_file = fopen("/home/dashu/fifo", "r")) == NULL) {
		perror("open fifo error.\n");
		return 2;
	}
	
	while(1) {
		if( (fgets(buf, 10001, in_file)) != NULL ) {
			if(!fork()) {
				buf[strlen(buf) - 1] = '\0';
				printf("received:%s\n",buf);
				length = 0;
				buf_len = strlen(buf);
				while(length < buf_len) {
					file_len = 17;
					while(length < buf_len && buf[length] != ' ') {
						filename[file_len++] = buf[length++];
					}
					length++;
					filename[file_len] = '\0';
					printf("filename:%s  ",filename);
					scan_virus(filename);
				}
				cl_engine_free(engine);
				exit(0);
			//	break;
			}
		}
	}
	fclose(in_file);
	cl_engine_free(engine);
	return 0;
}
