#ifndef EXT_JOURNAL_H
#define EXT_JOURNAL_H

#include <stdint.h>
#include <stdio.h>

#include "ext-struct.h"

#define JOURNAL_SIGNATURE	0xc03b3998

#define JOURNAL_DESCRIPTOR_BLOCK	1
#define JOURNAL_COMMIT_BLOCK		2
#define JOURNAL_SUPERBLOCK_VER_1_BLOCK	3
#define JOURNAL_SUPERBLOCK_VER_2_BLOCK	4
#define JOURNAL_REVOKE_BLOCK		5

struct ext_journal_standard_header_t {
	uint32_t jsh_signature;
	uint32_t jsh_block_type;
	uint32_t jsh_sequence;
};

struct ext_journal_superblock_t {
	struct ext_journal_standard_header_t js_header;
	uint32_t js_block_size;
	uint32_t js_blocks;
	uint32_t js_first_journal_block;
	uint32_t js_first_sequence;
	uint32_t js_first_transaction_block;
	uint32_t js_error;
};

#define JOURNAL_DESC_ESCAPE		0x01
#define JOURNAL_DESC_SAME_UUID	0x02
#define JOURNAL_DESC_DELETED	0x04	//not used? TODO make sure
#define JOURNAL_DESC_LAST_ENTRY	0x08

struct ext_journal_descriptor_t {
	struct ext_journal_standard_header_t jd_header;
};

struct ext_journal_descriptor_entry_t {
	uint32_t jde_fs_block;
	uint32_t jde_entry_flags;
	//uint32_t jde_uuid;		useless?
};

struct ext_journal_commit_t {
	struct ext_journal_standard_header_t jc_header;
};

struct ext_journal_revoke_t {
	struct ext_journal_standard_header_t jr_header;
	uint32_t jr_revoke_size;	//in bytes
};

struct sjournal_block_t;

#define JOURNAL_FREE_TRANSACTION	16

struct sjournal_transaction_t {
	uint32_t sjt_seq;
	struct sjournal_transaction_t *sjt_next;
	struct sjournal_block_t *sjt_desc;
};

struct sjournal_block_t {
	uint32_t sjb_blockid;
	char sjb_data[4096];
};

struct sjournal_t {
	struct sjournal_transaction_t *sj_transaction;
	struct sjournal_block_t *sj_blocklist;
};

int is_journal_fs_block(char *buf);

int parse_journal_standard_header(struct ext_journal_standard_header_t *journal_standard_header, char *buf, int size);

int parse_journal_descriptor_entry(struct ext_journal_descriptor_entry_t *desc_entry, char *buf, int size);

void dump_journal_superblock(FILE *logf, struct ext_journal_superblock_t *journal_superblock);

int parse_journal_superblock(struct ext_journal_superblock_t *journal_superblock, char *buf, int size);

void dump_journal_blocklist(FILE *logf, struct sjournal_block_t *journal_blocklist, int size);

struct sjournal_block_t *get_prev_journal_block(struct sjournal_block_t *journal_block, 
	struct sjournal_block_t *journal_blocklist, int size);

struct sjournal_block_t *get_journalblock_from_datablock(uint32_t blockid, 
	struct sjournal_block_t *journal_blocklist, int size);
int get_journal(struct ext_journal_superblock_t *jsb, struct sjournal_t *journal, 
	struct sinode_t *ilist, struct sblock_t *blist, int fd, struct ext_desc_t *extdesc);

int begin_transaction(struct sjournal_t *journal, struct sjournal_block_t *journal_block, uint32_t sequence);
int end_transaction(struct sjournal_t *journal, struct sjournal_transaction_t *transaction);

void free_journal(struct sjournal_t *journal);

#endif