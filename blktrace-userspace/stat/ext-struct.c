#include <string.h>
#include <stdlib.h>

#include "ext-struct.h"
#include "base-intro.h"

#ifndef KVM
#include "xen-dummy.h"
#else
#include "dummy.h"
#endif

#define SECTOR_SIZE (512)

extern FILE *logfile;
uint64_t partition_table[ MAX_PARTITION_NUM ][3];
int partition_count = 0;

#include "ext-generic.h"

int parse_extended_boot_record( int fd , char *buf , uint32_t offset_sector );//exported
int is_backup_group(uint32_t i) {
	if (i == 0 || i == 1)
		return 1;
	if (i % 3 ==0) {
		while (i % 3 == 0) i /= 3;
		return i == 1;
	}
	if (i % 5 == 0) {
		while (i % 5 == 0) i /= 5;
		return i == 1;
	}
	if (i % 7 == 0) {
		while (i % 7 == 0) i /= 7;
		return i == 1;
	}
	return 0;
}

int inode_isdir(uint16_t i_mode) {
	return ((i_mode & 0xf000) == EXT2_S_IFDIR);
	//return ((i_mode & EXT2_S_IFDIR) && !(i_mode & EXT2_S_IFCHR) && !(i_mode & EXT2_S_IFSOCK));
}

int inode_isregular(uint16_t i_mode) {
	return ((i_mode & 0xf000) == EXT2_S_IFREG);
	//return ((i_mode & EXT2_S_IFREG) && !(i_mode & EXT2_S_IFBLK) && !(i_mode & EXT2_S_IFSOCK));
}

uint32_t local_inode_index(uint32_t inodeid, struct ext_superblock_t *sb) {
	return (inodeid - 1) % sb->s_inodes_per_group;
}

int get_block_type(uint32_t blockid, struct ext_desc_t *extdesc) {
	uint32_t groupid, groupoffset;

	if (blockid < extdesc->sb->s_first_data_block) {
		return BOOTRECORD;
	}

	groupid = (blockid - extdesc->sb->s_first_data_block) / extdesc->sb->s_blocks_per_group;
	groupoffset = (blockid - extdesc->sb->s_first_data_block) % extdesc->sb->s_blocks_per_group;

	if (is_backup_group(groupid)) {
		if (groupoffset == 0)
			return SUPERBLOCK;
		if (groupoffset < extdesc->bgdesc->bg_block_bitmap) {
			return GROUPDESCTABLE;
		}
		groupoffset -= extdesc->bgdesc->bg_block_bitmap;
	}

	if (groupoffset == 0)
		return BLOCKBITMAP;
	if (groupoffset == 1)
		return INODEBITMAP;
	if (groupoffset < extdesc->ed_inode_table_blocks_per_group + 2)
		return INODETABLE;
	if (groupoffset < extdesc->sb->s_blocks_per_group)
		return DATABLOCK;

	return -1;
}

static void _get_block_type(struct block_desc_t *blockdesc, struct ext_desc_t *extdesc) {
	uint32_t offset;
	if (blockdesc->bd_id < extdesc->sb->s_first_data_block) {
		blockdesc->bd_type = BOOTRECORD;
		return;
	}

	if (blockdesc->bd_group_first_block) {
		if (blockdesc->bd_group_offset == 0) {
			blockdesc->bd_type = SUPERBLOCK;
			return;
		}
		if (blockdesc->bd_group_offset < blockdesc->bd_group_first_block) {
			blockdesc->bd_type = GROUPDESCTABLE;
			return;
		}
	}

	offset = blockdesc->bd_group_offset - blockdesc->bd_group_first_block;
	
	if (offset == 0) {
		blockdesc->bd_type = BLOCKBITMAP;
		return;
	}

	if (offset == 1) {
		blockdesc->bd_type = INODEBITMAP;
		return;
	}

	if (offset < extdesc->ed_inode_table_blocks_per_group + 2) {
		blockdesc->bd_type = INODETABLE;
		return;
	}

	if (offset < extdesc->sb->s_blocks_per_group) {
		blockdesc->bd_type = DATABLOCK;
		return;
	}

	blockdesc->bd_type = -1;
}

int init_blockdesc(struct block_desc_t *blockdesc, uint64_t offset, struct ext_desc_t *extdesc) {
	blockdesc->bd_id = offset >> extdesc->ed_block_shift;
	blockdesc->bd_group = (blockdesc->bd_id - extdesc->sb->s_first_data_block) / extdesc->sb->s_blocks_per_group;
	if (blockdesc->bd_group >= extdesc->ed_groups) {
		DPRINTF("disk-introspection: invalid block[%u] with invalid group[%u]\n", blockdesc->bd_id, blockdesc->bd_group);
		return -1;
	}
	blockdesc->bd_group_offset = (blockdesc->bd_id - extdesc->sb->s_first_data_block) % extdesc->sb->s_blocks_per_group;
	
	if (is_backup_group(blockdesc->bd_group)) {
		blockdesc->bd_group_first_block = extdesc->bgdesc->bg_block_bitmap;
	} else {
		blockdesc->bd_group_first_block = 0;
	}

	_get_block_type(blockdesc, extdesc);

	if (blockdesc->bd_type < 0)
		return -1;
	return 0;
}

int init_blockdesc2(struct block_desc_t *blockdesc, uint32_t blockid, struct ext_desc_t *extdesc) {
	blockdesc->bd_id = blockid;
	blockdesc->bd_group = (blockdesc->bd_id - extdesc->sb->s_first_data_block) / extdesc->sb->s_blocks_per_group;
	if (blockdesc->bd_group >= extdesc->ed_groups) {
		DPRINTF("disk-introspection: invalid block[%u] with invalid group[%u]\n", blockdesc->bd_id, blockdesc->bd_group);
		return -1;
	}
	blockdesc->bd_group_offset = (blockdesc->bd_id - extdesc->sb->s_first_data_block) % extdesc->sb->s_blocks_per_group;
	
	if (is_backup_group(blockdesc->bd_group)) {
		blockdesc->bd_group_first_block = extdesc->bgdesc->bg_block_bitmap;
	} else {
		blockdesc->bd_group_first_block = 0;
	}

	_get_block_type(blockdesc, extdesc);

	if (blockdesc->bd_type < 0)
		return -1;
	return 0;
}

static int parse_superblock_ext3_ext(struct ext3_superblock_ext_t *superblock_ext, char *buf, int size, int fs) {
	if (size <= 228)
		return -1;
	superblock_ext->s_journal_inum = readui32le(buf, 224);
	superblock_ext->s_journal_dev = readui32le(buf, 228);
	return 0;
}

static int parse_superblock(struct ext_superblock_t *superblock, char *buf, int size, int fs) {
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

	if (fs == EXT3FS) {
		//TODO check the result of malloc
		superblock->s_ext_nul = 0;
		if (parse_superblock_ext3_ext(&superblock->s_ext, buf, size, fs)) {
			return -1;
		}
	} else if (fs == EXT2FS) {
		superblock->s_ext_nul = 1;
	}
	return 0;
}

void dump_superblock(FILE *logf, struct ext_superblock_t *sb) {
	fprintf(logf, 
		"Ext%d-fs Superblock\ns_inodes_count: %u\ns_blocks_count: %u\ns_r_blocks_count: %u\ns_free_blocks_count: %u\n\
s_free_inodes_count: %u\ns_first_datablock: %u\ns_log_block_size: %u\ns_blocks_per_group: %u\ns_inodes_per_group: %u\n\
s_first_ino: %u\ns_inode_size: %u\n",
		(sb->s_ext_nul ? 2 : 3), sb->s_inodes_count, sb->s_blocks_count, sb->s_r_blocks_count, sb->s_free_blocks_count,
		sb->s_free_inodes_count, sb->s_first_data_block, sb->s_log_block_size, sb->s_blocks_per_group, sb->s_inodes_per_group,
		sb->s_first_ino, (uint32_t) sb->s_inode_size
		);

	if (!sb->s_ext_nul) {
		fprintf(logf, "s_journal_inum: %u\ns_journal_dev: %u\n",
			sb->s_ext.s_journal_inum, sb->s_ext.s_journal_dev);
	}
	fprintf(logf, "\n");
	fflush(logf);
}

static int check_mbr( char *buf ){
	// so called boot signature
	return ( buf[BOOT_SIGNATURE]==(char)0x55 ) && 
			( buf[BOOT_SIGNATURE+1] == (char)0xAA );
}
#if 0
static int check_ext_magic ( char *buf ){
	// CANNOT do this because magic number of ext file is at offset=1080
	// beyond the first 1024 byte
	return 0;
}
#endif
/*uint64_t chs2linear( int head , int sector , int cylinder ){
	uint64_t ret = cylinder ;
	ret *= geom->heads + 1 ;
	ret += head;
	ret *= geom->sectors + 1;
	ret += sector;
	return ret - 1 ;
}*/
static int parse_partition_table( int fd , char *buf ){
	int i = 0;
	int ext_partition_sec = -1;
	for ( i = 0 ; i < 4 ; ++ i ){
		int partition_type = (unsigned)buf[ PARTITION_TABLE_OFFSET + 4 + 16*(i) ] ;
		if (  partition_type == 0x5 ){ //extended
			ext_partition_sec = readui32le( buf , PARTITION_TABLE_OFFSET + 8 + 16*i );
		}
		else if ( partition_type ){ 
			//not free space
			partition_table[ partition_count ][START_ADDR_SUB] =
				readui32le( buf , PARTITION_TABLE_OFFSET + 8 + 16*i );
			partition_table[ partition_count ][END_ADDR_SUB] =
				partition_table[ partition_count ][0] +
				readui32le( buf , PARTITION_TABLE_OFFSET + 0xc + 16*i );
			partition_table[ partition_count ][ PART_TYPE_SUB ] = partition_type ;
			++partition_count ;
		}
	}
	if ( ext_partition_sec > 0 ){
		parse_extended_boot_record( fd , buf , ext_partition_sec );
	}
	return 0;
}
int parse_extended_boot_record( int fd , char *buf , uint32_t offset_sector ){
	while( 1 ){
		uint64_t offset = offset_sector * (uint64_t)SECTOR_SIZE ;
		int ret = align_read(fd, buf, offset, SECTOR_SIZE, SECTOR_SIZE, SECTOR_SIZE);
		if ( ret || buf[510]!=(char)0x55 || buf[511]!=(char)0xaa )
			return -1;
		int partition_type = 0;
		if ( !!( partition_type = (unsigned)buf[ PARTITION_TABLE_OFFSET + 4 ] ) ){
			int start_sec = readui32le( buf , PARTITION_TABLE_OFFSET+0x8 );
			int sec_count = readui32le( buf , PARTITION_TABLE_OFFSET+0xc );
			partition_table[ partition_count ][START_ADDR_SUB] = start_sec + offset_sector;
			partition_table[ partition_count ][END_ADDR_SUB] = start_sec+sec_count + offset_sector;
			partition_table[ partition_count ][PART_TYPE_SUB] = partition_type;
			partition_count ++;
			if ( partition_count == MAX_PARTITION_NUM ){
				fprintf(stderr,"Too many partitions\n");
				return 0;
			}
		}
		if ( !!(partition_type = (unsigned)buf[ PARTITION_TABLE_OFFSET + 4 + 16 ] ) ){
			//if next EBR exists
			if ( readui32le( buf , PARTITION_TABLE_OFFSET+0x8+16 ) == 0 ) break;
			offset_sector += readui32le( buf , PARTITION_TABLE_OFFSET+0x8+16 );
		}else break;
	}
	return 0;
}
static uint64_t get_first_partition_offset( char *buf ){
	return readui32le( buf , PARTITION_TABLE_OFFSET + 8 )*SECTOR_SIZE;
	//assume logic block size 512 (SECTOR_SIZE)
}

int get_mbr(int fd, char *buf, uint32_t size);//exported
int get_mbr(int fd, char *buf, uint32_t size) {
	int offset = align_read(fd, buf, 0, SECTOR_SIZE, size, DOM0BLOCKSIZE);
	if (offset < 0)
		return -1;
	if ( check_mbr( buf ) ){
		parse_partition_table( fd , buf );
		//parse only the first partition
		ptd->partition_offset = get_first_partition_offset( buf ) * SECTOR_SIZE ;
	}
	else
		ptd->partition_offset = 0 ;// current file is a whole partition with no partition table
		
	return 0;
}
int get_superblock(struct ext_superblock_t *sb, int fd, char *buf, uint32_t size, int fs) {
	int offset = align_read(fd, buf, SUPERBLOCKOFFSET, SUPERBLOCKSIZE, size, DOM0BLOCKSIZE);
	if (offset < 0)
		return -1;
	if (parse_superblock(sb, buf + offset, size - offset, fs) < 0)
		return -1;
	return 0;
}

static int parse_blockgroupdesc(struct ext_blockgroupdesc_t *blockgroupdesc, char *buf, int size, int fs) {
	if (size <= 8)
		return -1;
	blockgroupdesc->bg_block_bitmap = readui32le(buf, 0);
	blockgroupdesc->bg_inode_bitmap = readui32le(buf, 4);
	blockgroupdesc->bg_inode_table = readui32le(buf, 8);
	return 0;
}

void dump_blockgroupdesc(FILE *logf, struct ext_blockgroupdesc_t *blockgroupdesc) {
	fprintf(logf, 
		"Block Group Descriptor\nbg_block_bitmap: %u\nbg_inode_bitmap: %u\nbg_inode_table: %u\n\n", 
		blockgroupdesc->bg_block_bitmap, blockgroupdesc->bg_inode_bitmap, blockgroupdesc->bg_inode_table);
	fflush(logf);
}

int get_blockgroupdesc(struct ext_blockgroupdesc_t *bgdesc, struct ext_superblock_t *sb, int fd, char *buf, uint32_t size, int fs) {
	int offset = align_readblock(fd, buf, sb->s_first_data_block + 1, 1024 << sb->s_log_block_size, size, DOM0BLOCKSIZE);
	if (offset)
		return -1;
	if (parse_blockgroupdesc(bgdesc, buf, size, fs))
		return -1;
	return 0;
}

int init_ext_desc(struct ext_desc_t *extdesc, struct ext_superblock_t *superblock, struct ext_blockgroupdesc_t *blockgroupdesc, int fs) {
	extdesc->sb = superblock;
	extdesc->bgdesc = blockgroupdesc;

	extdesc->ed_block_size = 1024 << superblock->s_log_block_size;
	extdesc->ed_block_shift = 10 + superblock->s_log_block_size;
	extdesc->ed_groups = superblock->s_blocks_count / superblock->s_blocks_per_group +  !!(superblock->s_blocks_count % superblock->s_blocks_per_group);
	extdesc->ed_inode_table_blocks_per_group = CEILING(superblock->s_inode_size * superblock->s_inodes_per_group, extdesc->ed_block_size);
	extdesc->ed_inodes_per_block = extdesc->ed_block_size / superblock->s_inode_size;
	extdesc->ed_entries_per_block = extdesc->ed_block_size >> 2;

	extdesc->ed_free_inodes = 0;
	extdesc->ed_free_blocks = 0;
	extdesc->ed_directories = 0;

	extdesc->fs = fs;

	if (extdesc->fs == EXT3FS) {
		extdesc->ed_journal_inum = superblock->s_ext.s_journal_inum;
	}
	return 0;
}

void dump_ext_desc(FILE *logf, struct ext_desc_t *extdesc) {

	fprintf(logf, "Ext Desc\ned_block_size: %u\ned_block_shift: %u\ned_inodes_per_block: %u\ned_groups: %u\ned_inode_table_blocks: %u\n\
ed_free_inodes: %u\ned_free_blocks: %u\ned_directories: %u\nfilesystem: %d\n\n",
		extdesc->ed_block_size, extdesc->ed_block_shift, extdesc->ed_inodes_per_block, extdesc->ed_groups, extdesc->ed_inode_table_blocks_per_group,
		extdesc->ed_free_inodes, extdesc->ed_free_blocks, extdesc->ed_directories, extdesc->fs	
		);
	fflush(logf);
}

//TODO check the overflow of blockid
struct sinode_t *get_inode_from_datablock(uint32_t blockid) {
	if (ptd->blist[blockid].sb_inode)
		return &ptd->ilist[ptd->blist[blockid].sb_inode];
	return NULL;
}

int equal_inode(struct ext_inode_t *ia, struct ext_inode_t *ib) {
	int i ;
	if (!(ia->i_mode | ib->i_mode))
		return 1;
	if ((ia->i_mode != ib->i_mode) || (ia->i_size != ib->i_size) 
		|| (ia->i_ctime != ib->i_ctime) || (ia->i_mtime != ib->i_mtime) ||  (ia->i_dtime != ib->i_dtime)
		|| (ia->i_blocks != ib->i_blocks))
		return 0;
	for ( i = 0; i < 15; i++) {
		if (ia->i_block[i] != ib->i_block[i])
			return 0;
	}
	return 1;
}

int equal_inode_time(struct ext_inode_t *ia, struct ext_inode_t *ib) {
	if ((ia->i_ctime != ib->i_ctime) || (ia->i_mtime != ib->i_mtime) ||  (ia->i_dtime != ib->i_dtime))
		return 0;
	return 1;
}

int equal_inode_blocks(struct ext_inode_t *ia, struct ext_inode_t *ib) {
	int i ;
	for (i = 0; i < 15; i++) {
		if (ia->i_block[i] != ib->i_block[i])
			return 0;
	}
	return 1;
}
int equal_sinode_inode_time(struct sinode_t *sia, struct ext_inode_t *ib) ;//exported
int equal_sinode_inode_time(struct sinode_t *sia, struct ext_inode_t *ib) {
	return (sia->si_ctime == ib->i_ctime && sia->si_mtime == ib->i_mtime);
}

//TODO 
int equal_sinode_inode_blocks(struct sinode_t *sia, struct ext_inode_t *ib) {
	return 0;
}


void dump_2inode(FILE *logf, struct ext_inode_t *iold, struct ext_inode_t *inew, uint32_t inodeid) {
	int i;
	fprintf(logf, "\ninode[%u]\n", inodeid);
	fprintf(logf, "i_mode:\t\t%u\t\t%u\n", (uint32_t) iold->i_mode, (uint32_t) inew->i_mode);
	fprintf(logf, "i_size:\t\t%u\t\t%u\n", iold->i_size, inew->i_size);
	
	fprintf(logf, "i_atime:\t\t%x\t\t%x\n", iold->i_atime, inew->i_atime);
	fprintf(logf, "i_ctime:\t\t%x\t\t%x\n", iold->i_ctime, inew->i_ctime);
	fprintf(logf, "i_mtime:\t\t%x\t\t%x\n", iold->i_mtime, inew->i_mtime);
	fprintf(logf, "i_dtime:\t\t%x\t\t%x\n", iold->i_dtime, inew->i_dtime);
	
	fprintf(logf, "i_links_count:\t\t%x\t\t%x\n", (uint32_t) iold->i_links_count, (uint32_t) inew->i_links_count);

	fprintf(logf, "i_blocks:\t\t%u\t\t%u\n", iold->i_blocks, inew->i_blocks);
	for (i = 0; i < 15; i++) {
		if (iold->i_block[i] || inew->i_block[i] || iold->i_block[i] != inew->i_block[i])
		fprintf(logf, "i_block[%d]:\t\t%u\t\t%u\n", i, iold->i_block[i], inew->i_block[i]);
	}
	fprintf(logf, "\n");
	fflush(logf);
}

void dump_inode(FILE *logf, struct ext_inode_t *ext_inode, uint32_t inodeid) {
	int i;
	fprintf(logf, "\ninode[%u]\ni_mode: %u\ni_size: %u\ni_atime: %u\ni_ctime: %u\ni_mtime: %u\ni_dtime: %u\ni_links_count: %u\ni_blocks: %u\n", 
		inodeid, ext_inode->i_mode, ext_inode->i_size, ext_inode->i_atime, ext_inode->i_ctime, ext_inode->i_mtime, ext_inode->i_dtime, ext_inode->i_links_count, ext_inode->i_blocks);
	for (i = 0; i < 15; i++)
		if (ext_inode->i_block[i])
			fprintf(logf, "i_block[%d]: %u\n", i, ext_inode->i_block[i]);

	fprintf(logf, "\n");
	fflush(logf);
}

int parse_inode(struct ext_inode_t *ext_inode, char *buf, int size, int fs) {
	int i;
	if (size <= 100)
		return -1;
	ext_inode->i_mode = readui16le(buf, 0);
	ext_inode->i_size = readui32le(buf, 4);
	
	ext_inode->i_atime = readui32le(buf, 8);
	ext_inode->i_ctime = readui32le(buf, 12);
	ext_inode->i_mtime = readui32le(buf, 16);
	ext_inode->i_dtime = readui32le(buf, 20);
	ext_inode->i_links_count = readui16le(buf, 26);

	// skip inode other than file or folder
	if (!(inode_isdir(ext_inode->i_mode)) && !(inode_isregular(ext_inode->i_mode)))
		return 0;

	ext_inode->i_blocks = readui32le(buf, 28);
	for (i = 0; i < 15; i++) {
		ext_inode->i_block[i] = readui32le(buf, 40 + i * 4);
	}
	//TODO extend for ext4
	return 0;
}
#if 0
static int parse_direntry(struct ext_direntry *dentry, char *buf, int size) {
	dentry->d_inode = readui32le(buf, 0);
	if (dentry->d_inode) {
		if (size <= 7)
			return -1;
		dentry->d_rec_len = readui16le(buf, 4);
		dentry->d_name_len = readui8le(buf, 6);
		dentry->d_file_type = readui8le(buf, 7);
		if (size < 8 + dentry->d_name_len)
			return -1;
		memcpy(dentry->d_name, buf + 8, dentry->d_name_len);
		dentry->d_name[dentry->d_name_len] = '\0';
	}
	return 0;
}
#endif
static void recursive_dump_indirect_block(FILE *logf, struct sblock_t *blist, uint32_t blockid) {
	struct sblock_t *ptr;
	uint32_t t = 0, lt = 0;
	while (blockid) {
		ptr = &blist[blockid];
		if (ptr->sb_level) {
			if (t && lt != t) {
				fprintf(logf, "-%u, ", t - 1);
			} else {
				fprintf(logf, ", ");
			}
			t = 0;
			if (ptr->sb_level == 1)
				fprintf(logf, "(%u)[IND]%u, ", blist[blockid].sb_offset, blockid);
			else if (ptr->sb_level == 2)
				fprintf(logf, "(%u)[DIND]%u, ", blist[blockid].sb_offset, blockid);
			else
				fprintf(logf, "(%u)[TIND]%u, ", blist[blockid].sb_offset, blockid);
			if (ptr->sb_bh)
				recursive_dump_indirect_block(logf, blist, ptr->sb_bh);
		} else {
			if (t == 0) {
				lt = t = blockid + 1;
				fprintf(logf, "%u", blockid);
			} else {
				if (t == blockid) {
					t = blockid + 1;
				} else {
					if (lt != t) {
						fprintf(logf, "-%u, ", t - 1);
					} else {
						fprintf(logf, ", ");
					}
					lt = t = blockid + 1;
					fprintf(logf, "%u", blockid);
				}
			}
		}
		blockid = ptr->sb_sib;
		if (blockid && (ptr->sb_inode != blist[blockid].sb_inode)) {
			fprintf(logf, " ,B[%u], ", blockid);
		}
	}
	if (t && lt != t) {
		fprintf(logf, "-%u, ", t - 1);
	}
	/*while (blockid) {
		ptr = &blist[blockid];
		if (ptr->sb_level) {
			if (ptr->sb_level == 1)
				fprintf(logf, "(%u)[IND]%u, ", blist[blockid].sb_offset, blockid);
			else if (ptr->sb_level == 2)
				fprintf(logf, "(%u)[DIND]%u, ", blist[blockid].sb_offset, blockid);
			else
				fprintf(logf, "(%u)[TIND]%u, ", blist[blockid].sb_offset, blockid);
			if (ptr->sb_bh)
				recursive_dump_indirect_block(logf, blist, ptr->sb_bh);
		} else {
			fprintf(logf, "(%u)%u, ", blist[blockid].sb_offset, blockid);
		}
		blockid = ptr->sb_sib;
	}*/
}

void dump_sinode(FILE *logf, uint32_t inodeid, struct sinode_t *ilist, struct sblock_t *blist) {
	fprintf(logf, "[%u](%u,[%x,%x,%x],\t", inodeid, 
		ilist[inodeid].si_mode, ilist[inodeid].si_ctime, ilist[inodeid].si_mtime, ilist[inodeid].si_dtime);
	recursive_dump_indirect_block(logf, blist, ilist[inodeid].si_bh);
	fprintf(logf, ")\n");
	fflush(logf);
}

// caller guarantee blockid != 0
static struct sblock_t *
parse_indirect_block(struct sinode_t *inode, struct sblock_t *parent_block, struct sblock_t *current_block, uint32_t blockid,
	int indirect_level, uint32_t offset, struct ext_desc_t *extdesc, int fd) {
	
	int i;
	int ret;

	uint32_t link;
	char *buf;

	if (blockid >= extdesc->sb->s_blocks_count) {
		DPRINTF("disk-introspection: parse_indirect_block: invalid block id\n");
		return NULL;
	}

	if (!current_block) {
		if (parent_block)
			parent_block->sb_bh = blockid;
		else
			inode->si_bh = blockid;
	} else {
		current_block->sb_sib = blockid;
	}
	
	ptd->blist[blockid].sb_level = indirect_level;
	ptd->blist[blockid].sb_inode = INODEID(inode);
	ptd->blist[blockid].sb_bh = 0;
	ptd->blist[blockid].sb_offset = offset;

	if (indirect_level) {
		current_block = NULL;

		if ((ret = posix_memalign((void **) &buf, DOM0BLOCKSIZE, DOM0BLOCKSIZE)) != 0)
			return NULL;
		if (align_readblock(fd, buf, blockid, extdesc->ed_block_size, DOM0BLOCKSIZE, DOM0BLOCKSIZE)) {
			DPRINTF("disk-introspection: parse_indirect_block: align_readblock error\n");
			free(buf);
			return NULL;
		}
		++offset;
		for (i = 0; i < extdesc->ed_block_size >> 2; i++) {
			link = *(((uint32_t *) buf) + i);
			if (link) {	
				current_block = parse_indirect_block(inode, &ptd->blist[blockid], current_block,
					link, indirect_level - 1, offset, extdesc, fd);
				if (current_block == NULL) {
					free(buf);
					return NULL;
				}
			}
			offset  += (indirect_level == 1 ? 1 : 
				(indirect_level == 2 ? (extdesc->ed_entries_per_block + 1): 
					(extdesc->ed_entries_per_block * (extdesc->ed_entries_per_block + 1) + 1)));
		}
		if (current_block) {
			current_block->sb_sib = 0;
		}
		free(buf);
	}
	return &ptd->blist[blockid];
}

static void clear_sblock(uint32_t blockid) {
	while (blockid) {
		ptd->blist[blockid].sb_inode = 0;
		if (ptd->blist[blockid].sb_bh) 
			clear_sblock(ptd->blist[blockid].sb_bh);
		ptd->blist[blockid].sb_bh = 0;
		blockid = ptd->blist[blockid].sb_sib;
		ptd->blist[blockid].sb_sib = 0;
	}
}

static void clear_sinode(struct sinode_t *sinode) {
	sinode->si_mode = 0;
	clear_sblock(sinode->si_bh);
	sinode->si_bh = 0;
}

static int
init_sinode_sblock(uint32_t inodeid, struct ext_inode_t *inode, struct ext_desc_t *extdesc, int fd) {
	int i;
	struct sblock_t *current_block = NULL, *tmp_block;
	struct sinode_t *sinode = &ptd->ilist[inodeid];

	sinode->si_mode = inode->i_mode;
	
	sinode->si_ctime = inode->i_ctime;
	sinode->si_mtime = inode->i_mtime;
	sinode->si_dtime = inode->i_dtime;
	sinode->si_size = inode->i_size;

	sinode->si_bh = 0;
	if (inode_isdir(inode->i_mode))
		++extdesc->ed_directories;

	for (i = 0; i < 12; i++) {
		if (inode->i_block[i]) {
			tmp_block = parse_indirect_block(sinode, NULL, current_block, inode->i_block[i], 0,  i, extdesc, fd);
			if (!tmp_block)
				goto fail_init_sinode;
		else
				current_block = tmp_block;
		}
	}

	if (inode->i_block[12]) {
		tmp_block = parse_indirect_block(sinode, NULL, current_block, inode->i_block[12], 1, 12, extdesc, fd);
		if (!tmp_block)
			goto fail_init_sinode;
		else
			current_block = tmp_block;
	}
	
	if (inode->i_block[13]) {
		tmp_block = parse_indirect_block(sinode, NULL, current_block, inode->i_block[13], 2, 
			13 + extdesc->ed_entries_per_block, extdesc, fd);
		if (!tmp_block) 
			goto fail_init_sinode;
		else
			current_block = tmp_block;
	}
	
	if (inode->i_block[14]) {
		tmp_block = parse_indirect_block(sinode, NULL, current_block, inode->i_block[14], 3, 
			14 + extdesc->ed_entries_per_block * (extdesc->ed_entries_per_block + 2), extdesc, fd);
		if (!tmp_block)
			goto fail_init_sinode;
		else
			current_block = tmp_block;
	}

	if (current_block)
		current_block->sb_sib = 0;
	return 0;

fail_init_sinode:
	if (current_block)
		current_block->sb_sib = 0;
	clear_sinode(sinode);
	return -1;
}

//TODO inodetable size not aligned with DOM0BLOCKSIZE
int get_diskstat(char *bbitmap, char *ibitmap, int fd, struct ext_desc_t *extdesc) {
	int i, j, k;
	int ret = 0;

	uint32_t offset_blockid;
	uint32_t itable_offset;
	uint32_t inodeid;

	char *tmp_bbitmap;
	char *tmp_ibitmap;
	char *tmp_itable;

	struct ext_inode_t inode;

	if ((ret = posix_memalign((void **) &tmp_bbitmap, DOM0BLOCKSIZE, DOM0BLOCKSIZE)) != 0) {
		DPRINTF("disk-introspection: get_diskstat: tmp_bbitmap, error posix_memalign: %d\n", ret);
		return ret;
	}

	if ((ret = posix_memalign((void **) &tmp_ibitmap, DOM0BLOCKSIZE, DOM0BLOCKSIZE)) != 0) {
		DPRINTF("disk-introspection: get_diskstat: tmp_ibitmap: error posix_memalign: %d\n", ret);
		free(tmp_bbitmap);
		return ret;
	}

	if ((ret = posix_memalign((void **) &tmp_itable, DOM0BLOCKSIZE, extdesc->ed_inode_table_blocks_per_group << extdesc->ed_block_shift)) != 0) {
		DPRINTF("disk-introspection: get_diskstat: tmp_itable: error posix_memalign: %d\n", ret);
		free(tmp_bbitmap);
		free(tmp_ibitmap);
		return ret;
	}

	for (i = 0; i < extdesc->ed_groups; i++) {
		offset_blockid = is_backup_group(i) ? (extdesc->sb->s_blocks_per_group * i + extdesc->bgdesc->bg_block_bitmap) : (extdesc->sb->s_blocks_per_group * i);
		if ((ret = align_readblock(fd, tmp_bbitmap, offset_blockid, 
			extdesc->ed_block_size, DOM0BLOCKSIZE, DOM0BLOCKSIZE))) {
			DPRINTF("disk-introspection: get_diskstat: blockbitmap read error\n");
			goto free_diskstat;
		}
		memcpy(bbitmap + (i << extdesc->ed_block_shift), tmp_bbitmap, extdesc->ed_block_size);

		if ((ret = align_readblock(fd, tmp_ibitmap, offset_blockid + 1, 
			extdesc->ed_block_size, DOM0BLOCKSIZE, DOM0BLOCKSIZE))) {
			DPRINTF("disk-introspection: get_diskstat: inodebitmap read error\n");
			goto free_diskstat;
		}
		memcpy(ibitmap + (i << extdesc->ed_block_shift), tmp_ibitmap, extdesc->ed_block_size);

		if ((ret = align_readblocks(fd, tmp_itable, offset_blockid + 2, 
			extdesc->ed_block_size, extdesc->ed_inode_table_blocks_per_group, 
			extdesc->ed_inode_table_blocks_per_group << extdesc->ed_block_shift, 
			DOM0BLOCKSIZE))) {
			DPRINTF("disk-introspection: get_diskstat: inodetable read error\n");
			goto free_diskstat;
		}
		extdesc->ed_free_inodes += extdesc->sb->s_inodes_per_group;
		for (j = 0; j < (extdesc->sb->s_inodes_per_group >> 3); j++) {
			if (tmp_ibitmap[j]) {
				for (k = 0; k < 8; k++) {
					inodeid = (j << 3) + k + 1;
					if (inodeid > extdesc->sb->s_inodes_per_group)
						break;
					inodeid += i * extdesc->sb->s_inodes_per_group;
					if (tmp_ibitmap[j] & (1 << k)) {
						--extdesc->ed_free_inodes;
						itable_offset = local_inode_index(inodeid, extdesc->sb) * extdesc->sb->s_inode_size;
						if ((ret = parse_inode(&inode, 
							tmp_itable + itable_offset, 
							(extdesc->ed_inode_table_blocks_per_group << extdesc->ed_block_shift) - itable_offset,
							extdesc->fs))) {
							DPRINTF("disk-introspection: get_diskstat: parse_inode error\n");
							goto free_diskstat;
						}
						if (inode_isregular(inode.i_mode) || inode_isdir(inode.i_mode)) {
							if ((ret = init_sinode_sblock(inodeid, &inode, extdesc, fd))) {
								DPRINTF("disk-introspection: get_diskstat: init_sinode_sblock error\n");
								goto free_diskstat;
							}
						}
					}
				}
			}
		}
	}

free_diskstat:
	free(tmp_bbitmap);
	free(tmp_ibitmap);
	free(tmp_itable);
	return ret;
}
