#ifndef FS_H
#define FS_H
#include "block.h"

#define FS_MAX_FILES 16U
#define FS_NAME_BYTES 32U
enum { FS_OK = 0, FS_INVALID = -1, FS_IO = -2, FS_CORRUPT = -3,
       FS_NOT_FOUND = -4, FS_UNMOUNTED = -5 };
struct fs_entry { char name[FS_NAME_BYTES]; uint64_t start_sector, size; };
/* Mount owns a validated directory snapshot, but not the block device. */
struct filesystem {
    struct block_device *device;
    uint64_t sectors;
    uint32_t count;
    int mounted;
    struct fs_entry entries[FS_MAX_FILES];
};
int fs_mount(struct filesystem *fs, struct block_device *device);
/* Flat root only: no slash, dot components, or implicit path traversal. */
int fs_lookup(const struct filesystem *fs, const char *name, uint32_t *index);
/* Returns FS_NOT_FOUND at end of directory. */
int fs_list(const struct filesystem *fs, uint32_t index, struct fs_entry *out);
/* Returns bytes read via actual; offset >= size is successful EOF. */
int fs_read(const struct filesystem *fs, uint32_t index, uint64_t offset,
            void *buf, size_t capacity, size_t *actual);
struct filesystem *fs_root(void);
void storage_init(void);
int fs_test_all(void);
#endif
