#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <clamav.h>


#include "ssvd-clamav.h"

#ifndef KVM
#include "xen-dummy.h"
#define AV_LOG_FILE "/var/log/ssvd-av.log"
#else
#include "dummy.h"
#define AV_LOG_FILE "/run/shm/ssvd-av.log"
#endif

#include "queue.h"

extern FILE *logfile;
extern struct queue_root *queue;

struct cl_engine *engine;

pthread_t thrd_acquire_tasks;

uint32_t clamav_uuid = 0;
pthread_mutex_t mutex_uuid;

int flag_clamav_exit = 0;

FILE *scanlog = NULL;

int free_clamav(void) ;//exported

static int scan_virus(char *filename) {
	int fd, ret;
	unsigned long int size = 0;
	long double mb;
	const char *virname;
	
	if((fd = open(filename, O_RDONLY)) == -1) {
		DPRINTF("ssvd-clamav: Can't open file %s\n", filename);
		perror("ssvd-clamav: open");
		return 2;
    }

    fprintf(scanlog, "filename: %s\n", filename);
    /* scan file descriptor */
    if((ret = cl_scandesc(fd, &virname, &size, engine, CL_SCAN_STDOPT)) == CL_VIRUS) {
		fprintf(scanlog, "Virus detected: %s\n", virname);
    } else {
		if(ret == CL_CLEAN) {
		    fprintf(scanlog, "No virus detected.\n");
		} else {
		    fprintf(scanlog, "Error: %s\n", cl_strerror(ret));
		    fflush(scanlog);
		    close(fd);
		    return 2;
		}
    }
    close(fd);
    /*if(ret == CL_CLEAN) {*/
    	if (unlink(filename)) {
			fprintf(scanlog, "error unlink %s\n", filename);
			return -1;
		}
    /*}*/
    /* calculate size of scanned data */
    mb = size * 1.0 * (CL_COUNT_PRECISION / 1024);
    fprintf(scanlog, "Data scanned: %2.2Lf KB  size:%lu\n", mb, size);
    fflush(scanlog);
    return 0;
}

static void *acquire_tasks(void *arg) {
	struct queue_head *item;	
	char filename[30];
	
	while (!flag_clamav_exit) {
		while ((item = queue_get(queue))) {
			sprintf(filename, "%s%hu.%u", CLFSROOTDIR, item->version, item->value);
			if (item->type == ONMEMINODE) 
				scan_virus(filename);
		}
		sleep(5);
	}
	return NULL;
}

int init_clamav(void) {
	int ret;
	uint32_t sigs = 0;

	if (!(scanlog = fopen(AV_LOG_FILE, "w"))) {
		DPRINTF("ssvd-clamav: Can't open virus log");
		return -1;
	}

	if((ret = cl_init(CL_INIT_DEFAULT)) != CL_SUCCESS) {
		DPRINTF("ssvd-clamav: Can't initialize libclamav: %s\n", cl_strerror(ret));
		return -1;
    }

    if(!(engine = cl_engine_new())) {
		DPRINTF("ssvd-clamav: Can't create new engine\n");
		return -1;
    }

    if((ret = cl_load(cl_retdbdir(), engine, &sigs, CL_DB_STDOPT)) != CL_SUCCESS) {
		DPRINTF("ssvd-clamav: cl_load: %s\n", cl_strerror(ret));
  		cl_engine_free(engine);
		return -1;
    }

    DPRINTF("ssvd-clamav: Loaded %u signatures\n", sigs);

    if ((ret = cl_engine_compile(engine)) != CL_SUCCESS) {
    	DPRINTF("ssvd-clamav: Database initialization error: %s\n", cl_strerror(ret));
    	cl_engine_free(engine);
    	return -1;
    }

    DPRINTF("ssvd-clamav: engine comiled\n");

	if ((ret = pthread_mutex_init(&mutex_uuid, NULL))) {
		DPRINTF("ssvd-clamav: failed to init mutex");
		return -1;
	}

	if ((ret = pthread_create(&thrd_acquire_tasks, NULL, acquire_tasks, NULL)) != 0) {
		DPRINTF("ssvd-clamav: error create new thread: %d", ret);
		return -1;
	}

	return 0;
}

int free_clamav(void) {
	int ret = 0;
	flag_clamav_exit = 1;

	if ((ret = pthread_join(thrd_acquire_tasks, NULL)) != 0) {
		DPRINTF("block-ssvd: ssvd-server: pthread_join error");
	}

	fclose(scanlog);
	cl_engine_free(engine);
	return ret;
}
