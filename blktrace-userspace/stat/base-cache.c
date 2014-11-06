#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "base-cache.h"



extern FILE *logfile;
extern struct partition_data_t * ptd ;

static void free_cache_header(struct cache_header_t *ch, uint32_t link) {
	int i;
	for (i = 1; i < link; i++) {
		free(ch->ch[i].ce_data);
	}
	free(ch->ch);
}

static int init_cache_header(struct cache_header_t *ch, struct cache_t *cache, int link, uint32_t entry_size) {
	int i;
	ch->ch = (struct cache_entry_t *)
		malloc(sizeof(struct cache_entry_t) * link);

	ch->ch[0].ce_tag = 0;
	ch->ch[0].ce_next = &ch->ch[1];
	ch->ch[0].ce_prev = NULL;
	ch->ch[0].ce_data = (char *) malloc(entry_size);
	
	for (i = 1; i < link - 1; i++) {
		//ch->ch[i].ce_present = 0;
		ch->ch[i].ce_tag = i;
		ch->ch[i].ce_next = &ch->ch[i + 1];
		ch->ch[i].ce_prev = &ch->ch[i - 1];
		ch->ch[i].ce_data = (char *) malloc(entry_size);
	}
	
	ch->ch[link - 1].ce_tag = link - 1;
	ch->ch[link - 1].ce_next = NULL;
	ch->ch[link - 1].ce_prev = &ch->ch[link - 2];
	ch->ch[link - 1].ce_data = (char *) malloc(entry_size);

	ch->ch_used = NULL;
	ch->ch_tail = NULL;
	ch->ch_free = &ch->ch[0];

	ch->ch_container = cache;
	return 0;
}

void free_cache(struct cache_t *cache) {
	int i;
	for (i = 0; i < cache->c_ncache; i++) {
		free_cache_header(&cache->c_ch[i], cache->c_link);
	}
	free(cache->c_ch);
}

//TODO check return value of malloc
int init_cache(struct cache_t *cache, uint32_t ncache, uint32_t link, uint32_t entry_size) {
	int i;

	cache->c_ch = (struct cache_header_t *)
		malloc(sizeof(struct cache_header_t) * ncache);
	cache->c_ncache = ncache;
	cache->c_link = link;
	cache->c_entry_size = entry_size;
	cache->c_miss_count = 0;
	cache->c_hit_count = 0;
	cache->c_thrash_count = 0;

	for (i = 0; i < ncache; i++) {
		if (init_cache_header(&cache->c_ch[i], cache, link, entry_size))
			return -1;
	}
	return 0;
}

static void _remove_cache_entry(struct cache_header_t *ch, struct cache_entry_t *entry) {
	if (entry->ce_prev)
		entry->ce_prev->ce_next = entry->ce_next;
	else
		ch->ch_used = entry->ce_next;

	if (entry->ce_next)
		entry->ce_next->ce_prev = entry->ce_prev;
	else
		ch->ch_tail = entry->ce_prev;

	if (ch->ch_free) {
		ch->ch_free->ce_prev = entry;
	}

	entry->ce_next = ch->ch_free;
	ch->ch_free = entry;

	entry->ce_prev = NULL;
}

static struct cache_entry_t *_insert_cache_entry(struct cache_header_t *ch, uint32_t idx, char *buf) {
	struct cache_entry_t *entry = ch->ch_free;

	if (!ch->ch_free) {
		DPRINTF("disk-introspection: cache: _insert_cache_entry: no free cache\n");
		return NULL;
	}

	ch->ch_free = entry->ce_next;
	if (ch->ch_free)
		ch->ch_free->ce_prev = NULL;

	entry->ce_next = ch->ch_used;
	if (ch->ch_used) {
		ch->ch_used->ce_prev = entry;
	} else {
		ch->ch_tail = entry;
	}

	ch->ch_used = entry;

	entry->ce_idx = idx;

	return entry;
}

struct cache_entry_t *lookup_cache(struct cache_t *cache, uint32_t idx) {
	struct cache_header_t *ch = &cache->c_ch[idx % cache->c_ncache];
	struct cache_entry_t *entry = ch->ch_used;
	while (entry) {
		if (entry->ce_idx == idx) {
			return entry;
		}
		entry = entry->ce_next;
	}
	return NULL;
}

struct cache_entry_t *encache(struct cache_t *cache, uint32_t idx, char *buf) {
	struct cache_header_t *ch;
	struct cache_entry_t *entry;
	if ( !cache->c_ncache )return NULL;
	ch = &cache->c_ch[idx % cache->c_ncache];
	entry = lookup_cache(cache, idx);

	if (!entry) {
		if (!ch->ch_free) {
			if (!ch->ch_tail) {
				DPRINTF("disk-introspection: cache: encache error\n");
				return NULL;
			}
			_remove_cache_entry(ch, ch->ch_tail);
			++cache->c_thrash_count;
		}
		entry = _insert_cache_entry(ch, idx, buf);
	} else {
		if (entry->ce_prev) {
			entry->ce_prev->ce_next = entry->ce_next;
			if (entry->ce_next) {
				entry->ce_next->ce_prev = entry->ce_prev;
			} else {
				ch->ch_tail = entry->ce_prev;
			}

			ch->ch_used->ce_prev = entry;
			entry->ce_next = ch->ch_used;
			entry->ce_prev = NULL;
			ch->ch_used = entry;
		}
	}
	memcpy(entry->ce_data, buf, cache->c_entry_size);
	return entry;
}

//TODO fast_encache a non-existing entry
/*struct cache_entry_t *fast_encache(struct cache_t *cache, uint32_t idx, uint32_t tag, char *buf) {
	struct cache_header_t *ch = &cache->c_ch[idx % cache->c_ncache];
	struct cache_entry_t *entry;

	if (tag < cache->c_link) {
		entry = &ch->ch[tag];

		if (entry->ce_prev) {
			entry->ce_prev->ce_next = entry->ce_next;
			if (entry->ce_next) {
				entry->ce_next->ce_prev = entry->ce_prev;
			} else {
				ch->ch_tail = entry->ce_prev;
			}
			ch->ch_used = entry;
		}

		memcpy(entry->ce_data, buf, cache->c_entry_size);
		return entry;
	}

	return NULL;
}*/

struct cache_entry_t *decache(struct cache_t *cache, uint32_t idx) {
	struct cache_entry_t *entry = lookup_cache(cache, idx);
	if ( !cache->c_ncache )
		return NULL;
	if (entry) {
		_remove_cache_entry(&cache->c_ch[idx % cache->c_ncache], entry);
	}
	return entry;
}

//TODO fast_decache a non-existing entry
struct cache_entry_t *fast_decache(struct cache_t *cache, uint32_t idx, uint32_t tag) {
	struct cache_header_t *ch = &cache->c_ch[idx % cache->c_ncache];

	if (tag < cache->c_link) {
		_remove_cache_entry(ch, &ch->ch[tag]);
		return &ch->ch[tag];
	}
	return NULL;
}
