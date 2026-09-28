
#ifndef LGAMES_PORT_DIRENT_H
#define LGAMES_PORT_DIRENT_H

#define DIR FATFS_DIR
#include "ff.h"
#undef DIR

#ifdef __cplusplus
extern "C" {
#endif

struct dirent {
    char d_name[FF_MAX_LFN + 1];
    unsigned char d_type;
};

typedef struct RP_DIR {
    FATFS_DIR fatfs;
    struct dirent current;
} DIR;

#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

#ifdef __cplusplus
}
#endif
#endif
