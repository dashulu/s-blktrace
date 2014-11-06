#ifndef EXT_GENERIC_H
#define EXT_GENERIC_H

#include <stdint.h>
#include "ext-generic.h"
#include "ext-journal.h"

#include "base-cache.h"
#include "base-intro.h"
#include "ext-struct.h"

#include "ssvd-clamav.h"

struct partition_data_t {
	// extfs struct. ONLY use (ptd->extdesc) to access them.
	struct ext_superblock_t		superblock;
	struct ext_blockgroupdesc_t blockgroupdesc;
	struct ext_desc_t 			extdesc;

	struct ext_journal_superblock_t journal_superblock;

	// extfs monitor struct
	struct sblock_t* blist;
	struct sinode_t* ilist;
	char *bbitmap, *ibitmap;

	// cache
	struct cache_t itable_cache;
	struct cache_t data_cache;

	//journal
	struct sjournal_t journal;
	
	uint64_t partition_offset;
};
extern struct partition_data_t * ptd ;


#endif
