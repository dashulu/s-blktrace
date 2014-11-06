#include <stdlib.h>

#include "ext-journal.h"
#include "base-intro.h"

#ifndef KVM
#include "xen-dummy.h"
#else
#include "dummy.h"
#endif

struct sjournal_transaction_t *trans, *free_trans;
extern FILE *logfile;

int is_journal_fs_block(char *buf) {
	return (readui32be(buf, 0) != JOURNAL_SIGNATURE);
}

//TODO size check correct
int parse_journal_standard_header(struct ext_journal_standard_header_t *journal_standard_header, char *buf, int size) {
	if (size < 12) {
		DPRINTF("disk-introspection: parse_journal_standard_header: insufficient buffer size\n");
		return -1;
	}
	journal_standard_header->jsh_signature = readui32be(buf, 0);
	journal_standard_header->jsh_block_type = readui32be(buf, 4);
	journal_standard_header->jsh_sequence = readui32be(buf, 8);
	if (journal_standard_header->jsh_signature != JOURNAL_SIGNATURE) {
		DPRINTF("disk-introspection: parse_journal_standard_header: invalid signature\n");
		return -1;
	}
	if (!journal_standard_header->jsh_block_type || journal_standard_header->jsh_block_type > JOURNAL_REVOKE_BLOCK) {
		DPRINTF("disk-introspection: parse_journal_standard_header: invalid block type\n");
		return -1;
	}	
	return 0;
}

int parse_journal_descriptor_entry(struct ext_journal_descriptor_entry_t *desc_entry, char *buf, int size) {
	if (size < 8) {
		DPRINTF("disk-introspection: parse_journal_descriptor_entry: insufficient buf size\n");
		return -1;
	}	
	desc_entry->jde_fs_block = readui32be(buf, 0);
	desc_entry->jde_entry_flags = readui32be(buf, 4);
	if (!(desc_entry->jde_entry_flags & JOURNAL_DESC_SAME_UUID)) {
		if (size < 24) {
			DPRINTF("disk-introspection: parse_journal_descriptor_entry: insufficient buf size\n");
			return -1;
		}
		return size - 24;
	} else {
		return size - 8;
	}
}

void dump_journal_superblock(FILE *logf, struct ext_journal_superblock_t *journal_superblock) {
	fprintf(logf, 
		"Journal Superblock\njs_signature: %x\njs_block_type: %u\njs_sequence: %u\njs_block_size: %u\n\
js_blocks: %u\njs_first_journal_block: %u\njs_first_sequence: %u\njs_first_transaction_block: %u\njs_error: %u\n\n",
		journal_superblock->js_header.jsh_signature,
		journal_superblock->js_header.jsh_block_type,
		journal_superblock->js_header.jsh_sequence, 
		journal_superblock->js_block_size, 
		journal_superblock->js_blocks,
		journal_superblock->js_first_journal_block,
		journal_superblock->js_first_sequence,
		journal_superblock->js_first_transaction_block,
		journal_superblock->js_error
		);
	fflush(logf);
}

int parse_journal_superblock(struct ext_journal_superblock_t *journal_superblock, char *buf, int size) {
	if (size < 36) {
		DPRINTF("disk-introspection: parse_journal_superblock: insufficient buffer size\n");
	}
	if (parse_journal_standard_header(&journal_superblock->js_header, buf, 12))
		return -1;
	journal_superblock->js_block_size = readui32be(buf, 12);
	journal_superblock->js_blocks = readui32be(buf, 16);
	journal_superblock->js_first_journal_block = readui32be(buf, 20);
	journal_superblock->js_first_sequence = readui32be(buf, 24);
	journal_superblock->js_first_transaction_block = readui32be(buf, 28);
	journal_superblock->js_error = readui32be(buf, 32);
	return 0;
}

struct sjournal_block_t *get_prev_journal_block(struct sjournal_block_t *journal_block, struct sjournal_block_t *journal_blocklist, int size) {
	if (journal_block == journal_blocklist || journal_block == journal_blocklist + 1) {
		return &journal_blocklist[size - 1];
	}
	return journal_block - 1;
}

// divide and search
struct sjournal_block_t *get_journalblock_from_datablock(uint32_t blockid, struct sjournal_block_t *journal_blocklist, int size) {
	int ll = 0, ul = size - 1; 
	int mid;

	if (blockid < journal_blocklist[0].sjb_blockid 
		|| blockid > journal_blocklist[size - 1].sjb_blockid)
		return NULL;

	while (ll <= ul) {
		mid = ll + ((ul - ll) >> 1);
		if (blockid < journal_blocklist[mid].sjb_blockid)
			ul = mid - 1;
		else if (blockid > journal_blocklist[mid].sjb_blockid)
			ll = mid + 1;
		else
			return &journal_blocklist[mid];		
	}
	return NULL;
}

static struct sjournal_transaction_t *alloc_trans(void) {
	struct sjournal_transaction_t *t;
	if (free_trans) {
		t = free_trans;
		free_trans = t->sjt_next;
		return t;
	}
	return NULL;
}

static void dealloc_trans(struct sjournal_transaction_t *transaction) {
	transaction->sjt_next = free_trans;
	free_trans = transaction;
}

void dump_journal_blocklist(FILE *logf, struct sjournal_block_t *journal_blocklist, int size) {
	int i;
	uint32_t t = 0;

	for (i = 0; i < size; i++) {
		if (t == 0) {
			t = journal_blocklist[i].sjb_blockid + 1;
			fprintf(logf, "%d", journal_blocklist[i].sjb_blockid);
		} else {
			if (t == journal_blocklist[i].sjb_blockid)
				t = journal_blocklist[i].sjb_blockid + 1;
			else {
				fprintf(logf, "-%d, %d", t - 1, journal_blocklist[i].sjb_blockid);
				t = journal_blocklist[i].sjb_blockid + 1;
			}
		}
	}

	if (t) {
		fprintf(logf, "-%d\n", t - 1);
	} else {
		fprintf(logf, "\n");
	}
	fflush(logf);
}

//TODO assert journal_block aligned with ascending blockid. check array limit exceeding
static struct sjournal_block_t *init_journal_blocklist(struct sjournal_block_t *journal_block, uint32_t blockid, struct sblock_t *blist) {
	while (blockid) {
		if (blist[blockid].sb_level) {
			journal_block = init_journal_blocklist(journal_block, blist[blockid].sb_bh, blist);
		} else {
			journal_block->sjb_blockid = blockid;
			++journal_block;
		}
		blockid = blist[blockid].sb_sib;
	}
	return journal_block;
}
#if 0
void dump_journal(FILE *logf, struct sjournal_t *journal, int size) {
	int i;
	struct sjournal_block_t *journal_block;
}
#endif

int get_journal(struct ext_journal_superblock_t *jsb, struct sjournal_t *journal, struct sinode_t *ilist, struct sblock_t *blist, int fd, struct ext_desc_t *extdesc) {
	int ret, i;
	char *buf;
	uint32_t offset;

	if ((ret = posix_memalign((void **) &buf, DOM0BLOCKSIZE, DOM0BLOCKSIZE)) != 0)
		return -1;

	// read the first journal block ---> journal superblock
	offset = align_readblock(fd, buf, ilist[extdesc->ed_journal_inum].si_bh, extdesc->ed_block_size, DOM0BLOCKSIZE, DOM0BLOCKSIZE);
	if (offset) {
		DPRINTF("disk-introspection: get_journal: error read journal block\n");
		goto free_get_journal;
	}

	if (parse_journal_superblock(jsb, buf, DOM0BLOCKSIZE)) {
		goto free_get_journal;
	}

	//TODO check return value of malloc
	trans = (struct sjournal_transaction_t *) malloc(JOURNAL_FREE_TRANSACTION * sizeof(struct sjournal_transaction_t));
	free_trans = trans;
	for (i = 0; i < JOURNAL_FREE_TRANSACTION - 1; i++) {
		trans[i].sjt_next = &trans[i + 1];
	}
	trans[JOURNAL_FREE_TRANSACTION - 1].sjt_next = NULL;

	journal->sj_transaction = NULL;
	journal->sj_blocklist = (struct sjournal_block_t *) malloc(jsb->js_blocks * sizeof(struct sjournal_block_t));
	if (init_journal_blocklist(journal->sj_blocklist, ilist[extdesc->ed_journal_inum].si_bh, blist) 
		!= &journal->sj_blocklist[jsb->js_blocks]) {
		DPRINTF("disk-introspection: get_journal: error init journal blocks\n");
		goto free_get_journal;
	}

	free(buf);
	return 0;
free_get_journal:
	free(buf);
	return -1;
}

int begin_transaction(struct sjournal_t *journal, struct sjournal_block_t *journal_block, uint32_t sequence) {
	if (journal->sj_transaction) {
		DPRINTF("disk-introspection: multiple transactions[%u]\n", sequence);
		return -1;
	}
	DPRINTF("begin_transaction[%u]\n", sequence);
	if (!(journal->sj_transaction = alloc_trans()))
		return -1;
	journal->sj_transaction->sjt_seq = sequence;
	journal->sj_transaction->sjt_desc = journal_block;
	return 0;
}

int end_transaction(struct sjournal_t *journal, struct sjournal_transaction_t *transaction) {
	if (journal->sj_transaction != transaction) {
		DPRINTF("disk-introspection: inidentical transaction\n");
		return -1;
	}
	dealloc_trans(transaction);
	journal->sj_transaction = NULL;
	return 0;
}

void free_journal(struct sjournal_t *journal) {
	free(trans);
	free(journal->sj_blocklist);
}
