#ifndef _BLOCK_SSVD_
#define _BLOCK_SSVD_

#include "tapdisk-driver.h"
#include "tapdisk.h"

#define MAX_SSVD_AIO_REQS TAPDISK_DATA_REQUESTS

struct tdssvd_state;

struct ssvd_request {
	td_request_t		treq;
	struct tiocb		tiocb;
	struct tdssvd_state	*state;
	int			op;
	int			size;
	uint64_t	offset;
};

struct tdssvd_state {
	int	fd;
	td_driver_t *driver;

	int	ssvd_free_count;
	struct ssvd_request	ssvd_requests[MAX_SSVD_AIO_REQS];
	struct ssvd_request *ssvd_free_list[MAX_SSVD_AIO_REQS];
};

int ssvd_stat_init(int fd, const char *log_file_name);
int ssvd_stat_log(int op, uint64_t offset, uint32_t size, char *tdbuf, int err);
int ssvd_stat_close(void);

int ssvd_server_init(void);
int ssvd_server_close(void);

#endif
