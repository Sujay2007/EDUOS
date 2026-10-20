#include "fs.h"
#include "serial.h"

/* Exercise the production parser through a synthetic block device. No heap use. */
static uint8_t image[8 * BLOCK_SIZE];
static struct filesystem test_fs;
static int failures, reject_reads;
static void check(int ok, const char *label) {
    serial_write(ok ? "  [PASS] " : "  [FAIL] ");
    serial_write(label); serial_write("\r\n");
    if (!ok) failures++;
}
static void put64(unsigned offset, uint64_t n) {
    for (unsigned i = 0; i < 8; i++) image[offset + i] = n >> (i * 8);
}
static int memory_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buf) {
    (void)dev;
    if (reject_reads) return -1;
    uint8_t *out = buf;
    for (size_t i = 0; i < (size_t)count * BLOCK_SIZE; i++) out[i] = image[lba * BLOCK_SIZE + i];
    return 0;
}
static struct block_device memory_disk = {"fixture", 8, 0, memory_read};
static void fixture(void) {
    for (unsigned i = 0; i < sizeof(image); i++) image[i] = 0;
    const char *magic = "EDUFS001", *name = "hello.bin", *empty = "empty.txt";
    for (unsigned i = 0; i < 8; i++) image[i] = magic[i];
    image[8] = 1; image[12] = 2;
    put64(16, 8); put64(24, 1);
    for (unsigned i = 0; name[i]; i++) image[512 + i] = name[i];
    put64(544, 3); put64(552, 700);
    for (unsigned i = 0; empty[i]; i++) image[576 + i] = empty[i];
    for (unsigned i = 0; i < 700; i++) image[1536 + i] = i % 251;
    reject_reads = 0;
}
static int bad(unsigned offset, uint64_t value) {
    fixture(); put64(offset, value);
    return fs_mount(&test_fs, &memory_disk) == FS_CORRUPT && !test_fs.mounted;
}
int fs_test_all(void) {
    failures = 0;
    serial_write("\r\nFilesystem & Storage Test Suite:\r\n");
    fixture();
    check(fs_mount(&test_fs, &memory_disk) == 0, "Valid filesystem mount");
    uint32_t file = 99, empty = 99;
    check(fs_lookup(&test_fs, "hello.bin", &file) == 0 && file == 0 &&
          fs_lookup(&test_fs, "empty.txt", &empty) == 0 && empty == 1 &&
          fs_lookup(&test_fs, "missing", &file) == FS_NOT_FOUND, "Lookup and missing files");
    struct fs_entry entry;
    check(fs_list(&test_fs, 0, &entry) == 0 && entry.size == 700 &&
          fs_list(&test_fs, 1, &entry) == 0 && entry.size == 0 &&
          fs_list(&test_fs, 2, &entry) == FS_NOT_FOUND, "Directory listing and end");
    char long_name[33];
    for (unsigned i = 0; i < 32; i++) long_name[i] = 'a';
    long_name[32] = 0;
    check(fs_lookup(&test_fs, "", &file) == FS_INVALID &&
          fs_lookup(&test_fs, "../hello.bin", &file) == FS_INVALID &&
          fs_lookup(&test_fs, ".", &file) == FS_INVALID &&
          fs_lookup(&test_fs, "..", &file) == FS_INVALID &&
          fs_lookup(&test_fs, "a/b", &file) == FS_INVALID &&
          fs_lookup(&test_fs, "bad name", &file) == FS_INVALID &&
          fs_lookup(&test_fs, long_name, &file) == FS_INVALID &&
          fs_lookup(&test_fs, 0, &file) == FS_INVALID, "Invalid names and length limits");
    uint8_t buf[10]; size_t got = 0;
    for (unsigned i = 0; i < sizeof(buf); i++) buf[i] = 0xcc;
    int ok = fs_read(&test_fs, 0, 510, buf + 1, 4, &got) == 0 && got == 4 &&
             buf[0] == 0xcc && buf[5] == 0xcc;
    for (unsigned i = 0; i < 4; i++) if (buf[i + 1] != (510 + i) % 251) ok = 0;
    check(ok, "Bounded unaligned read across sector boundary and canaries");
    check(fs_read(&test_fs, 0, 698, buf, 10, &got) == 0 && got == 2 &&
          fs_read(&test_fs, 0, 700, buf, 10, &got) == 0 && got == 0 &&
          fs_read(&test_fs, 0, UINT64_MAX, buf, 10, &got) == 0 && got == 0 &&
          fs_read(&test_fs, 1, 0, buf, 10, &got) == 0 && got == 0 &&
          fs_read(&test_fs, 0, 0, 0, 0, &got) == 0 && got == 0,
          "Short reads, EOF, huge offset, empty file and zero capacity");
    check(fs_read(&test_fs, 0, 0, 0, 1, &got) == FS_INVALID &&
          fs_read(&test_fs, 0, 0, buf, 1, 0) == FS_INVALID &&
          fs_read(&test_fs, 99, 0, buf, 1, &got) == FS_NOT_FOUND &&
          block_read(&memory_disk, 8, 1, buf) != 0 &&
          block_read(&memory_disk, UINT64_MAX, 1, buf) != 0 &&
          block_read(&memory_disk, 0, UINT32_MAX, buf) != 0 &&
          block_read(&memory_disk, 0, 1, 0) != 0, "Invalid read arguments and block ranges");
    check(bad(0, 0) && bad(8, 2) && bad(12, 17) && bad(16, 9) &&
          bad(16, 2) && bad(24, 2) && bad(32, 1), "Corrupt superblock rejected");
    check(bad(544, 1) && bad(544, UINT64_MAX) && bad(552, UINT64_MAX) &&
          bad(608, 3) && bad(560, 1), "Invalid extents, empty-file metadata and reserved fields rejected");
    fixture();
    for (unsigned i = 0; i < 32; i++) image[512 + i] = 'x';
    ok = fs_mount(&test_fs, &memory_disk) == FS_CORRUPT;
    fixture(); image[512] = '/';
    ok &= fs_mount(&test_fs, &memory_disk) == FS_CORRUPT;
    fixture(); image[530] = 'x'; /* garbage after terminating NUL */
    ok &= fs_mount(&test_fs, &memory_disk) == FS_CORRUPT;
    fixture();
    for (unsigned i = 0; i < 32; i++) image[576 + i] = image[512 + i];
    ok &= fs_mount(&test_fs, &memory_disk) == FS_CORRUPT;
    fixture(); put64(608, 4); put64(616, 1); /* overlapping data extents */
    ok &= fs_mount(&test_fs, &memory_disk) == FS_CORRUPT;
    check(ok, "Corrupt names, duplicate entries and overlapping extents rejected");
    fixture(); image[12] = 0;
    check(fs_mount(&test_fs, &memory_disk) == 0 &&
          fs_list(&test_fs, 0, &entry) == FS_NOT_FOUND, "Empty directory");
    fixture(); fs_mount(&test_fs, &memory_disk); reject_reads = 1;
    ok = fs_read(&test_fs, 0, 0, buf, 1, &got) == FS_IO && got == 0;
    ok &= fs_mount(&test_fs, &memory_disk) == FS_IO && !test_fs.mounted;
    ok &= fs_read(&test_fs, 0, 0, buf, 1, &got) == FS_UNMOUNTED;
    check(ok, "Device errors propagate and failed mount remains unavailable");
    reject_reads = 0;

    struct filesystem *root = fs_root();
    if (!root->mounted) {
        serial_write("  [SKIP] Persistent-disk checks: no mounted disk.\r\n");
    } else {
        uint8_t sector[BLOCK_SIZE];
        check(block_read(root->device, 0, 1, sector) == 0 && sector[0] == 'E' &&
              block_read(root->device, root->device->sectors, 1, sector) != 0,
              "ATA disk sector read and capacity bounds");
        const char expected[] = "Hello from persistent virtual-disk storage!\n";
        char text[sizeof(expected)];
        ok = fs_lookup(root, "hello.txt", &file) == 0 &&
             fs_read(root, file, 0, text, sizeof(text), &got) == 0 && got == sizeof(expected) - 1;
        if (ok) for (size_t i = 0; i < got; i++) if (text[i] != expected[i]) ok = 0;
        check(ok, "Read real hello.txt contents from persistent disk");
        ok = fs_lookup(root, "pattern.txt", &file) == 0 &&
             fs_read(root, file, 510, buf, 10, &got) == 0 && got == 10;
        if (ok) for (unsigned i = 0; i < 10; i++) if (buf[i] != 'a' + (510 + i) % 26) ok = 0;
        check(ok, "Persistent file read across disk sectors");
        check(fs_lookup(root, "empty.txt", &empty) == 0 &&
              fs_read(root, empty, 0, buf, 10, &got) == 0 && got == 0,
              "Persistent empty file");
    }
    serial_write(failures ? "Filesystem tests FAILED.\r\n" : "Filesystem tests PASSED.\r\n");
    return failures;
}
