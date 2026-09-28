

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#define STORAGE_MAX_NAME 256

typedef struct {
    char     name[STORAGE_MAX_NAME];
    uint32_t size;
} storage_entry_t;

bool storage_mount(void);
void storage_unmount(void);

bool storage_list_uf2(storage_entry_t *out, int max, int *count);

bool storage_open(const char *name);

bool storage_read(void *buf, uint32_t len, uint32_t *read);

bool storage_rewind(void);

bool storage_seek(uint32_t offset);

uint32_t storage_size(void);
void storage_close(void);

#endif
