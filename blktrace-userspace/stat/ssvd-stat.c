#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "ssvd-stat.h"
#ifndef KVM
#include "xen-dummy.h"
#else
#include "dummy.h"
#include <sys/uio.h>
#endif

#include "base-intro.h"
#include "ext-generic.h"

/*
#include <parted/parted.h>
#include <parted/debug.h>

PedDevice *pedDev = 0; //used to retrieve geometry information
PedDevice *pedDisk = 0; //used to retrieve geometry information
PedCHSGeometry *geom = 0;
*/
int enable_log = 0;
int clear_log = 0;

FILE *logfile = NULL;
FILE *logfile_debug = NULL;
char logfile_name[100];
char logfiledbg_name[100];

#define SECTOR_SIZE (512)

int fs = 0;
int init_ext_introspection(int fd, int fs);
int ext_generic_introspection(int op, uint64_t offset, uint32_t size, char *buf);
void free_ext_introspection(void);

char temp[DOM0BLOCKSIZE] __attribute__ ((aligned (DOM0BLOCKSIZE)));


//void  updatePedDeviceInfo( const char* image_file ){
//	pedDisk = ped_disk_new ( 
//		pedDev = ped_device_get( image_file )
//	 );
	/* after this the values in pedDisk is still wrong!!!
	 * go to pedDev->bios_geom for correct value!
	 * note that the value is NOT correct before calling ped_disk_new
	 * */
//	geom = &(pedDev->bios_geom);
//}
int ssvd_stat_init(int image_fd, const char *log_file_name);//exported
int ssvd_stat_init(int image_fd, const char *log_file_name) {
	//int ret;
	if (log_file_name) {
		logfile = fopen(log_file_name, "w");
		strcpy(logfile_name, log_file_name);

		strcpy(logfiledbg_name, logfile_name);
		strcat(logfiledbg_name, "_debug");
		logfile_debug = fopen(logfiledbg_name, "w");
	}

	fs = EXT3FS;
	if (init_ext_introspection(image_fd, fs)){
		fprintf(stderr,"ERROR! init ext_introspection failed!\n");
		//return -1;
	}
	lseek(image_fd, 0, SEEK_SET);
	return 0;
}

int switch_context_by_offset( uint64_t offset );
int ssvd_stat_log(int op, uint64_t offset, uint32_t size, char *buf, int err);//exported in header files
int ssvd_stat_log(int op, uint64_t offset, uint32_t size, char *buf, int err) {
	if (clear_log) {
		fclose(logfile);
		logfile = fopen(logfile_name, "w");
		clear_log = 0;
	}
	if (!err) {
	if (! switch_context_by_offset( offset ) )
		if (ext_generic_introspection(op, offset - ptd->partition_offset , size, buf)) {
			DPRINTF("disk-introspection: ext_introspection error\n");
			return -1;
		}
	}

	return 0;
}
#ifdef KVM
int ssvd_stat_log_vector( int op , const struct iovec *iov , int iovcnt , uint64_t offset , int err );//exported
int ssvd_stat_log_vector( int op , const struct iovec *iov , int iovcnt , uint64_t offset , int err ) {
	int i  , ret ;
	for ( i = 0 ; i < iovcnt ; ++ i ){
		ret |= ssvd_stat_log( op , offset , iov[i].iov_len , iov[i].iov_base , err );
		offset += iov[i].iov_len ;
	}
	return ret ;
}
#endif
int ssvd_stat_close(void) ;//exported
int ssvd_stat_close(void) {
	free_ext_introspection();
	if (fclose(logfile) == EOF) return -1;
	if (fclose(logfile_debug) == EOF) return -1;
	return 0;
}
