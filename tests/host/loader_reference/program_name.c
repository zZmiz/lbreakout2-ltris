

#include "program_name.h"

#include <string.h>

#include "storage.h"
#include "uf2_format.h"
#include "boot_config.h"

#define BI_MARKER_START          0x7188ebf2u
#define BI_MARKER_END            0xe71aa390u
#define BI_TYPE_ID_AND_STRING    6u
#define BI_TAG_RP                ((((uint16_t)'P') << 8) | (uint16_t)'R')
#define BI_ID_RP_PROGRAM_NAME    0x02031c86u

#define BI_SEARCH_BYTES          4096u

static inline bool in_xip_range(uint32_t addr, uint32_t base, uint32_t size)
{
    return addr >= base && addr - base < size;
}

bool program_name_from_xip(uint32_t base, uint32_t region_size,
                           char *out, unsigned cap)
{
    if (cap > 0) out[0] = '\0';
    if (cap == 0) return false;

    const uint32_t *p   = (const uint32_t *)(uintptr_t)base;
    const uint32_t *end = (const uint32_t *)(uintptr_t)(base + BI_SEARCH_BYTES - 20u);
    uint32_t bi_s = 0, bi_e = 0;
    bool found = false;
    for (; p <= end; p++) {
        if (p[0] != BI_MARKER_START) continue;
        if (p[4] != BI_MARKER_END)   continue;
        bi_s = p[1];
        bi_e = p[2];
        if (!in_xip_range(bi_s, base, region_size)) return false;
        if (!in_xip_range(bi_e, base, region_size)) return false;
        if (bi_s > bi_e || ((bi_e - bi_s) & 3u) != 0)  return false;
        found = true;
        break;
    }
    if (!found) return false;

    const uint32_t *pp = (const uint32_t *)(uintptr_t)bi_s;
    const uint32_t *pe = (const uint32_t *)(uintptr_t)bi_e;
    for (; pp < pe; pp++) {
        uint32_t entry_addr = *pp;
        if (!in_xip_range(entry_addr, base, region_size)) continue;
        const uint8_t *e = (const uint8_t *)(uintptr_t)entry_addr;
        uint16_t type = (uint16_t)(e[0] | (e[1] << 8));
        uint16_t tag  = (uint16_t)(e[2] | (e[3] << 8));
        if (type != BI_TYPE_ID_AND_STRING || tag != BI_TAG_RP) continue;
        uint32_t id = (uint32_t)e[4] | ((uint32_t)e[5] << 8)
                    | ((uint32_t)e[6] << 16) | ((uint32_t)e[7] << 24);
        if (id != BI_ID_RP_PROGRAM_NAME) continue;
        uint32_t str_addr = (uint32_t)e[8] | ((uint32_t)e[9] << 8)
                          | ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        if (!in_xip_range(str_addr, base, region_size)) return false;
        const char *s = (const char *)(uintptr_t)str_addr;
        uint32_t max = (base + region_size) - str_addr;
        if (max > cap - 1u) max = cap - 1u;
        unsigned i = 0;
        while (i < max && s[i] != '\0') { out[i] = s[i]; i++; }
        out[i] = '\0';
        return i > 0;
    }
    return false;
}

typedef struct {
    uint32_t image_base;
    uint32_t payload_size;
    uint32_t first_idx;
} uf2_layout_t;

static bool block_is_program(const uf2_block_t *b)
{
    if (b->magic_start0 != UF2_MAGIC_START0) return false;
    if (b->magic_start1 != UF2_MAGIC_START1) return false;
    if (b->magic_end    != UF2_MAGIC_END)    return false;
    if (b->flags & UF2_FLAG_NOT_MAIN_FLASH)  return false;

    if (b->flags & UF2_FLAG_EXT_TAGS_PRESENT) return false;
    if (b->flags & UF2_FLAG_FAMILY_ID_PRESENT) {
        if (b->file_size_or_family != UF2_FAMILY_RP2350_ARM_S) return false;
    }
    if (b->payload_size == 0 || b->payload_size > UF2_MAX_PAYLOAD) return false;
    return true;
}

static bool read_block_at(uint32_t idx, uf2_block_t *out)
{
    if (!storage_seek(idx * sizeof(uf2_block_t))) return false;
    uint32_t got = 0;
    if (!storage_read(out, sizeof(*out), &got))   return false;
    if (got != sizeof(*out))                      return false;
    if (out->magic_start0 != UF2_MAGIC_START0)    return false;
    if (out->magic_start1 != UF2_MAGIC_START1)    return false;
    if (out->magic_end    != UF2_MAGIC_END)       return false;
    return true;
}

static int addr_to_block_idx(const uf2_layout_t *L, uint32_t image_addr)
{
    if (image_addr < L->image_base) return -1;
    uint32_t off = image_addr - L->image_base;
    return (int)(off / L->payload_size) + (int)L->first_idx;
}

static bool read_image_bytes(const uf2_layout_t *L, uint32_t image_addr,
                             void *dst_v, uint32_t len)
{
    uint8_t *dst = (uint8_t *)dst_v;
    while (len > 0) {
        int bi = addr_to_block_idx(L, image_addr);
        if (bi < 0) return false;
        uf2_block_t blk;
        if (!read_block_at((uint32_t)bi, &blk)) return false;
        if (!block_is_program(&blk))            return false;
        if (image_addr < blk.target_addr)       return false;
        uint32_t in_block = image_addr - blk.target_addr;
        if (in_block >= blk.payload_size)       return false;
        uint32_t avail = blk.payload_size - in_block;
        uint32_t take  = (len < avail) ? len : avail;
        memcpy(dst, blk.data + in_block, take);
        dst        += take;
        image_addr += take;
        len        -= take;
    }
    return true;
}

static bool find_marker(uf2_layout_t *L, uint32_t *out_bi_s, uint32_t *out_bi_e)
{

    const uint32_t SCAN_MAX = 8;
    bool layout_ready = false;
    for (uint32_t idx = 0; idx < SCAN_MAX + 4; idx++) {
        uf2_block_t blk;
        if (!read_block_at(idx, &blk))   continue;
        if (!block_is_program(&blk))     continue;
        if (!layout_ready) {
            L->image_base   = blk.target_addr;
            L->payload_size = blk.payload_size;
            L->first_idx    = idx;
            layout_ready    = true;
        }

        const uint8_t *pl = blk.data;
        if (blk.payload_size < 20) continue;
        for (uint32_t i = 0; i + 20u <= blk.payload_size; i += 4u) {
            uint32_t m0 = (uint32_t)pl[i]   | ((uint32_t)pl[i+1]  << 8)
                       | ((uint32_t)pl[i+2] << 16) | ((uint32_t)pl[i+3] << 24);
            if (m0 != BI_MARKER_START) continue;
            uint32_t m4 = (uint32_t)pl[i+16] | ((uint32_t)pl[i+17] << 8)
                       | ((uint32_t)pl[i+18] << 16) | ((uint32_t)pl[i+19] << 24);
            if (m4 != BI_MARKER_END) continue;
            uint32_t bi_s = (uint32_t)pl[i+4]  | ((uint32_t)pl[i+5]  << 8)
                         | ((uint32_t)pl[i+6]  << 16) | ((uint32_t)pl[i+7]  << 24);
            uint32_t bi_e = (uint32_t)pl[i+8]  | ((uint32_t)pl[i+9]  << 8)
                         | ((uint32_t)pl[i+10] << 16) | ((uint32_t)pl[i+11] << 24);
            if (bi_s < L->image_base || bi_e < bi_s) return false;
            if (((bi_e - bi_s) & 3u) != 0)           return false;
            *out_bi_s = bi_s;
            *out_bi_e = bi_e;
            return true;
        }
        if (idx >= SCAN_MAX) break;
    }
    return false;
}

bool program_name_from_uf2_file(const char *path, char *out, unsigned cap)
{
    if (cap > 0) out[0] = '\0';
    if (cap == 0) return false;
    if (!storage_open(path)) return false;

    uf2_layout_t L = {0};
    uint32_t bi_s = 0, bi_e = 0;
    if (!find_marker(&L, &bi_s, &bi_e)) { storage_close(); return false; }

    const uint32_t MAX_ENTRIES = 256u;
    uint32_t n = (bi_e - bi_s) / 4u;
    if (n > MAX_ENTRIES) n = MAX_ENTRIES;

    for (uint32_t k = 0; k < n; k++) {
        uint32_t entry_addr = 0;
        if (!read_image_bytes(&L, bi_s + k * 4u, &entry_addr, sizeof(entry_addr))) {
            storage_close();
            return false;
        }
        uint8_t e[12];
        if (!read_image_bytes(&L, entry_addr, e, sizeof(e))) continue;
        uint16_t type = (uint16_t)(e[0] | (e[1] << 8));
        uint16_t tag  = (uint16_t)(e[2] | (e[3] << 8));
        if (type != BI_TYPE_ID_AND_STRING || tag != BI_TAG_RP) continue;
        uint32_t id = (uint32_t)e[4] | ((uint32_t)e[5] << 8)
                    | ((uint32_t)e[6] << 16) | ((uint32_t)e[7] << 24);
        if (id != BI_ID_RP_PROGRAM_NAME) continue;
        uint32_t str_addr = (uint32_t)e[8] | ((uint32_t)e[9] << 8)
                          | ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);

        unsigned i = 0;
        while (i < cap - 1u) {
            uint8_t chunk[32];
            uint32_t want = (uint32_t)((cap - 1u - i) < sizeof(chunk)
                                       ? (cap - 1u - i) : sizeof(chunk));
            if (!read_image_bytes(&L, str_addr + i, chunk, want)) break;
            unsigned j = 0;
            while (j < want && chunk[j] != '\0') {
                out[i + j] = (char)chunk[j];
                j++;
            }
            i += j;
            if (j < want) break;
        }
        out[i] = '\0';
        storage_close();
        return i > 0;
    }

    storage_close();
    return false;
}
