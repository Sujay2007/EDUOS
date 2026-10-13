#include <stdint.h>
#include <stddef.h>
#include <limine.h>

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x03);
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
}

void serial_putchar(char c) {
    while ((inb(0x3FD) & 0x20) == 0) {
    }
    outb(0x3F8, (uint8_t)c);
}

void serial_write(const char *text) {
    while (*text) {
        serial_putchar(*text++);
    }
}

void serial_write_hex64(uint64_t value) {
    static const char hex_digits[] = "0123456789abcdef";
    serial_write("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putchar(hex_digits[(value >> i) & 0xf]);
    }
}

static void serial_write_dec(uint64_t value) {
    char buf[21];
    int idx = 0;

    if (value == 0) {
        serial_putchar('0');
        return;
    }

    while (value > 0) {
        buf[idx++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (idx > 0) {
        serial_putchar(buf[--idx]);
    }
}

static const char *memmap_type_to_string(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE:
            return "Usable";
        case LIMINE_MEMMAP_RESERVED:
            return "Reserved";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
            return "ACPI Reclaimable";
        case LIMINE_MEMMAP_ACPI_NVS:
            return "ACPI NVS";
        case LIMINE_MEMMAP_BAD_MEMORY:
            return "Bad Memory";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
            return "Bootloader Reclaimable";
        case LIMINE_MEMMAP_KERNEL_AND_MODULES:
            return "Kernel/Modules";
        case LIMINE_MEMMAP_FRAMEBUFFER:
            return "Framebuffer";
        default:
            return "Unknown";
    }
}

static void print_memory_map(void) {
    struct limine_memmap_response *response = memmap_request.response;

    if (response == NULL) {
        serial_write("[MEMMAP] Error: No memory map response received from Limine!\r\n");
        return;
    }

    serial_write("\r\nPhysical Memory Map (");
    serial_write_dec(response->entry_count);
    serial_write(" entries):\r\n");
    serial_write("  #   Base Address         Length               Type\r\n");
    serial_write("--------------------------------------------------------------------\r\n");

    for (uint64_t i = 0; i < response->entry_count; i++) {
        struct limine_memmap_entry *entry = response->entries[i];
        if (entry == NULL) {
            continue;
        }

        serial_write(" [");
        if (i < 10) {
            serial_putchar(' ');
        }
        serial_write_dec(i);
        serial_write("] ");

        serial_write_hex64(entry->base);
        serial_write(" - ");
        serial_write_hex64(entry->length);
        serial_write("  ");
        serial_write(memmap_type_to_string(entry->type));
        serial_write("\r\n");
    }
}

#include "pfa.h"

static void print_pfa_stats(const char *label) {
    struct pfa_stats stats;
    pfa_get_stats(&stats);

    serial_write("\r\n--- PFA Statistics (");
    serial_write(label);
    serial_write(") ---\r\n");
    serial_write("  Total addressable pages: ");
    serial_write_dec(stats.total_pages);
    serial_write(" (");
    serial_write_hex64(stats.total_pages * PAGE_SIZE);
    serial_write(")\r\n");

    serial_write("  Usable pages           : ");
    serial_write_dec(stats.usable_pages);
    serial_write(" (");
    serial_write_dec((stats.usable_pages * PAGE_SIZE) / 1024 / 1024);
    serial_write(" MiB)\r\n");

    serial_write("  Bitmap metadata pages  : ");
    serial_write_dec(stats.metadata_pages);
    serial_write(" (");
    serial_write_dec(stats.metadata_pages * 4);
    serial_write(" KiB)\r\n");

    serial_write("  Allocated pages        : ");
    serial_write_dec(stats.allocated_pages);
    serial_write("\r\n");

    serial_write("  Free pages             : ");
    serial_write_dec(stats.free_pages);
    serial_write("\r\n");
}

static void test_pfa(void) {
    serial_write("\r\nInitializing Physical Page Allocator...\r\n");
    int init_res = pfa_init();
    if (init_res != 0) {
        serial_write("[PFA ERROR] Initialization failed with code: ");
        serial_write_dec((uint64_t)-init_res);
        serial_write("\r\n");
        return;
    }
    serial_write("[PFA] Initialization successful!\r\n");

    print_pfa_stats("Initial State");

    /* Allocate 4 pages */
    serial_write("\r\n[PFA Test] Allocating 4 physical pages...\r\n");
    uint64_t p0 = pfa_alloc_page();
    uint64_t p1 = pfa_alloc_page();
    uint64_t p2 = pfa_alloc_page();
    uint64_t p3 = pfa_alloc_page();

    serial_write("  Page 0: "); serial_write_hex64(p0); serial_write("\r\n");
    serial_write("  Page 1: "); serial_write_hex64(p1); serial_write("\r\n");
    serial_write("  Page 2: "); serial_write_hex64(p2); serial_write("\r\n");
    serial_write("  Page 3: "); serial_write_hex64(p3); serial_write("\r\n");

    print_pfa_stats("After 4 Allocations");

    /* Free page 1 and page 2 */
    serial_write("\r\n[PFA Test] Freeing Page 1 (");
    serial_write_hex64(p1);
    serial_write(") and Page 2 (");
    serial_write_hex64(p2);
    serial_write(")...\r\n");

    int f1 = pfa_free_page(p1);
    int f2 = pfa_free_page(p2);
    serial_write("  Free Page 1 result: "); serial_write_dec((uint64_t)f1); serial_write("\r\n");
    serial_write("  Free Page 2 result: "); serial_write_dec((uint64_t)f2); serial_write("\r\n");

    /* Test duplicate free error */
    serial_write("  Testing duplicate free on Page 1: ");
    int f_dup = pfa_free_page(p1);
    if (f_dup != 0) {
        serial_write("Handled safely (code ");
        serial_write_dec((uint64_t)-f_dup);
        serial_write(")\r\n");
    } else {
        serial_write("UNEXPECTED SUCCESS!\r\n");
    }

    /* Test unaligned free error */
    serial_write("  Testing unaligned free (0x1001): ");
    int f_unaligned = pfa_free_page(0x1001);
    if (f_unaligned != 0) {
        serial_write("Handled safely (code ");
        serial_write_dec((uint64_t)-f_unaligned);
        serial_write(")\r\n");
    }

    /* Test reserved address free error */
    serial_write("  Testing reserved memory free (0x0): ");
    int f_res = pfa_free_page(0x0);
    if (f_res != 0) {
        serial_write("Handled safely (code ");
        serial_write_dec((uint64_t)-f_res);
        serial_write(")\r\n");
    }

    print_pfa_stats("After Freeing 2 Pages");

    /* Reallocate: Should immediately reuse the freed pages */
    serial_write("\r\n[PFA Test] Reallocating 2 physical pages...\r\n");
    uint64_t p_re1 = pfa_alloc_page();
    uint64_t p_re2 = pfa_alloc_page();

    serial_write("  Reallocated 1: "); serial_write_hex64(p_re1);
    if (p_re1 == p1 || p_re1 == p2) {
        serial_write(" (matches freed page)\r\n");
    } else {
        serial_write("\r\n");
    }

    serial_write("  Reallocated 2: "); serial_write_hex64(p_re2);
    if (p_re2 == p1 || p_re2 == p2) {
        serial_write(" (matches freed page)\r\n");
    } else {
        serial_write("\r\n");
    }

    print_pfa_stats("After Reallocation");

    /* Clean up all allocated test pages */
    pfa_free_page(p0);
    pfa_free_page(p3);
    pfa_free_page(p_re1);
    pfa_free_page(p_re2);

    print_pfa_stats("After Freeing All Test Pages");
}

void _start(void) {
    serial_init();
    serial_write("\r\nEducational OS kernel booted successfully!\r\n");
    serial_write("Running directly on the emulated x86-64 machine.\r\n");

    print_memory_map();
    test_pfa();

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
