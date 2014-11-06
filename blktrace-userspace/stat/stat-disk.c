#include <stdlib.h>
#include "stat-disk.h"

#define DOM0BLOCKSIZE 4096
#define SUPERBLOCKOFFSET 1024
#define SUPERBLOCKSIZE 1024

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

static int _align_read(int fd, char *buf, uint64_t offset, uint32_t size) {
	offset = 0 ; // offset current value by partition offset
	int ret;
	if (lseek(fd, offset, SEEK_SET) < 0) {
		printf("lseek error, fd:%d offset:%lu\n", fd, offset);
		return -1;
	}
	if ((ret = read(fd, buf, size)) != size) {
		printf("read error\n");
		return -1;
	}
	return 0;
}

int align_read(int fd, char *buf, uint64_t offset, uint32_t size, uint32_t capacity, uint32_t alignment) {
#ifndef KVM
	uint64_t _offset;
	uint32_t _size;

	if (((uint64_t) buf) % alignment) {
		printf("alignment error\n");
		return -1;
	}

	_offset = ROUNDDOWN(offset, alignment);
	_size = (uint32_t) ROUNDUP(offset - _offset + size, alignment);

	if (capacity < _size) {
		printf("capacity error\n");
		return -1;
	}
	if (_align_read(fd, buf, _offset, _size) < 0) {
		printf("_align_read error\n");
		return -1;
	}

	return (int)(offset - _offset);
#else
	// no alignment on KVM
	return _align_read( fd , buf , offset , size );
#endif
}

int parse_superblock(struct ext_superblock_t *superblock, char *buf, int size, int fs) {
	if (size <= 88)
		return -1;
	superblock->s_inodes_count = readui32le(buf, 0);
	superblock->s_blocks_count = readui32le(buf, 4);
	superblock->s_r_blocks_count = readui32le(buf, 8);
	superblock->s_free_blocks_count = readui32le(buf, 12);
	superblock->s_free_inodes_count = readui32le(buf, 16);

	superblock->s_first_data_block = readui32le(buf, 20);
	superblock->s_log_block_size = readui32le(buf, 24);

	superblock->s_blocks_per_group = readui32le(buf, 32);
	superblock->s_inodes_per_group = readui32le(buf, 40);
	superblock->s_first_ino = readui32le(buf, 84);
	superblock->s_inode_size = readui16le(buf, 88);

	return 0;
}

int get_superblock(struct ext_superblock_t *sb, int fd, char *buf, uint32_t size, int fs) {
	int offset = align_read(fd, buf, SUPERBLOCKOFFSET, SUPERBLOCKSIZE, size, DOM0BLOCKSIZE);
	if (offset < 0) {
		printf("offset:%d\n", offset);
		return -1;
	}
	if (parse_superblock(sb, buf + offset, size - offset, fs) < 0) {
		printf("parse error\n");
		return -1;
	}
	return 0;
}

void dump_superblock(struct ext_superblock_t *superblock) {
	printf("s_inodes_count:%d\n", superblock->s_inodes_count);
	printf("s_blocks_count:%d\n", superblock->s_blocks_count);
	printf("s_inode_size:%d\n", superblock->s_inode_size);
}

int init_ext_introspection(int fd, int fs) {
	char *buf;
	int i;
	struct ext_superblock_t superblock;
	//struct sinode_t *sinode;
	// get initialized time
	time_t before, after;
	before = time(NULL);

	if (posix_memalign((void **) &buf, DOM0BLOCKSIZE, DOM0BLOCKSIZE) != 0) {
		return -1;
	}
	
	if (get_superblock(&superblock, fd, buf, DOM0BLOCKSIZE, fs)) {
		printf("get superblock error.\n");
	}

	free(buf);

	dump_superblock(&superblock);

	after = time(NULL);
	after -= before ;//prevent warning
	printf("time consumed:%lu\n", after);

	return 0;
}