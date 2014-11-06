#include <undered_map>

using namespace std;

typedef std::unordered_map <uint32_t, cache_entry_t > cache_type;

extern "C" {

struct cache_entry_t *encache(struct cache_t *cache, uint32_t, char *buf) {
	std::unordered_map < uint32_t , cache_entry_t> *ptr = 
}

struct cache_entry_t *decache(struct cache_t *cache, uint32_t idx) {

}

}