

#ifndef PROGRAM_NAME_H
#define PROGRAM_NAME_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool program_name_from_xip(uint32_t base, uint32_t region_size,
                           char *out, unsigned cap);

bool program_name_from_uf2_file(const char *path, char *out, unsigned cap);

#ifdef __cplusplus
}
#endif

#endif
