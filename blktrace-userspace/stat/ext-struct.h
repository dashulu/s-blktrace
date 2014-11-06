#ifndef EXT_STRUCT_H
#define EXT_STRUCT_H

#include <stdio.h>
#include <stdint.h>

#define MAX_PARTITION_NUM (100)
#define START_ADDR_SUB (0)
#define END_ADDR_SUB (1)
#define PART_TYPE_SUB (2)
// DEFINED IN EXT-STRUCT
extern uint64_t partition_table[ MAX_PARTITION_NUM ][3];
extern int partition_count;

//extern PedCHSGeometry *geom ;/* in ssvd-stat.c */
#define PARTITION_TABLE_OFFSET (0x1be)
#define ADDR_OFFSET_FROM_ENTRY (0x1)
#define BOOT_SIGNATURE 	(0x1FE)
#define SECTOR_SIZE (512)

#define SUPERBLOCKOFFSET	1024
#define SUPERBLOCKSIZE		1024

#define BOOTRECORD			0
#define SUPERBLOCK			1
#define GROUPDESCTABLE		2

#define BLOCKBITMAP 		3
#define INODEBITMAP 		4
#define INODETABLE 			5
#define DATABLOCK			6

#define UNKNOWN				-1

// mainly journal
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

// value of i_mode
#define EXT2_S_IFSOCK	0xc000
#define EXT2_S_IFLNK	0xa000
#define EXT2_S_IFREG	0x8000
#define EXT2_S_IFBLK	0x6000
#define EXT2_S_IFDIR	0x4000
#define EXT2_S_IFCHR	0x2000
#define EXT2_S_IFIFO	0x1000

// currently needless
struct ext4_inode_ext_t {

};

struct ext_inode_t {
	uint16_t i_mode;
	uint16_t i_uid;
	uint32_t i_size;
	uint32_t i_atime;
	uint32_t i_ctime;
	uint32_t i_mtime;
	uint32_t i_dtime;

	uint16_t i_gid;
	uint16_t i_links_count;

	uint32_t i_blocks;
	uint32_t i_flags;
	uint32_t i_osd1;
	uint32_t i_block[15];
	uint32_t i_generation;
	uint32_t i_file_acl;
	uint32_t i_dir_acl;
	uint32_t i_faddr;
	uint32_t i_osd2[3];
	
	int i_ext_nul;
	struct ext4_inode_ext_t i_ext;	
};

#define EXTMAXNAMELEN 255

struct ext_direntry {
	uint32_t	d_inode;
	uint16_t	d_rec_len;
	uint8_t		d_name_len;
	uint8_t		d_file_type;
	char		d_name[EXTMAXNAMELEN];
};

struct ext_blockgroupdesc_t {
	uint32_t	bg_block_bitmap;
	uint32_t	bg_inode_bitmap;
	uint32_t	bg_inode_table;
// below attributes are currently useless
	uint16_t	bg_free_blocks_count;
	uint16_t	bg_free_inodes_count;
	uint16_t	bg_used_dirs_count;
// ignore useless features
};

// customized struct for convenient
struct ext_desc_t {
	struct ext_superblock_t *sb;
	struct ext_blockgroupdesc_t *bgdesc;
	
	uint32_t ed_block_size;
	uint32_t ed_block_shift;
	uint32_t ed_groups;
	uint32_t ed_inode_table_blocks_per_group;
	uint32_t ed_inodes_per_block;
	uint32_t ed_entries_per_block;

	uint32_t ed_free_inodes;
	uint32_t ed_free_blocks;
	uint32_t ed_directories;

	uint32_t ed_journal_inum;

	int fs;
};

struct block_desc_t {
	uint32_t bd_id;
	uint32_t bd_group;
	int bd_type;

	uint32_t bd_group_offset;
	uint32_t bd_group_first_block;
};

int is_backup_group(uint32_t i);

int inode_isdir(uint16_t i_mode);
int inode_isregular(uint16_t i_mode);

uint32_t local_inode_index(uint32_t inodeid, struct ext_superblock_t *sb);

int get_block_type(uint32_t blockid, struct ext_desc_t *extdesc);

int get_superblock(struct ext_superblock_t *sb, 
	int fd, char *buf, uint32_t size, int fs);
int get_blockgroupdesc(struct ext_blockgroupdesc_t *bgdesc, struct ext_superblock_t *sb, 
	int fd, char *buf, uint32_t size, int fs);
int init_ext_desc(struct ext_desc_t *extdesc, struct ext_superblock_t *superblock, 
	struct ext_blockgroupdesc_t *blockgroupdesc, int fs);


int init_blockdesc(struct block_desc_t *blockdesc, uint64_t offset, 
	struct ext_desc_t *extdesc);
int init_blockdesc2(struct block_desc_t *blockdesc, uint32_t blockid, 
	struct ext_desc_t *extdesc);
int equal_inode(struct ext_inode_t *ia, struct ext_inode_t *ib);
int equal_inode_blocks(struct ext_inode_t *ia, struct ext_inode_t *ib);
int equal_inode_time(struct ext_inode_t *ia, struct ext_inode_t *ib);

int parse_inode(struct ext_inode_t *ext_inode, char *buf, int size, int fs);

void dump_superblock(FILE *logf, struct ext_superblock_t *sb);
void dump_blockgroupdesc(FILE *logf, struct ext_blockgroupdesc_t *blockgroupdesc);
void dump_inode(FILE *logf, struct ext_inode_t *ext_inode, uint32_t inodeid);
void dump_2inode(FILE *logf, struct ext_inode_t *iold, struct ext_inode_t *inew, uint32_t inodeid);
void dump_ext_desc(FILE *logf, struct ext_desc_t *extdesc);

struct sinode_t {
	uint16_t si_mode;
	uint32_t si_size;
	
	uint32_t si_atime;
	uint32_t si_ctime;
	uint32_t si_mtime;
	uint32_t si_dtime;

	uint32_t si_bh;
	struct sinode_t *si_parent;	//parent directory
};

struct sblock_t {
	//link indirect level
	uint8_t sb_level;

	uint32_t sb_offset;
	uint32_t sb_sib;
	uint32_t sb_bh;

	uint32_t sb_inode;
};

struct sinode_t *get_inode_from_datablock(uint32_t blockid);

int euqal_sinode_inode_time(struct sinode_t *sia, struct ext_inode_t *ib);
int equal_sinode_inode_blocks(struct sinode_t *sia, struct ext_inode_t *ib);

int get_diskstat(char *bbitmap, char *ibitmap, int fd, struct ext_desc_t *extdesc);

void dump_sinode(FILE *logf, uint32_t inodeid, struct sinode_t *ilist, struct sblock_t *blist);

#define CACHEMASK ~(0x3ff)

#define CACHETAGMASK	0x3f
#define CACHETAGSHIFT	0

#define CACHETYPEMASK	0x7
#define CACHETYPESHIFT	6

#define CACHEPRESENTSHIFT 9

//extern struct sinode_t *ilist;
//extern struct sblock_t *blist;

#define INODEID(i) (uint32_t)((i) - ptd->ilist)
#define BLOCKID(b) (uint32_t)((b) - ptd->blist)

#endif