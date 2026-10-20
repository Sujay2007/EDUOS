#include "fs.h"
#include "serial.h"

static struct filesystem root;
static uint64_t le64(const uint8_t *p) {
    uint64_t n = 0;
    for (unsigned i = 0; i < 8; i++) n |= (uint64_t)p[i] << (8 * i);
    return n;
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int same(const char *a, const char *b) {
    for (unsigned i = 0; i < FS_NAME_BYTES; i++) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
    }
    return 0;
}
static int name_valid(const char *name) {
    if (!name || !name[0]) return 0;
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]))) return 0;
    for (unsigned i = 0; i < FS_NAME_BYTES; i++) {
        unsigned char c = name[i];
        if (!c) return 1;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) return 0;
    }
    return 0;
}
int fs_mount(struct filesystem *fs, struct block_device *device) {
    if (!fs) return FS_INVALID;
    fs->mounted = 0;
    fs->device = 0;
    fs->count = 0;
    fs->sectors = 0;
    if (!device || device->sectors < 3) return FS_INVALID;
    uint8_t sector[BLOCK_SIZE];
    if (block_read(device, 0, 1, sector)) return FS_IO;
    const char magic[8] = {'E','D','U','F','S','0','0','1'};
    for (unsigned i = 0; i < 8; i++) if (sector[i] != magic[i]) return FS_CORRUPT;
    uint32_t count = le32(sector + 12);
    uint64_t total = le64(sector + 16);
    if (le32(sector + 8) != 1 || count > FS_MAX_FILES ||
        total < 3 || total > device->sectors || le64(sector + 24) != 1) return FS_CORRUPT;
    for (unsigned i = 32; i < BLOCK_SIZE; i++) if (sector[i]) return FS_CORRUPT;
    for (uint32_t i = 0; i < count; i++) {
        if (i % 8 == 0 && block_read(device, 1 + i / 8, 1, sector)) return FS_IO;
        const uint8_t *raw = sector + (i % 8) * 64;
        struct fs_entry *e = &fs->entries[i];
        for (unsigned j = 0; j < FS_NAME_BYTES; j++) e->name[j] = raw[j];
        if (!name_valid(e->name)) return FS_CORRUPT;
        int ended = 0;
        for (unsigned j = 0; j < FS_NAME_BYTES; j++) {
            if (!raw[j]) ended = 1;
            else if (ended) return FS_CORRUPT;
        }
        for (unsigned j = 48; j < 64; j++) if (raw[j]) return FS_CORRUPT;
        e->start_sector = le64(raw + 32);
        e->size = le64(raw + 40);
        uint64_t extent = e->size / BLOCK_SIZE + (e->size % BLOCK_SIZE != 0);
        if (!e->size) {
            if (e->start_sector) return FS_CORRUPT;
        } else if (e->start_sector < 3 || e->start_sector >= total ||
                   extent > total - e->start_sector) return FS_CORRUPT;
        for (uint32_t j = 0; j < i; j++) {
            const struct fs_entry *prev = &fs->entries[j];
            if (same(e->name, prev->name)) return FS_CORRUPT;
            uint64_t prev_extent = prev->size / BLOCK_SIZE + (prev->size % BLOCK_SIZE != 0);
            if (extent && prev_extent && e->start_sector < prev->start_sector + prev_extent &&
                prev->start_sector < e->start_sector + extent) return FS_CORRUPT;
        }
    }
    fs->device = device;
    fs->count = count;
    fs->sectors = total;
    fs->mounted = 1;
    return FS_OK;
}
int fs_lookup(const struct filesystem *fs, const char *name, uint32_t *index) {
    if (!index || !name_valid(name)) return FS_INVALID;
    if (!fs || !fs->mounted) return FS_UNMOUNTED;
    for (uint32_t i = 0; i < fs->count; i++) {
        if (same(name, fs->entries[i].name)) { *index = i; return FS_OK; }
    }
    return FS_NOT_FOUND;
}
int fs_list(const struct filesystem *fs, uint32_t index, struct fs_entry *out) {
    if (!out) return FS_INVALID;
    if (!fs || !fs->mounted) return FS_UNMOUNTED;
    if (index >= fs->count) return FS_NOT_FOUND;
    *out = fs->entries[index];
    return FS_OK;
}
int fs_read(const struct filesystem *fs, uint32_t index, uint64_t offset,
            void *buf, size_t capacity, size_t *actual) {
    if (!actual) return FS_INVALID;
    *actual = 0;
    if (capacity && !buf) return FS_INVALID;
    if (!fs || !fs->mounted) return FS_UNMOUNTED;
    if (index >= fs->count) return FS_NOT_FOUND;
    const struct fs_entry *e = &fs->entries[index];
    if (offset >= e->size || !capacity) return FS_OK;
    uint64_t left = e->size - offset;
    if (left > capacity) left = capacity;
    uint8_t sector[BLOCK_SIZE], *out = buf;
    while (left) {
        uint64_t lba = e->start_sector + offset / BLOCK_SIZE;
        if (block_read(fs->device, lba, 1, sector)) return FS_IO;
        size_t skip = offset % BLOCK_SIZE;
        size_t n = BLOCK_SIZE - skip;
        if (n > left) n = (size_t)left;
        for (size_t i = 0; i < n; i++) out[*actual + i] = sector[skip + i];
        *actual += n;
        offset += n;
        left -= n;
    }
    return FS_OK;
}
struct filesystem *fs_root(void) { return &root; }
void storage_init(void) {
    struct block_device *dev = ata_init();
    if (!dev) { serial_write("[FS] No supported ATA disk; filesystem unavailable.\r\n"); return; }
    if (fs_mount(&root, dev)) { serial_write("[FS] EDUFS mount rejected (I/O or corrupt metadata).\r\n"); return; }
    serial_write("[FS] Mounted EDUFS v1 read-only on ");
    serial_write(dev->name);
    serial_write("; files="); serial_write_dec(root.count); serial_write("\r\n");
}
