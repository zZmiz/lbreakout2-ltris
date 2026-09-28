
#ifndef RP_GAME_ALLOC_H
#define RP_GAME_ALLOC_H

#include <stdlib.h>
#include <string.h>
#include "rp_platform.h"
static inline char *rp_game_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = rp_psram_malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
#define malloc rp_psram_malloc
#define calloc rp_psram_calloc
#define realloc rp_psram_realloc
#define free rp_psram_free
#define strdup rp_game_strdup
#endif
