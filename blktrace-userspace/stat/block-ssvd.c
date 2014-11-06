#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <sys/stat.h>

#include "blk.h"
#include "tapdisk-interface.h"
#include "block-ssvd.h"
#include "ssvd-clamav.h"

#include "base-intro.h"

void tdssvd_complete(void *arg, struct tiocb *tiocb, int err);

/*Get Image size, secsize*/
static int tdssvd_get_image_info(int fd, td_disk_info_t *info)
{
	int ret;
	long size;
	unsigned long total_size;
	struct statvfs statBuf;
	struct stat stat;

	ret = fstat(fd, &stat);
	if (ret != 0) {
		DPRINTF("ERROR: fstat failed, Couldn't stat image");
		return -EINVAL;
	}

	if (S_ISBLK(stat.st_mode)) {
		/*Accessing block device directly*/
		info->size = 0;
		if (blk_getimagesize(fd, &info->size) != 0)
			return -EINVAL;

		DPRINTF("Image size: \n\tpre sector_shift  [%llu]\n\tpost "
			"sector_shift [%llu]\n",
			(long long unsigned)(info->size << SECTOR_SHIFT),
			(long long unsigned)info->size);

		/*Get the sector size*/
		if (blk_getsectorsize(fd, &info->sector_size) != 0)
			info->sector_size = DEFAULT_SECTOR_SIZE;

	} else {
		/*Local file? try fstat instead*/
		info->size = (stat.st_size >> SECTOR_SHIFT);
		info->sector_size = DEFAULT_SECTOR_SIZE;
		DPRINTF("Image size: \n\tpre sector_shift  [%llu]\n\tpost "
			"sector_shift [%llu]\n",
			(long long unsigned)(info->size << SECTOR_SHIFT),
			(long long unsigned)info->size);
	}

	if (info->size == 0) {		
		info->size =((uint64_t) 16836057);
		info->sector_size = DEFAULT_SECTOR_SIZE;
	}

	DPRINTF("block-ssvd: sector size: %lu", info->sector_size);
	info->info = 0;

	return 0;
}

int tdssvd_open(td_driver_t *driver, const char *name, td_flag_t flags) {
	int i, fd, ret, o_flags;
	struct tdssvd_state *prv;

	ret = 0;
	prv = (struct tdssvd_state *)driver->data;

	DPRINTF("block-ssvd open('%s')", name);

	memset(prv, 0, sizeof(struct tdssvd_state));

	prv->ssvd_free_count = MAX_SSVD_AIO_REQS;
	for (i = 0; i < MAX_SSVD_AIO_REQS; i++)
		prv->ssvd_free_list[i] = &prv->ssvd_requests[i];

	/* Open the file */
	o_flags = O_DIRECT | O_LARGEFILE | 
		((flags & TD_OPEN_RDONLY) ? O_RDONLY : O_RDWR);
	fd = open(name, o_flags);

	if ( (fd == -1) && (errno == EINVAL) ) {
                /* Maybe O_DIRECT isn't supported. */
		o_flags &= ~O_DIRECT;
		fd = open(name, o_flags);
		if (fd != -1) DPRINTF("WARNING: Accessing image without O_DIRECT! (%s)\n", name);

	} else if (fd != -1) DPRINTF("open(%s) with O_DIRECT\n", name);
	
	if (fd == -1) {
		DPRINTF("Unable to open [%s] (%d)!\n", name, 0 - errno);
		ret = 0 - errno;
		goto done;
	}

	ret = tdssvd_get_image_info(fd, &driver->info);
	if (ret) {
		close(fd);
		goto done;
	}
	
	prv->fd = fd;

	ssvd_stat_init(fd, "/var/log/ssvd-rw.log");
	ssvd_server_init();
	init_clamav();

done:
	return ret;	
}

int tdssvd_close(td_driver_t *driver) {
	struct tdssvd_state *prv = (struct tdssvd_state *)driver->data;
	close(prv->fd);

	ssvd_stat_close();
	ssvd_server_close();

	DPRINTF("block-ssvd: closed");

	return 0;
}

void tdssvd_queue_read(td_driver_t *driver, td_request_t treq) {
	int size;
	uint64_t offset;
	struct ssvd_request *ssvd;
	struct tdssvd_state *prv;

	prv    = (struct tdssvd_state *)driver->data;
	size   = treq.secs * driver->info.sector_size;
	offset = treq.sec  * (uint64_t)driver->info.sector_size;

	if (prv->ssvd_free_count == 0)
		goto fail;

	ssvd        = prv->ssvd_free_list[--prv->ssvd_free_count];
	ssvd->treq  = treq;
	ssvd->state = prv;

	ssvd->op = 0;
	ssvd->size = size;
	ssvd->offset = offset;

	td_prep_read(&ssvd->tiocb, prv->fd, treq.buf,
		     size, offset, tdssvd_complete, ssvd);
	td_queue_tiocb(driver, &ssvd->tiocb);

	return;

fail:
	td_complete_request(treq, -EBUSY);
}

void tdssvd_queue_write(td_driver_t *driver, td_request_t treq) {
	int size;
	uint64_t offset;
	struct ssvd_request *ssvd;
	struct tdssvd_state *prv;

	prv     = (struct tdssvd_state *)driver->data;
	size    = treq.secs * driver->info.sector_size;
	offset  = treq.sec  * (uint64_t)driver->info.sector_size;

	if (prv->ssvd_free_count == 0)
		goto fail;

	ssvd        = prv->ssvd_free_list[--prv->ssvd_free_count];
	ssvd->treq  = treq;
	ssvd->state = prv;
	ssvd->op = 1;
	ssvd->size = size;
	ssvd->offset = offset;

	td_prep_write(&ssvd->tiocb, prv->fd, treq.buf,
		      size, offset, tdssvd_complete, ssvd);
	td_queue_tiocb(driver, &ssvd->tiocb);

	return;

fail:
	td_complete_request(treq, -EBUSY);
}

int tdssvd_get_parent_id(td_driver_t *driver, td_disk_id_t *id) {
	return TD_NO_PARENT;
}

int tdssvd_validate_parent(td_driver_t *driver, td_driver_t *pdriver, td_flag_t flags) {
	return -EINVAL;
}

void tdssvd_complete(void *arg, struct tiocb *tiocb, int err) {
	struct ssvd_request *ssvd = (struct ssvd_request *)arg;
	struct tdssvd_state *prv = ssvd->state;
	char buf[DOM0BLOCKSIZE];

	int size = ssvd->size;
	uint64_t offset = ssvd->offset;

	td_complete_request(ssvd->treq, err);
	prv->ssvd_free_list[prv->ssvd_free_count++] = ssvd;
	if (err) {
		ssvd_stat_log(ssvd->op, ssvd->offset, ssvd->size, NULL, err);	
	} else {
		memcpy(buf, ssvd->treq.buf, ssvd->size);
		ssvd_stat_log(ssvd->op, ssvd->offset, ssvd->size, buf, err);
	}
}

struct tap_disk tapdisk_ssvd = {
	.disk_type          = "tapdisk_ssvd",
	.flags              = 0,
	.private_data_size  = sizeof(struct tdssvd_state),
	.td_open            = tdssvd_open,
	.td_close           = tdssvd_close,
	.td_queue_read      = tdssvd_queue_read,
	.td_queue_write     = tdssvd_queue_write,
	.td_get_parent_id   = tdssvd_get_parent_id,
	.td_validate_parent = tdssvd_validate_parent,
	.td_debug           = NULL,
};
