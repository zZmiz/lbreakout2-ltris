

#ifndef UF2_FORMAT_H
#define UF2_FORMAT_H

#include <stdint.h>

#define UF2_MAGIC_START0 0x0A324655u
#define UF2_MAGIC_START1 0x9E5D5157u
#define UF2_MAGIC_END    0x0AB16F30u

#define UF2_FLAG_NOT_MAIN_FLASH    0x00000001u
#define UF2_FLAG_FILE_CONTAINER    0x00001000u
#define UF2_FLAG_FAMILY_ID_PRESENT 0x00002000u
#define UF2_FLAG_MD5_PRESENT       0x00004000u
#define UF2_FLAG_EXT_TAGS_PRESENT  0x00008000u

#define UF2_FAMILY_RP2040        0xe48bff56u
#define UF2_FAMILY_RP2XXX_ABS    0xe48bff57u
#define UF2_FAMILY_RP2350_DATA   0xe48bff58u
#define UF2_FAMILY_RP2350_ARM_S  0xe48bff59u
#define UF2_FAMILY_RP2350_RISCV  0xe48bff5au
#define UF2_FAMILY_RP2350_ARM_NS 0xe48bff5bu

#define UF2_BLOCK_SIZE     512u
#define UF2_MAX_PAYLOAD    476u

typedef struct __attribute__((packed)) {
    uint32_t magic_start0;
    uint32_t magic_start1;
    uint32_t flags;
    uint32_t target_addr;
    uint32_t payload_size;
    uint32_t block_no;
    uint32_t num_blocks;
    uint32_t file_size_or_family;
    uint8_t  data[UF2_MAX_PAYLOAD];
    uint32_t magic_end;
} uf2_block_t;

#ifdef __cplusplus
static_assert(sizeof(uf2_block_t) == UF2_BLOCK_SIZE, "UF2 block must be 512 bytes");
#else
_Static_assert(sizeof(uf2_block_t) == UF2_BLOCK_SIZE, "UF2 block must be 512 bytes");
#endif

typedef enum {
    UF2_CLS_PROGRAM,
    UF2_CLS_SKIP,
    UF2_CLS_OUT_OF_RANGE,
    UF2_CLS_BAD_MAGIC,
    UF2_CLS_BAD_PAYLOAD
} uf2_class_t;

static inline uf2_class_t uf2_classify_block(const uf2_block_t *b,
                                             uint32_t expected_family,
                                             uint32_t region_base,
                                             uint32_t region_end,
                                             uint32_t xip_base,
                                             uint32_t *out_flash_offset,
                                             uint32_t *out_len)
{
    if (b->magic_start0 != UF2_MAGIC_START0 ||
        b->magic_start1 != UF2_MAGIC_START1 ||
        b->magic_end    != UF2_MAGIC_END) {
        return UF2_CLS_BAD_MAGIC;
    }
    if (b->payload_size == 0 || b->payload_size > UF2_MAX_PAYLOAD) {
        return UF2_CLS_BAD_PAYLOAD;
    }
    if (b->flags & UF2_FLAG_NOT_MAIN_FLASH) {
        return UF2_CLS_SKIP;
    }
    if (b->flags & UF2_FLAG_FAMILY_ID_PRESENT) {
        if (b->file_size_or_family != expected_family) {
            return UF2_CLS_SKIP;
        }
    }

    uint64_t start = b->target_addr;
    uint64_t end   = start + b->payload_size;
    if (start < region_base || end > region_end || end < start) {
        return UF2_CLS_OUT_OF_RANGE;
    }
    if (out_flash_offset) *out_flash_offset = (uint32_t)(b->target_addr - xip_base);
    if (out_len)          *out_len          = b->payload_size;
    return UF2_CLS_PROGRAM;
}

#endif
