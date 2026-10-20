#include "block.h"

int block_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buf) {
    if (!dev || !dev->read_sectors || lba > dev->sectors ||
        (uint64_t)count > dev->sectors - lba ||
        (uint64_t)count > SIZE_MAX / BLOCK_SIZE || (count && !buf)) return -1;
    if (!count) return 0;
    return dev->read_sectors(dev, lba, count, buf);
}
