#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef KVM
#include "xen-dummy.h"
#else
#include "dummy.h"
#endif
#include "ext-generic.h"
//TODO change to macro implementation


uint64_t ROUNDDOWN(uint64_t a, uint32_t n) {
	return a - a % ((uint64_t) n);
}

uint64_t ROUNDUP(uint64_t a, uint32_t n) {
	return ROUNDDOWN(a + (uint64_t) (n - 1), n);
}

uint32_t CEILING(uint32_t a, uint32_t d) {
	return a / d + a % d;
}

uint32_t readui32le(char *buf, int offset) {
	return *((uint32_t *)(buf + offset));
}

uint16_t readui16le(char *buf, int offset) {
	return *((uint16_t *) (buf + offset));
}

uint8_t readui8le(char *buf, int offset) {
	return *((uint8_t *) (buf + offset));
}

uint32_t readui32be(char *buf, int offset) {
	unsigned char *_buf = (unsigned char *) buf;
	return (((uint32_t) _buf[offset]) << 24) | (((uint32_t) _buf[offset + 1]) << 16) | (((uint32_t) _buf[offset + 2]) << 8) | (((uint32_t) _buf[offset + 3]));
}

uint16_t readui16be(char *buf, int offset) {
	unsigned char *_buf = (unsigned char *) buf;
	return (((uint16_t) _buf[offset]) << 8) | (((uint16_t) _buf[offset + 1]));
}

uint8_t readui8be(char *buf, int offset) {
	return *((uint8_t *) (buf + offset));
}
//macro implementation

static int _align_read(int fd, char *buf, uint64_t offset, uint32_t size) {
	offset += ptd->partition_offset ; // offset current value by partition offset
	int ret;
	if (lseek(fd, offset, SEEK_SET) < 0) {
		DPRINTF("block-ssvd: _block_read: error lseek to %lu", offset);
		return -1;
	}
	if ((ret = read(fd, buf, size)) != size) {
		DPRINTF("block-ssvd: _block_read: error[%d]", ret);
		return -1;
	}
	return 0;
}

int align_read(int fd, char *buf, uint64_t offset, uint32_t size, uint32_t capacity, uint32_t alignment) {
#ifndef KVM
	uint64_t _offset;
	uint32_t _size;

	if (((uint64_t) buf) % alignment) {
		return -1;
	}

	_offset = ROUNDDOWN(offset, alignment);
	_size = (uint32_t) ROUNDUP(offset - _offset + size, alignment);

	if (capacity < _size) {
		return -1;
	}
	if (_align_read(fd, buf, _offset, _size) < 0)
		return -1;

	return (int)(offset - _offset);
#else
	// no alignment on KVM
	return _align_read( fd , buf , offset , size );
#endif
}

int align_readblock(int fd, char *buf, uint32_t blockid, uint32_t bsize, uint32_t capacity, uint32_t alignment) {
	uint64_t offset = (uint64_t) blockid * bsize;
	return align_read(fd, buf, offset, bsize, capacity, alignment);
}

int align_readblocks(int fd, char *buf, uint32_t blockid, uint32_t bsize, uint32_t block_count, uint32_t capacity, uint32_t alignment) {
	uint64_t offset = (uint64_t) blockid * bsize;
	return align_read(fd, buf, offset, bsize * block_count, capacity, alignment);
}
