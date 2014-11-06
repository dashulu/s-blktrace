#ifndef BASE_CACHE_H
#define BASE_CACHE_H

#include <stdint.h>

struct cache_entry_t {
	//int ce_present;
	uint32_t ce_idx;
	uint32_t ce_tag;
	struct cache_entry_t *ce_next;
	struct cache_entry_t *ce_prev;
	char *ce_data;
};

struct cache_header_t {
	struct cache_entry_t *ch;
	struct cache_entry_t *ch_used;
	struct cache_entry_t *ch_tail;
	struct cache_entry_t *ch_free;

	struct cache_t *ch_container;
};

struct cache_t {
	struct cache_header_t *c_ch;
	uint32_t c_ncache;
	uint32_t c_link;
	uint32_t c_entry_size;
	uint32_t c_miss_count;
	uint32_t c_hit_count;
	uint32_t c_thrash_count;
};

int init_cache(struct cache_t *cache, uint32_t ncache, uint32_t link, uint32_t size);
void free_cache(struct cache_t *cache);

struct cache_entry_t *encache(struct cache_t *cache, uint32_t, char *buf);
struct cache_entry_t *fast_encache(struct cache_t *cache, uint32_t idx, uint32_t tag, char *buf);

struct cache_entry_t *decache(struct cache_t *cache, uint32_t idx);
struct cache_entry_t *fast_decache(struct cache_t *cache, uint32_t idx, uint32_t tag);

struct cache_entry_t *lookup_cache(struct cache_t *cache, uint32_t idx);

#endif