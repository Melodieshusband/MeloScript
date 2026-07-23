#include "melo_strpool.h"
#include <string.h>

void melo_strpool_init(MeloStringPool *pool) {
    pool->offset = 0;
}

MeloString *melo_strpool_intern(MeloStringPool *pool, const char *s, uint32_t len) {
    uint32_t hash = melo_hash_str(s, len);

    size_t scan = 0;
    while (scan < pool->offset) {
        MeloString *existing = (MeloString *)(pool->buf + scan);
        if (existing->hash == hash && existing->length == len &&
            memcmp(existing->data, s, len) == 0) {
            return existing;
        }
        size_t entry_size = sizeof(MeloString) + existing->length + 1;
        entry_size = (entry_size + 15) & ~((size_t)15);
        scan += entry_size;
    }

    size_t needed = sizeof(MeloString) + len + 1;
    size_t aligned_needed = (needed + 15) & ~((size_t)15);

    if (pool->offset + aligned_needed > MELO_STRPOOL_SIZE) {
        return NULL;
    }

    MeloString *entry = (MeloString *)(pool->buf + pool->offset);
    entry->length = len;
    entry->hash = hash;
    entry->refcount = 0;
    memcpy(entry->data, s, len);
    entry->data[len] = '\0';

    pool->offset += aligned_needed;
    return entry;
}
