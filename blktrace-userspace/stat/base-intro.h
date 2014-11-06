#ifndef BASE_INTRO_H
#define BASE_INTRO_H

#include <stdint.h>

#define DOM0BLOCKSIZE 4096

#define EXT2FS 		0
#define EXT3FS		1

uint64_t ROUNDDOWN(uint64_t a, uint32_t n);
uint64_t ROUNDUP(uint64_t a, uint32_t n);
uint32_t CEILING(uint32_t a, uint32_t d);

uint32_t readui32le(char *buf, int offset);
uint16_t readui16le(char *buf, int offset);
uint8_t readui8le(char *buf, int offset);
uint32_t readui32be(char *buf, int offset);
uint16_t readui16be(char *buf, int offset);
uint8_t readui8be(char *buf, int offset);

int align_read(int fd, char *buf, uint64_t offset, uint32_t size, 
	uint32_t capacity, uint32_t alignment);
int align_readblock(int fd, char *buf, uint32_t blockid, uint32_t bsize, 
	uint32_t capacity, uint32_t alignment);
int align_readblocks(int fd, char *buf, uint32_t blockid, uint32_t bsize, uint32_t block_count, 
	uint32_t capacity, uint32_t alignment);

#endif