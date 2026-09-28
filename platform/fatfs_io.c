
#include "rp_platform.h"
#include "dirent.h"

#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "tf_card.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define RP_MAX_FDS 24
#define RP_FD_BASE 3

static FIL files[RP_MAX_FDS];
static uint8_t used[RP_MAX_FDS];
static FATFS filesystem;

static const char *strip_dotslash(const char *path)
{
    return path && path[0] == '.' && path[1] == '/' ? path + 2 : path;
}

int rp_fatfs_init(const char *asset_dir)
{
    static pico_fatfs_spi_config_t cfg = {
        spi1, CLK_SLOW_DEFAULT, CLK_FAST_DEFAULT_PIO,
        40, 43, 30, 31, true
    };
    if (!pico_fatfs_set_config(&cfg))
        pico_fatfs_config_spi_pio(pio2, pio_claim_unused_sm(pio2, true));
    FRESULT fr = f_mount(&filesystem, "", 1);
    if (fr != FR_OK) { printf("FatFs mount error %d\n", fr); return 0; }
    fr = f_chdir(asset_dir);
    if (fr != FR_OK) { printf("FatFs chdir %s error %d\n", asset_dir, fr); return 0; }
    return 1;
}

static FIL *file_for(int fd)
{
    int slot = fd - RP_FD_BASE;
    return slot >= 0 && slot < RP_MAX_FDS && used[slot] ? &files[slot] : NULL;
}

static BYTE fatfs_mode(int flags)
{
    BYTE mode = (flags & O_RDWR) ? (FA_READ | FA_WRITE) :
                ((flags & O_WRONLY) ? FA_WRITE : FA_READ);
    if (flags & O_CREAT) {
        if (flags & O_EXCL) mode |= FA_CREATE_NEW;
        else if (flags & O_TRUNC) mode |= FA_CREATE_ALWAYS;
        else mode |= FA_OPEN_ALWAYS;
    } else if (flags & O_TRUNC) mode |= FA_CREATE_ALWAYS;
    else mode |= FA_OPEN_EXISTING;
    if (flags & O_APPEND) mode |= FA_OPEN_APPEND;
    return mode;
}

static int file_open(const char *path, int flags)
{
    int slot;
    for (slot = 0; slot < RP_MAX_FDS && used[slot]; ++slot) {}
    if (slot == RP_MAX_FDS) { errno = EMFILE; return -1; }
    FRESULT fr = f_open(&files[slot], strip_dotslash(path), fatfs_mode(flags));
    if (fr != FR_OK) { errno = ENOENT; return -1; }
    used[slot] = 1;
    return slot + RP_FD_BASE;
}

static int file_close(int fd)
{
    FIL *file = file_for(fd);
    if (!file) return -1;
    FRESULT fr = f_close(file);
    used[fd - RP_FD_BASE] = 0;
    return fr == FR_OK ? 0 : -1;
}

static _ssize_t file_read(int fd, void *buffer, size_t length)
{
    FIL *file = file_for(fd); UINT count = 0;
    if (!file || f_read(file, buffer, (UINT)length, &count) != FR_OK) return -1;
    return (_ssize_t)count;
}

static _ssize_t file_write(int fd, const void *buffer, size_t length)
{
    FIL *file = file_for(fd); UINT count = 0;
    if (!file || f_write(file, buffer, (UINT)length, &count) != FR_OK) return -1;
    return (_ssize_t)count;
}

static off_t file_seek(int fd, off_t offset, int whence)
{
    FIL *file = file_for(fd); FSIZE_t pos;
    if (!file) return -1;
    if (whence == SEEK_SET) pos = (FSIZE_t)offset;
    else if (whence == SEEK_CUR) pos = f_tell(file) + offset;
    else if (whence == SEEK_END) pos = f_size(file) + offset;
    else return -1;
    return f_lseek(file, pos) == FR_OK ? (off_t)pos : -1;
}

static int console_write(const char *buffer, int length)
{ return stdio_put_string(buffer, length, false, true); }

int open(const char *path, int flags, ...) { return file_open(path, flags); }
int close(int fd) { return file_close(fd); }
_ssize_t read(int fd, void *buffer, size_t n) { return file_read(fd, buffer, n); }
_ssize_t write(int fd, const void *buffer, size_t n)
{ return fd == 1 || fd == 2 ? console_write((const char *)buffer, (int)n) : file_write(fd, buffer, n); }
off_t lseek(int fd, off_t offset, int whence) { return file_seek(fd, offset, whence); }

int _open(const char *path, int flags, ...) { return file_open(path, flags); }
int _close(int fd) { return file_close(fd); }
int _read(int fd, char *buffer, int length)
{ return fd == 0 ? stdio_get_until(buffer, length, at_the_end_of_time) : (int)file_read(fd, buffer, length); }
int _write(int fd, char *buffer, int length)
{ return fd == 1 || fd == 2 ? console_write(buffer, length) : (int)file_write(fd, buffer, length); }
off_t _lseek(int fd, off_t offset, int whence) { return file_seek(fd, offset, whence); }

int _fstat(int fd, struct stat *st)
{
    if (!st) return -1;
    memset(st, 0, sizeof(*st));
    if (fd >= 0 && fd <= 2) { st->st_mode = S_IFCHR; return 0; }
    FIL *file = file_for(fd);
    if (!file) return -1;
    st->st_mode = S_IFREG;
    st->st_size = (off_t)f_size(file);
    return 0;
}
int _isatty(int fd) { return fd >= 0 && fd <= 2; }

static int path_stat(const char *path, struct stat *st)
{
    FILINFO info;
    if (!st || f_stat(strip_dotslash(path), &info) != FR_OK) { errno = ENOENT; return -1; }
    memset(st, 0, sizeof(*st));
    st->st_mode = (info.fattrib & AM_DIR) ? S_IFDIR : S_IFREG;
    st->st_size = (off_t)info.fsize;
    return 0;
}
int stat(const char *path, struct stat *st) { return path_stat(path, st); }
int _stat(const char *path, struct stat *st) { return path_stat(path, st); }
int access(const char *path, int mode)
{
    FILINFO info;
    if (f_stat(strip_dotslash(path), &info) != FR_OK) { errno = ENOENT; return -1; }
    if ((mode & W_OK) && (info.fattrib & AM_RDO)) { errno = EACCES; return -1; }
    return 0;
}
int mkdir(const char *path, mode_t mode)
{ (void)mode; FRESULT fr = f_mkdir(strip_dotslash(path)); return fr == FR_OK || fr == FR_EXIST ? 0 : -1; }
int unlink(const char *path) { return f_unlink(strip_dotslash(path)) == FR_OK ? 0 : -1; }
int remove(const char *path) { return unlink(path); }
int rename(const char *from, const char *to)
{ return f_rename(strip_dotslash(from), strip_dotslash(to)) == FR_OK ? 0 : -1; }

char *getcwd(char *buffer, size_t size)
{
    if (!buffer || size < 2) { errno = ERANGE; return NULL; }
    buffer[0] = '.';
    buffer[1] = 0;
    return buffer;
}

DIR *opendir(const char *path)
{
    DIR *dir = (DIR *)calloc(1, sizeof(*dir));
    if (!dir) return NULL;
    if (f_opendir(&dir->fatfs, strip_dotslash(path)) != FR_OK) { free(dir); return NULL; }
    return dir;
}

struct dirent *readdir(DIR *dir)
{
    FILINFO info;
    if (!dir || f_readdir(&dir->fatfs, &info) != FR_OK || !info.fname[0]) return NULL;
    strncpy(dir->current.d_name, info.fname, sizeof(dir->current.d_name) - 1);
    dir->current.d_name[sizeof(dir->current.d_name) - 1] = 0;
    dir->current.d_type = (info.fattrib & AM_DIR) ? DT_DIR : DT_REG;
    return &dir->current;
}

int closedir(DIR *dir)
{
    if (!dir) return -1;
    FRESULT fr = f_closedir(&dir->fatfs);
    free(dir);
    return fr == FR_OK ? 0 : -1;
}
