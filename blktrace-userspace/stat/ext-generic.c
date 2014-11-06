#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifndef KVM
#include "xen-dummy.h"
#else
#include "dummy.h"
#endif

#include "ext-generic.h"
#include "ext-struct.h"
#include "ext-journal.h"

#include "base-cache.h"
#include "base-intro.h"

#include "ssvd-clamav.h"

#include "queue.h"

extern FILE *logfile;
extern FILE *logfile_debug;

struct partition_data_t global;
struct partition_data_t * ptd = &global;
struct partition_data_t * ptd_arr = 0;

int get_mbr(int fd, char *buf, uint32_t size);//exported from ext-struct.c
int equal_sinode_inode_time(struct sinode_t *sia, struct ext_inode_t *ib);

//queue
struct queue_root *queue;

uint16_t global_version = 0;

//uint64_t partition_offset  = 0;

#define FREEJOURNALITEM 	1024

struct journal_item_t {
	uint32_t journal_flags;
	struct block_desc_t blockdesc;
	struct sjournal_block_t *journal_block;
	struct journal_item_t *next;
};

struct journal_item_t *free_journal_items, *journal_items;
struct journal_item_t *head[5], *tail[5];

static void *malloc_aligned(unsigned int size)
{
	void *ptr;
	int r = posix_memalign(&ptr, 256, size);
	if (r != 0) {
		DPRINTF("disk-introspection: malloc error\n");
		return NULL;
	}
	memset(ptr, 0, size);
	return ptr;
}

//TODO check the return value of calloc
static int
init_diskstat(struct sblock_t **blist, struct sinode_t **ilist, char **bbitmap, char **ibitmap, struct ext_desc_t *extdesc, int fd) {
	*blist = calloc(extdesc->sb->s_blocks_count, sizeof(struct sblock_t));
	*ilist = calloc(extdesc->sb->s_inodes_count + 1, sizeof(struct sinode_t));

	*bbitmap = calloc(extdesc->ed_groups << extdesc->ed_block_shift, sizeof(char));
	*ibitmap = calloc(extdesc->ed_groups << extdesc->ed_block_shift, sizeof(char));

	if (extdesc->fs <= EXT3FS) {
		if (get_diskstat(*bbitmap, *ibitmap, fd, extdesc)) {
			return -1;
		}
	} else {
		//extent_tree_parsing
	}

	return 0;
}

#if 0
static void dump_diskstat(void) {
	int i;
	// full disk dump
	fprintf(logfile, "full disk dump:\n");
	for (i = 1; i <= (ptd->superblock).s_inodes_count; i++) {
		if (ptd->ilist[i].si_mode) {
			dump_sinode(logfile, i, ptd->ilist, ptd->blist);	
		}
	}

	dump_superblock(logfile, &(ptd->superblock));
	dump_blockgroupdesc(logfile, &(ptd->blockgroupdesc));
	dump_ext_desc(logfile, &ptd->extdesc);

	if (ptd->extdesc.fs == EXT3FS) {
		dump_journal_superblock(logfile, &(ptd->journal_superblock));
		dump_sinode(logfile, 8, ptd->ilist, ptd->blist);
		dump_journal_blocklist(logfile, (ptd->journal).sj_blocklist, (ptd->journal_superblock).js_blocks);
	}
}
#endif

static int init_caches(void) {
	if (init_cache(&(ptd->itable_cache), 128, 7, ptd->extdesc.ed_block_size))
		return -1;
	if (init_cache(&(ptd->data_cache), 4096, 12, ptd->extdesc.ed_block_size))
		return -1;
	return 0;
}

static int init_journal(int fd) {
	int i;
	if (get_journal(&(ptd->journal_superblock), &(ptd->journal), ptd->ilist, ptd->blist, fd, &ptd->extdesc))
		return -1;
	
	journal_items = free_journal_items = calloc(FREEJOURNALITEM, sizeof(struct journal_item_t));
	for (i = 0; i < FREEJOURNALITEM - 1; i++) {
		free_journal_items[i].next = &free_journal_items[i + 1];
	}
	free_journal_items[FREEJOURNALITEM - 1].next = NULL;

	for (i = 0; i < 5; i++) {
		head[i] = NULL;
		tail[i] = NULL;
	}
	
	return 0;
}

static int init_queue(void) {
	queue = ALLOC_QUEUE_ROOT();
	return 0;
}
static int allocate_context( int sz ){
	ptd_arr = malloc( sz * sizeof( struct partition_data_t ) );
	return (! ptd_arr );
}
int free_context( void );//exported
int free_context( void ){
	free( ptd_arr );
	return 0 ;
}

int global_partition = -1;

int switch_context( int i );//exported
int switch_context( int i ){
	ptd = ptd_arr + i ;
	ptd->partition_offset = partition_table[i][0]*(SECTOR_SIZE);
	if (global_partition != i) {
		DPRINTF("switch to partition[%d]\n", i);
		global_partition = i;
	}
	return 0 ;
}
int switch_context_by_offset( uint64_t offset ) ;;//exported
int switch_context_by_offset( uint64_t offset ){
	offset /= SECTOR_SIZE;
	int i = 0 ;
	for ( i = 0 ; i < partition_count ; ++i )
		if ( partition_table[i][0] <= offset &&
			partition_table[i][1] > offset )
			{
				switch_context(i);
				return 0;
			}
	return -1;
}
int init_ext_introspection(int fd, int fs) ;//exported
int init_ext_introspection(int fd, int fs) {
	char *buf;
	int i;
	//struct sinode_t *sinode;
	// get initialized time
	time_t before, after;
	before = time(NULL);

	if (posix_memalign((void **) &buf, DOM0BLOCKSIZE, DOM0BLOCKSIZE) != 0) {
		return -1;
	}
	
	if (get_mbr(fd, buf, DOM0BLOCKSIZE))
		return -1;
	if ( partition_count == 0 ){ 
		//the image has no partition table but rather a single partition
		partition_table[0][0] = 0 ;
		partition_table[0][1] = ~0LL; // somehow the max
		partition_table[0][2] = 1;//not zero any way
		partition_count = 1 ;
	}
	allocate_context( partition_count );
	for ( i = 0 ; i < partition_count ; ++ i ){
		
		switch_context( i );
		
		if (get_superblock(&(ptd->superblock), fd, buf, DOM0BLOCKSIZE, fs))
			goto fail ;

		if (get_blockgroupdesc(&(ptd->blockgroupdesc), &(ptd->superblock), fd, buf, DOM0BLOCKSIZE, fs))
			goto fail ;

		if (init_ext_desc(&ptd->extdesc, &(ptd->superblock), &(ptd->blockgroupdesc), fs))
			goto fail ;
			
		if (init_diskstat(&ptd->blist, &ptd->ilist, &ptd->bbitmap, &ptd->ibitmap, &ptd->extdesc, fd))
			goto fail ;

		if (fs == EXT3FS && init_journal(fd))
			goto fail ;

		if (init_caches())
			goto fail ;
		
		continue;
		fail:
			partition_table[i][0] = 0;
			partition_table[i][1] = 0; //set to length zero
	}

	if (init_queue())
		return -1;

	free(buf);

	after = time(NULL);
	after -= before ;//prevent warning
	//dump_diskstat();

	return 0;
}

static void inodebitmap_write_introspection(struct block_desc_t *bd, char *buf) {
	memcpy(ptd->ibitmap + (bd->bd_group << ptd->extdesc.ed_block_shift), buf, ptd->extdesc.ed_block_size);
}

static void blockbitmap_write_introspection(struct block_desc_t *bd, char *buf) {
	memcpy(ptd->bbitmap + (bd->bd_group << ptd->extdesc.ed_block_shift), buf, ptd->extdesc.ed_block_size);
}

static int write_to_tmpfs(uint32_t inodeid, uint16_t version, uint32_t offset, uint32_t size, char *buf, uint32_t *count) {
	char filename[20];
	int fd;
	int ret;
	int64_t ret2;
	
	sprintf(filename, "/%s/%hu.%u", CLFSROOTDIR, version, inodeid);
	if ((fd = open(filename, O_RDWR | O_CREAT , 0777 )) == -1) {
		DPRINTF("disk-introspection: write_to_tmpfs: error open %s\n", filename);
		perror("disk-introspection: open");
		return -1;
	}

	if ((ret2 = lseek(fd, offset << ptd->extdesc.ed_block_shift, SEEK_SET)) 
		!= (((uint64_t) offset) << ptd->extdesc.ed_block_shift)) {
		DPRINTF("disk-introspection: write_to_tmpfs: error seek to %s: %lu (%d)\n", filename, ret2, errno);
		close(fd);
		return -1;
	} 
	if ((ret = write(fd, buf, size)) != size) {
		DPRINTF("disk-introspection: write_to_tmpfs: error write to %s: %d (%d)\n", filename, ret, errno);
		close(fd);
		return -1;
	}

	if (close(fd)) {
		DPRINTF("disk-introspection: write_to_tmpfs: error close[%s]\n", filename);
		perror("disk-introspection: close");
		return -1;
	}

	++(*count);

	return 0;
}

static int recur_copy_block(uint32_t inodeid, uint32_t version, uint32_t blockid, uint32_t *count) {
	int ret = 0;
	uint32_t size;
	struct cache_entry_t *entry;
	while (blockid) {
		if (ptd->blist[blockid].sb_level) {
			if (recur_copy_block(inodeid, version, ptd->blist[blockid].sb_bh, count)) {
				ret = -1;
			}
		} else {
			if ((entry = lookup_cache(&(ptd->data_cache), blockid))) {
				++(ptd->data_cache).c_hit_count;
				size = ptd->ilist[inodeid].si_size - ptd->blist[blockid].sb_offset * ptd->extdesc.ed_block_size;
				size = size > ptd->extdesc.ed_block_size ? ptd->extdesc.ed_block_size : size;
				if (write_to_tmpfs(inodeid, version, ptd->blist[blockid].sb_offset, size, entry->ce_data, count))
					ret = -1;
			} else {
				// modify the policy to tolerate cache missing in our system
				++(ptd->data_cache).c_miss_count;
				fprintf(logfile, "block [%u]@[%u] missing in cache\n", blockid, ptd->blist[blockid].sb_offset);
				ret = 0;
			}
		}
		blockid = ptd->blist[blockid].sb_sib;
	}
	return ret;
}

static int copy_inode(struct sinode_t *sinode) {
	char filename[20];
	struct queue_head *queue_item;
	uint16_t version;
	uint32_t count = 0;

	// ignore blank files....and file with type other than regular/folder
	if (sinode->si_bh) {
		version = global_version++;
		sprintf(filename, "/%s/%hu.%u", CLFSROOTDIR, version, INODEID(sinode));
		queue_item = malloc_aligned(sizeof(struct queue_head));
		//DPRINTF("write inode[%hu.%u] to tmpfs...", version, INODEID(sinode));
		
		if (recur_copy_block(INODEID(sinode), version, sinode->si_bh, &count)) {
			//DPRINTF("failed\n", version, INODEID(sinode));
			unlink(filename);
			return -1;
		}

		//DPRINTF("finish\n", version, INODEID(sinode));

		if (count == 0)
			return -1;
		
		queue_item = malloc_aligned(sizeof(struct queue_head));

		INIT_QUEUE_HEAD(queue_item);
		queue_item->value = INODEID(sinode);
		queue_item->version = version;
		queue_item->type = ONMEMINODE;

		queue_put(queue_item, queue);
	}
	return 0;
}

static int unmap_block(struct sblock_t *sblock, uint32_t inodeid) {
	uint32_t blockid, tblockid;

	if (inodeid != sblock->sb_inode) {
		fprintf(logfile, "Dangling link for block[%u]: O[%u], N[%u]\n", BLOCKID(sblock), sblock->sb_inode, inodeid);
		fflush(logfile);
		return 0;
	}

	sblock->sb_inode = 0;
	sblock->sb_offset = 0;
	blockid = sblock->sb_bh;
	decache(&(ptd->data_cache), BLOCKID(sblock));
	if (!sblock->sb_level && blockid ) {
		fprintf(logfile, "direct block[%u] with links[%u]\n", BLOCKID(sblock), blockid);
		return 0;
	} 

	while (blockid) {
		tblockid = ptd->blist[blockid].sb_sib;
		//fprintf(logfile, "unmap7: (%u)", blockid);
		unmap_block(&ptd->blist[blockid], inodeid);
		//fprintf(logfile, "$");
		blockid = tblockid;
	}
	sblock->sb_bh = 0;
	sblock->sb_sib = 0;
	sblock->sb_level = 0;
	return 0;
}

static int remap_block(struct sinode_t *sinode, struct sblock_t *sblock, char *buf) {
	int i;
	uint32_t link, blockid;
	struct sblock_t *current_block = NULL, 
					*ocurrent_block = NULL,
					*tcurrent_block;
	struct cache_entry_t *entry;
	blockid = sblock->sb_bh;

	for (i = 0; i < ptd->extdesc.ed_block_size >> 2; i++) {
		link = *(((uint32_t *) buf) + i);
		if (link >= (ptd->superblock).s_blocks_count) {
			fprintf(logfile, "parsing expired block[%u]\n", BLOCKID(sblock));
			fflush(logfile);
			return 0;
		}
		if (link) {
			if (blockid != link) {
				if (blockid)
					ocurrent_block = &ptd->blist[blockid];
				break;
			}
			current_block = &ptd->blist[blockid];
			blockid = current_block->sb_sib;
		}
	}

	if (!ocurrent_block && current_block && current_block->sb_sib) {
		ocurrent_block = &ptd->blist[current_block->sb_sib];
	}

	if (ocurrent_block) {
		while (1) {
			if (ocurrent_block->sb_sib)
				tcurrent_block = &ptd->blist[ocurrent_block->sb_sib];
			else
				tcurrent_block = NULL;
			//fprintf(logfile, "unmap1: (%u)", BLOCKID(ocurrent_block));
			unmap_block(ocurrent_block, sblock->sb_inode);
			//fprintf(logfile, "$");

			if (tcurrent_block)
				ocurrent_block = tcurrent_block;
			else
				break;
		}
	}

	for (; i < ptd->extdesc.ed_block_size >> 2; i++) {
		link = *(((uint32_t *) buf) + i);
		if (link >= (ptd->superblock).s_blocks_count) {
			fprintf(logfile, "parsing expired block[%u]\n", BLOCKID(sblock));
			fflush(logfile);
			return 0;
		}
		if (link) {
			if (get_inode_from_datablock(link)) {
				//fprintf(logfile, "unmap2: (%u)", link);
				unmap_block(&ptd->blist[link], sblock->sb_inode);
				//fprintf(logfile, "$");
			}
			if (!current_block) {
				sblock->sb_bh = link;
			} else {
				current_block->sb_sib = link;	
			}

			current_block = &ptd->blist[link];

			current_block->sb_level = sblock->sb_level - 1;
			current_block->sb_inode = INODEID(sinode);

			//current_block->sb_level can only be 0 ~ 2
			if (!current_block->sb_level) {
				current_block->sb_bh = 0;
				current_block->sb_offset = sblock->sb_offset + i;
			} else {
				current_block->sb_offset = (current_block->sb_level == 1 ? 
					(sblock->sb_offset + 1 + i * (1 + ptd->extdesc.ed_entries_per_block)) :
					(sblock->sb_offset + 1 + i * (1 + ptd->extdesc.ed_entries_per_block * (1 + ptd->extdesc.ed_entries_per_block))));
				entry = lookup_cache(&(ptd->data_cache), link);
				if (!entry) {
					//fprintf(logfile, "cache miss for block[%u]@[%u]\n", link, INODEID(sinode));
					//fflush(logfile);
					++(ptd->data_cache).c_miss_count;
				} else {
					//fprintf(logfile, "cache hit for block[%u]@[%u]\n", link, INODEID(sinode));
					//fflush(logfile);
					++(ptd->data_cache).c_hit_count;
				}
				if (entry && remap_block(sinode, &ptd->blist[link], entry->ce_data))
					return -1;
			}
		}
	}

	if (current_block)
		current_block->sb_sib = 0;

	fprintf(logfile, "\ninode[%u] mapping updated\n", INODEID(sinode));
	dump_sinode(logfile, INODEID(sinode), ptd->ilist, ptd->blist);
	return 0;
}

static int remove_sinode(struct sinode_t *sinode) {
	uint32_t blockid, tblockid;
	struct sblock_t *current_block;

	sinode->si_mode = 0;
	sinode->si_size = 0;
	blockid = sinode->si_bh;

	while (blockid) {
		current_block = &ptd->blist[blockid];
		tblockid = current_block->sb_sib;
		//fprintf(logfile, "unmap3: (%u)", BLOCKID(current_block));
		if (unmap_block(current_block, INODEID(sinode))) {
			//fprintf(logfile, "error unmap\n");
			return -1;
		}
		blockid = tblockid;
	}
	sinode->si_bh = 0;
	return 0;
}

static int diff_inode(struct sinode_t *sinode, struct ext_inode_t *inode, struct ext_inode_t *oinode) {
	int i;
	struct sblock_t *current_block, *ocurrent_block, *tcurrent_block;
	struct sinode_t *temp_inode;
	uint32_t blockid;
	uint32_t inodeid = INODEID(sinode);

	struct cache_entry_t *entry;


	// currently see inodes other than file/folder as no-existing......
	if (!inode_isdir(inode->i_mode) && !inode_isregular(inode->i_mode)) {
		inode->i_mode = 0;
	}

	if (sinode->si_mode) {
		if (inode->i_ctime <= inode->i_dtime || !inode->i_mode) {
			// inode removed
			fprintf(logfile, "\ninode[%u] removed\n", inodeid);
			dump_sinode(logfile, inodeid, ptd->ilist, ptd->blist);
			remove_sinode(sinode);
			sinode->si_dtime = inode->i_dtime;

			/*sinode->si_mode = 0;
			sinode->si_size = 0;
			sinode->si_dtime = inode->i_dtime;
			blockid = sinode->si_bh;

			while (blockid) {
				current_block = &ptd->blist[blockid];
				tblockid = current_block->sb_sib;
				//fprintf(logfile, "unmap3: (%u)", BLOCKID(current_block));
				if (unmap_block(current_block, INODEID(sinode))) {
					//fprintf(logfile, "error unmap\n");
					return -1;
				}
				//fprintf(logfile, "$");
				blockid = tblockid;
			}
			sinode->si_bh = 0;*/
			dump_sinode(logfile, inodeid, ptd->ilist, ptd->blist);
		} else {
			// see conclusion 2.
			if ((oinode && equal_inode_time(oinode, inode) && equal_inode_blocks(oinode, inode)) || equal_sinode_inode_time(sinode, inode))
				return 0;
			//inode modified
			fprintf(logfile, "\ninode[%u] modified\n", inodeid);
			dump_sinode(logfile, inodeid, ptd->ilist, ptd->blist);
			sinode->si_mode = inode->i_mode;

			sinode->si_size = inode->i_size;

			sinode->si_ctime = inode->i_ctime;
			sinode->si_mtime = inode->i_mtime;
			sinode->si_dtime = inode->i_dtime;

			if ((oinode && equal_inode_blocks(oinode, inode)) || equal_sinode_inode_blocks(sinode, inode)) {
				goto skip_inode_map_blocks;
			}

			blockid = sinode->si_bh;
			current_block = NULL;
			ocurrent_block = NULL;

			for (i = 0; i < 15; i++) {
				if (inode->i_block[i]) {
					if (inode->i_block[i] >= ptd->extdesc.sb->s_blocks_count) {
						DPRINTF("diff_inode: error parsing inode modification\n");
						remove_sinode(sinode);
						return 0;
					}
					if (inode->i_block[i] != blockid) {
						if (blockid)
							ocurrent_block = &ptd->blist[blockid];
						break;
					}
					current_block = &ptd->blist[inode->i_block[i]];
					blockid = current_block->sb_sib;
				}
			}

			if (!ocurrent_block && current_block && current_block->sb_sib) {
				ocurrent_block = &ptd->blist[current_block->sb_sib];
			}

			// get rid of original mapping	
			if (ocurrent_block) {
				while (1) {
					if (ocurrent_block->sb_sib)
						tcurrent_block = &ptd->blist[ocurrent_block->sb_sib];
					else
						tcurrent_block = NULL;

					//fprintf(logfile, "unmap4: (%u)", BLOCKID(ocurrent_block));
					unmap_block(ocurrent_block, INODEID(sinode));
					//fprintf(logfile, "$");
					if (tcurrent_block)
						ocurrent_block = tcurrent_block;
					else
						break;
				}
			}

			// construct new mapping
			for (; i < 15; i++) {
				if (inode->i_block[i]) {
					if (inode->i_block[i] >= ptd->extdesc.sb->s_blocks_count) {
						DPRINTF("diff_inode: error parsing inode modification\n");
						if (current_block)
							current_block->sb_sib = 0;
						remove_sinode(sinode);
						return 0;
					}
					if ((temp_inode = get_inode_from_datablock(inode->i_block[i]))) {
						//fprintf(logfile, "unmap5: (%u)", inode->i_block[i]);
						unmap_block(&ptd->blist[inode->i_block[i]], INODEID(temp_inode));
						//fprintf(logfile, "$");
					}

					if (!current_block) {
						sinode->si_bh = inode->i_block[i];
					} else {
						current_block->sb_sib = inode->i_block[i];	
					}
					
					current_block = &ptd->blist[inode->i_block[i]];
					current_block->sb_level = i < 12 ? 0 : (i == 12 ? 1 : (i == 13 ? 2 : 3));
					current_block->sb_inode = INODEID(sinode);

					if (i < 12) {
						current_block->sb_bh = 0;
						current_block->sb_offset = i;
					} else {
						current_block->sb_offset = (i == 12 ? 12 : 
							(i == 13 ? (i + ptd->extdesc.ed_entries_per_block) : 
								(i + (ptd->extdesc.ed_entries_per_block + 1) * ptd->extdesc.ed_entries_per_block)));
						entry = lookup_cache(&(ptd->data_cache), inode->i_block[i]);
						if (!entry) {
							//fprintf(logfile, "cache miss for block[%u]@[%u]\n", inode->i_block[i], INODEID(sinode));
							//fflush(logfile);
							++(ptd->data_cache).c_miss_count;
						} else {
							//fprintf(logfile, "cache hit for block[%u]@[%u]\n", inode->i_block[i], INODEID(sinode));
							//fflush(logfile);
							++(ptd->data_cache).c_hit_count;
						}
						if (entry && remap_block(sinode, current_block, entry->ce_data))
							return -1;
					}
				}
			}
			if (current_block)
				current_block->sb_sib = 0;

skip_inode_map_blocks:
			// only check regular file....
			if (inode_isregular(sinode->si_mode))
				copy_inode(sinode);
			dump_sinode(logfile, inodeid, ptd->ilist, ptd->blist);
			return 0;
		}
	} else {
		// TODO improve check
		if (inode->i_ctime > inode->i_dtime && inode->i_mode) {
			//inode created
			fprintf(logfile, "\ninode[%u] created\n", inodeid);
			dump_sinode(logfile, inodeid, ptd->ilist, ptd->blist);
			sinode->si_mode = inode->i_mode;

			sinode->si_size = inode->i_size;
			
			sinode->si_ctime = inode->i_ctime;
			sinode->si_mtime = inode->i_mtime;
			sinode->si_dtime = inode->i_dtime;

			current_block = NULL;
			sinode->si_bh = 0;

			for (i = 0; i < 15; i++) {
				if (inode->i_block[i]) {
					if (inode->i_block[i] >= ptd->extdesc.sb->s_blocks_count) {
						DPRINTF("diff_inode: error parsing inode modification\n");
						if (current_block)
							current_block->sb_sib = 0;
						remove_sinode(sinode);
						return 0;
					}
					if ((temp_inode = get_inode_from_datablock(inode->i_block[i]))) {
						//fprintf(logfile, "unmap6: (%u)", inode->i_block[i]);
						unmap_block(&ptd->blist[inode->i_block[i]], INODEID(temp_inode));
						//fprintf(logfile, "\n");
					}

					if (!current_block) {
						sinode->si_bh = inode->i_block[i];
					} else {
						current_block->sb_sib = inode->i_block[i];	
					}

					current_block = &ptd->blist[inode->i_block[i]];
					current_block->sb_level = i < 12 ? 0 : (i == 12 ? 1 : (i == 13 ? 2 : 3));
					current_block->sb_inode = INODEID(sinode);

					if (i < 12) {
						current_block->sb_bh = 0;
						current_block->sb_offset = i;
					} else {
						current_block->sb_offset = (i == 12 ? 12 : 
							(i == 13 ? (i + ptd->extdesc.ed_entries_per_block) : 
								(i + (ptd->extdesc.ed_entries_per_block + 1) * ptd->extdesc.ed_entries_per_block)));
						entry = lookup_cache(&(ptd->data_cache), inode->i_block[i]);
						if (!entry) {
							//fprintf(logfile, "cache miss for block[%u]@[%u]\n", inode->i_block[i], INODEID(sinode));
							//fflush(logfile);
							++(ptd->data_cache).c_miss_count;
						} else {
							//fprintf(logfile, "cache hit for block[%u]@[%u]\n", inode->i_block[i], INODEID(sinode));
							//fflush(logfile);
							++(ptd->data_cache).c_hit_count;
						}
						if (entry && remap_block(sinode, current_block, entry->ce_data))
							return -1;
					}
				}
			}

			if (current_block)
				current_block->sb_sib = 0;

			// only check regular file....
			if (inode_isregular(sinode->si_mode))
				copy_inode(sinode);
			dump_sinode(logfile, inodeid, ptd->ilist, ptd->blist);
			
			
		}
	}
	return 0;
}

//TODO i_block overflow
static int inodetable_write_introspection(struct block_desc_t *bd, char *buf) {
	int i;
	struct sinode_t *sinode;
	struct ext_inode_t inode, oinode;
	uint32_t inodeid = (bd->bd_group_offset - bd->bd_group_first_block - 2) * ptd->extdesc.ed_inodes_per_block + 
		bd->bd_group * ptd->extdesc.sb->s_inodes_per_group + 1;

	uint32_t size = ptd->extdesc.ed_block_size;
	

	struct cache_entry_t *entry = lookup_cache(&(ptd->itable_cache), bd->bd_id);
	char *obuf = entry ? entry->ce_data : NULL;

	//TODO handle ext3
	if (!entry && ptd->extdesc.fs == EXT2FS)
		fprintf(logfile, "cache miss for inode table[%u]\n", bd->bd_id);

	for (i = 0; i < ptd-> extdesc.ed_inodes_per_block; i++) {
		if (parse_inode(&inode, buf, size, ptd->extdesc.fs)) {
			DPRINTF("disk-introspection: error parsing inode table\n");
		} else {
			sinode = &ptd->ilist[inodeid];
			if (obuf) {
				if (parse_inode(&oinode, obuf, size, ptd->extdesc.fs)) {
					DPRINTF("disk-introspection: error parsing old inode table\n");
				} else {
					if (!equal_inode(&oinode, &inode)) {
						if (diff_inode(sinode, &inode, &oinode)) {
							DPRINTF("disk-introspection: error diff inode table2\n");
						}
					}
				}
				obuf += ptd->extdesc.sb->s_inode_size;
			} else {
				if (diff_inode(sinode, &inode, NULL)) {
					DPRINTF("disk-introspection: error diff inode table\n");		
				}
			}
		}

		buf += ptd->extdesc.sb->s_inode_size;
		size -= ptd->extdesc.sb->s_inode_size;
		++inodeid;
	}
	return 0;
}

//TODO assert a data block cannot simultaneously exist in 2 caches.
static int datablock_write_introspection(struct block_desc_t *bd, char *buf) {
	struct sinode_t *sinode;
	sinode = get_inode_from_datablock(bd->bd_id);

	//TODO filename cache
	if (sinode) {
		if (inode_isdir(sinode->si_mode)) {
			//TODO build directory hierachy
		}
		if (ptd->blist[bd->bd_id].sb_level) {
			if (remap_block(sinode, &ptd->blist[bd->bd_id], buf)) {
				return -1;
			}
		}
	}

	return 0;
}

static int ext2_cache_block(struct block_desc_t *bd, char *buf) {
	if (bd->bd_type == INODETABLE) {
		if (!encache(&(ptd->itable_cache), bd->bd_id, buf)) {
			DPRINTF("disk-introspection: error encache\n");
			return -1;
		}
	} else if (bd->bd_type == DATABLOCK) {
		if (!encache(&(ptd->data_cache), bd->bd_id, buf)) {
			DPRINTF("disk-introspection: error encache\n");
			return -1;
		}
	}
	return 0;
}

//TODO check the return value of ..._write_introspection
static int ext2_introspection(int op, struct block_desc_t *bd, char *buf) {
	if (op) {
		switch (bd->bd_type) {
			case BLOCKBITMAP:
				blockbitmap_write_introspection(bd, buf);
				break;
			case INODEBITMAP:
				inodebitmap_write_introspection(bd, buf);
				break;
			case INODETABLE:
				if (inodetable_write_introspection(bd, buf))
					return -1;
				break;
			case DATABLOCK:
				if (datablock_write_introspection(bd, buf))
					return -1;
				break;
		}
	}

	if (ext2_cache_block(bd, buf))
		return -1;
	return 0;
}

static int get_journal_type(uint32_t blockid, int blocktype) {
	switch (blocktype) {
		case INODEBITMAP: case BLOCKBITMAP:
			return 0;
		case DATABLOCK:
			if (ptd->blist[blockid].sb_inode) {
				if (ptd->blist[blockid].sb_level) return 2;
				return 4;
			}
			return 1;
		case INODETABLE:
			return 3;
	}
	return -1;
}

//TODO handle different (ptd->journal) block size
static int journal_rw_introspection(int op, struct block_desc_t *bd, char *buf) {
	int i;
	int size;
	struct ext_journal_standard_header_t journal_header;
	struct ext_journal_descriptor_entry_t journal_desc_entry;
	struct sjournal_block_t *journal_block, *temp_journal_block;
	struct block_desc_t blockdesc;

	struct journal_item_t *ptr;

	// indirect (ptd->journal) block, ignored in (ptd->journal) blocklist
	if (!(journal_block = get_journalblock_from_datablock(bd->bd_id, (ptd->journal).sj_blocklist, 
		(ptd->journal_superblock).js_blocks))) {
		return 0;
	}

	if (op) {
		if (is_journal_fs_block(buf)) {
			memcpy(journal_block->sjb_data, buf, (ptd->journal_superblock).js_block_size);
		} else {
			if (parse_journal_standard_header(&journal_header, buf, ptd->extdesc.ed_block_size))
				return -1;
			switch (journal_header.jsh_block_type) {
				case JOURNAL_DESCRIPTOR_BLOCK:
					DPRINTF("disk-introspection: begin@[%u]: ", bd->bd_id);
					if (begin_transaction(&(ptd->journal), journal_block, journal_header.jsh_sequence)) {
						return -1;
					}
					memcpy(journal_block->sjb_data, buf, (ptd->journal_superblock).js_block_size);
					break;
				case JOURNAL_COMMIT_BLOCK:
					DPRINTF("disk-introspection: commit@[%u]: ", bd->bd_id);
					if (!(ptd->journal).sj_transaction) {
						// an empty transaction  
						DPRINTF("empty\n");
						return 0;
					}
					DPRINTF("commit transaction[%u]\n", (ptd->journal).sj_transaction->sjt_seq);
					if ((ptd->journal).sj_transaction->sjt_seq != journal_header.jsh_sequence) {
						DPRINTF("commit non-identical transaction[%u]\n", (ptd->journal).sj_transaction->sjt_seq);
						return -1;
					}

					size = (ptd->journal_superblock).js_block_size - 12;
					buf = (ptd->journal).sj_transaction->sjt_desc->sjb_data + 12;
					temp_journal_block = (ptd->journal).sj_transaction->sjt_desc;

					while ((size = parse_journal_descriptor_entry(&journal_desc_entry, buf, size)) >= 0) {
						// cycling
						if (temp_journal_block == &(ptd->journal).sj_blocklist[(ptd->journal_superblock).js_blocks - 1])
							temp_journal_block = &(ptd->journal).sj_blocklist[1];
						else
							++temp_journal_block;

						if (!init_blockdesc2(&blockdesc, journal_desc_entry.jde_fs_block, &ptd->extdesc)) {
							i = get_journal_type(blockdesc.bd_id, blockdesc.bd_type);
							if (i >=0 && i < 5) {
								if (!free_journal_items) {
									DPRINTF("disk-introspection: insufficient (ptd->journal) items. WooA");
									return -1;
								}
								if (!head[i]) {
									head[i] = tail[i] = free_journal_items;
								} else {
									tail[i]->next = free_journal_items;
									tail[i] = free_journal_items;
								}

								free_journal_items = free_journal_items->next;
								tail[i]->next = NULL;
								tail[i]->journal_flags = journal_desc_entry.jde_entry_flags;
								tail[i]->journal_block = temp_journal_block;

								tail[i]->blockdesc.bd_id = blockdesc.bd_id;
								tail[i]->blockdesc.bd_group = blockdesc.bd_group;
								tail[i]->blockdesc.bd_type = blockdesc.bd_type;
								tail[i]->blockdesc.bd_group_offset = blockdesc.bd_group_offset;
								tail[i]->blockdesc.bd_group_first_block = blockdesc.bd_group_first_block;

							}
						} else {
							DPRINTF("disk-introspection: error init blockdesc\n");
						}

						if (journal_desc_entry.jde_entry_flags & JOURNAL_DESC_SAME_UUID)
							buf += 8;
						else
							buf += 24;

						if (journal_desc_entry.jde_entry_flags & JOURNAL_DESC_LAST_ENTRY)
							break;
					}

					for (i = 0; i < 5; i++) {
						ptr = head[i];
						while (ptr) {
							if (ptr->journal_flags & JOURNAL_DESC_ESCAPE) {
								//TODO validate the correctness. our machine uses little-endian
								*((uint32_t *) (ptr->journal_block->sjb_data)) = 0x98c93bc0;
							}
							//fprintf(logfile, "\tfilesystem block[%u]@[%u]\n", ptr->blockdesc.bd_id, ptr->journal_block->sjb_blockid);
							if (ext2_introspection(1, &ptr->blockdesc, ptr->journal_block->sjb_data)) {
								DPRINTF("disk-introspection: error introspecting (ptd->journal)");
							}
							if (ptr->journal_flags & JOURNAL_DESC_ESCAPE)
								*((uint32_t *) (ptr->journal_block->sjb_data)) = 0;
							ptr = ptr->next;
						}
						if (tail[i]) {
							tail[i]->next = free_journal_items;
							free_journal_items = head[i];
						}
						head[i] = tail[i] = NULL;
					}

					fprintf(logfile, "\n");
					fflush(logfile);

					if (end_transaction(&(ptd->journal), (ptd->journal).sj_transaction))
						return -1;
					break;
				case JOURNAL_REVOKE_BLOCK:
					//fprintf(logfile_debug, "revoke block[%u]@[%u]\n", journal_header.jsh_sequence, journal_block->sjb_blockid);
					//fflush(logfile_debug);
					memcpy(journal_block->sjb_data, buf, (ptd->journal_superblock).js_block_size);
					break;
			}
		}
	}
	return 0;
}

static int ext3_introspection(int op, struct block_desc_t *bd, char *buf) {
	struct sinode_t *sinode;
	if (bd->bd_type == DATABLOCK) {
		sinode = get_inode_from_datablock(bd->bd_id);
		if (sinode && INODEID(sinode) == ptd->extdesc.ed_journal_inum) {
			if (journal_rw_introspection(op, bd, buf))
				return -1;
			return 0;
		}
	}
	if (ext2_cache_block(bd, buf))
		return -1;
	return 0;
}
int ext_generic_introspection(int op, uint64_t offset, uint32_t size, char *buf) ;//exported to eliminate warnings
int ext_generic_introspection(int op, uint64_t offset, uint32_t size, char *buf) {
	struct block_desc_t blockdesc;
	
	if ( !ptd->extdesc.ed_block_size ){
		fprintf(stderr, "extdesc not initialized!\n" );
		return -1;
	}
	if (offset % ptd->extdesc.ed_block_size) {
		if (offset != SUPERBLOCKOFFSET || size != SUPERBLOCKSIZE) {
			//DPRINTF("disk-introspection: ext_introspection: offset[%lu](%u) not aligned with block size\n", offset, size);
		}
		return 0;
	}
	//TODO: handle not divisible
	if (
	#ifndef KVM
	(size >> ptd->extdesc.ed_block_shift) > 1 || 
	#endif
	size % ptd->extdesc.ed_block_size) {		
		//DPRINTF("disk-introspection: ext_introspection: invalid block operation size: %u\n", size);
		//return -1;
		return 0; //get rid of so many rubbish error message...
	}
	#ifdef KVM
	if ( (size >> ptd->extdesc.ed_block_shift) > 1 ){
		uint64_t block_size = 1 << ptd->extdesc.ed_block_shift;
		int ret = 0;
		while ( size ){
			ret |= !!( ext_generic_introspection( op , offset , block_size , buf ) ) ;
			offset += block_size;
			buf += block_size;
			size -= block_size;
		}
		return ret ? -1 : 0 ;
	}
	#endif

	if (init_blockdesc(&blockdesc, offset, &ptd->extdesc)) {
		DPRINTF("disk-introspection: ext_introspection: error init_blockdesc\n");
		return -1;
	}

	if (ptd->extdesc.fs == EXT3FS) {
		if (ext3_introspection(op, &blockdesc, buf)) {
			DPRINTF("disk-introspection: error ext3_introspection\n");
			return -1;
		}
	} else if (ptd->extdesc.fs == EXT2FS) {
		if (ext2_introspection(op, &blockdesc, buf)) {
			DPRINTF("disk-introspection: error ext2_introspection\n");
			return -1;
		}
	}

	return 0;
}

static void free_diskstat(void) {
	free(ptd->blist);
	free(ptd->ilist);
	free(ptd->bbitmap);
	free(ptd->ibitmap);
}

static void free_caches(void) {
	free_cache(&(ptd->itable_cache));
	free_cache(&(ptd->data_cache));
}

void free_ext_introspection(void)  ;//exported
void free_ext_introspection(void) {
	int i;
	/*fprintf(logfile, "data cache hit count: %u\n", (ptd->data_cache).c_hit_count);
	fprintf(logfile, "data cache miss count: %u\n", (ptd->data_cache).c_miss_count);
	fprintf(logfile, "data cache thrash count: %u\n", (ptd->data_cache).c_thrash_count);

	free_diskstat();
	free_caches();

	if (ptd->extdesc.fs == EXT3FS) {
		free(journal_items);
		free_journal(&(ptd->journal));
	}*/

	for ( i = 0 ; i < partition_count ; ++ i ){
		switch_context( i );
		fprintf(logfile, "[%d]: data cache hit count: %u\n", i, (ptd->data_cache).c_hit_count);
		fprintf(logfile, "[%d]: data cache miss count: %u\n", i, (ptd->data_cache).c_miss_count);
		fprintf(logfile, "[%d]: data cache thrash count: %u\n", i, (ptd->data_cache).c_thrash_count);

		free_diskstat();
		free_caches();
		if (ptd->extdesc.fs == EXT3FS) {
			free(journal_items);
			free_journal(&(ptd->journal));
		}
		
	}
}
