#include <stdio.h>
#include <stdint.h>

struct ext3_superblock_ext_t {
	uint32_t s_journal_inum;
	uint32_t s_journal_dev;
};

struct ext_superblock_t {
	uint32_t s_inodes_count;
	uint32_t s_blocks_count;
	uint32_t s_r_blocks_count;
	uint32_t s_free_blocks_count;
	uint32_t s_free_inodes_count;
	uint32_t s_first_data_block;
	uint32_t s_log_block_size;
	/*
	ignored needless feature
	uint32_t s_log_frag_size;
	*/
	uint32_t s_blocks_per_group;
	/*
	uint32_t s_frags_per_group;
	*/
	uint32_t s_inodes_per_group;
	/*
	ignored needless features
	*/
	uint32_t s_first_ino;
	uint16_t s_inode_size;
	/*
	ignored needless features
	*/
	// for compatiblity with ext3

	int s_ext_nul;
	struct ext3_superblock_ext_t s_ext;
};