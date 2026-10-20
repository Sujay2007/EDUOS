#include "block.h"
#include "io.h"
#include "idt.h"

#define ATA_BASE 0x1f0
#define ATA_CTRL 0x3f6
#define ATA_POLL_LIMIT 1000000U
static struct block_device disk;

static uint16_t read_word(void) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"((uint16_t)ATA_BASE));
    return value;
}
static void settle(void) {
    for (int i = 0; i < 4; i++) (void)inb(ATA_CTRL);
}
static int wait_ready(int data) {
    for (unsigned i = 0; i < ATA_POLL_LIMIT; i++) {
        uint8_t status = inb(ATA_BASE + 7);
        if (status == 0 || status == 0xff) return -1;
        if (!(status & 0x80)) {
            if (status & 0x21) return -1; /* ERR or device fault */
            if (!data || (status & 8)) return 0;
        }
        __asm__ volatile ("pause");
    }
    return -1;
}
static int ata_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buf) {
    (void)dev;
    if (lba >= (1ULL << 28) || count > (1ULL << 28) - lba) return -1;
    uint8_t *out = buf;
    /* Serialize the PIO command across timer preemption on this single CPU. */
    uint64_t flags = interrupts_save_and_disable();
    int result = 0;
    for (uint32_t n = 0; n < count; n++, lba++) {
        outb(ATA_BASE + 6, 0xe0 | ((lba >> 24) & 15));
        settle();
        if (wait_ready(0)) { result = -1; break; }
        outb(ATA_BASE + 2, 1);
        outb(ATA_BASE + 3, lba & 255);
        outb(ATA_BASE + 4, (lba >> 8) & 255);
        outb(ATA_BASE + 5, (lba >> 16) & 255);
        outb(ATA_BASE + 7, 0x20); /* READ SECTORS */
        settle();
        if (wait_ready(1)) { result = -1; break; }
        for (unsigned w = 0; w < 256; w++) {
            uint16_t word = read_word();
            *out++ = word & 255;
            *out++ = word >> 8;
        }
        settle();
        if (wait_ready(0)) { result = -1; break; }
    }
    interrupts_restore(flags);
    return result;
}
struct block_device *ata_init(void) {
    uint64_t flags = interrupts_save_and_disable();
    uint16_t identify[256];
    disk.sectors = 0;
    outb(ATA_CTRL, 2); /* nIEN: polling only, no ATA IRQ handler required */
    outb(ATA_BASE + 6, 0xa0);
    settle();
    if (wait_ready(0)) goto absent;
    outb(ATA_BASE + 2, 0);
    outb(ATA_BASE + 3, 0);
    outb(ATA_BASE + 4, 0);
    outb(ATA_BASE + 5, 0);
    outb(ATA_BASE + 7, 0xec); /* IDENTIFY DEVICE */
    settle();
    if (wait_ready(1) || inb(ATA_BASE + 4) || inb(ATA_BASE + 5)) goto absent;
    for (unsigned i = 0; i < 256; i++) identify[i] = read_word();
    if (!(identify[49] & (1U << 9))) goto absent;
    /* Reject non-512-byte logical sectors rather than misinterpreting data. */
    if ((identify[106] & 0xc000) == 0x4000 && (identify[106] & 0x1000) &&
        (identify[117] != 256 || identify[118] != 0)) goto absent;
    disk.sectors = (uint64_t)identify[60] | ((uint64_t)identify[61] << 16);
    if (disk.sectors > (1ULL << 28)) disk.sectors = 1ULL << 28;
    if (!disk.sectors) goto absent;
    disk.name = "ata0 (PIO/LBA28)";
    disk.read_sectors = ata_read;
    disk.context = 0;
    interrupts_restore(flags);
    return &disk;
absent:
    interrupts_restore(flags);
    return 0;
}
