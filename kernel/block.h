#ifndef BLOCK_H
#define BLOCK_H
#include <stdint.h>
#include <stddef.h>

#define BLOCK_SIZE 512U
/* Read-only sector device. Callers own buffers; no allocation or writes. */
struct block_device {
    const char *name;
    uint64_t sectors;
    void *context;
    int (*read_sectors)(struct block_device *, uint64_t, uint32_t, void *);
};
int block_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buf);
/* Primary-channel master, legacy I/O ATA, LBA28, 512-byte sectors only. */
struct block_device *ata_init(void);
#endif
