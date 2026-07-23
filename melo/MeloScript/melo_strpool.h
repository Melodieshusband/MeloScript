#ifndef MELO_STRPOOL_H
#define MELO_STRPOOL_H

#include "melo_value.h"
#include <stddef.h>

#define MELO_STRPOOL_SIZE (16 * 1024)

typedef struct {
    uint8_t buf[MELO_STRPOOL_SIZE];
    size_t offset;
} MeloStringPool;

void melo_strpool_init(MeloStringPool *pool);

static uint32_t melo_hash_str(const char *s, uint32_t len) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

MeloString *melo_strpool_intern(MeloStringPool *pool, const char *s, uint32_t len);

#endif
