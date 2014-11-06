/*
 * block.h
 *
 *  Created on: 20 Oct, 2013
 *      Author: phoeagon
 */

#ifndef BLOCK_H_
#define BLOCK_H_
#include <stdint.h>

struct iovec;

int ssvd_stat_init(int image_fd, const char *log_file_name);
int ssvd_server_init(void);
int init_clamav(void);
int ssvd_stat_log(int op, uint64_t offset, uint32_t size, char *buf, int err);
int ssvd_stat_log_vector( int op , const struct iovec *iov , int iovcnt , uint64_t offset , int err );
int ssvd_stat_close(void) ;
int ssvd_server_close(void);


#endif /* BLOCK_H_ */
