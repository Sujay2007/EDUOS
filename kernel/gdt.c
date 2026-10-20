#include "gdt.h"
#include <stddef.h>

/*
 * GDT Descriptor Formats (x86-64)
 * Standard segment descriptors are 8 bytes.
 * System segment descriptors (like 64-bit TSS) are 16 bytes.
 */

struct __attribute__((packed)) gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
};

struct __attribute__((packed)) tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
};

struct __attribute__((packed)) gdt_table {
    struct gdt_entry null_desc;    /* 0x00 */
    struct gdt_entry kernel_code;  /* 0x08 */
    struct gdt_entry kernel_data;  /* 0x10 */
    struct gdt_entry user_data;    /* 0x18 */
    struct gdt_entry user_code;    /* 0x20 */
    struct tss_descriptor tss_desc;/* 0x28 (16 bytes: 0x28 + 0x30) */
};

static struct gdt_table gdt;
static struct tss64 tss;
static struct gdt_ptr g_gdt_ptr;

/* 4 KiB dedicated stack for Double Faults (IST1) */
static uint8_t double_fault_stack[4096] __attribute__((aligned(16)));

extern void gdt_flush(uint64_t gdt_ptr_addr, uint16_t cs, uint16_t ds, uint16_t tss_sel);

static void set_gdt_entry(struct gdt_entry *entry, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    entry->base_low = (uint16_t)(base & 0xFFFF);
    entry->base_middle = (uint8_t)((base >> 16) & 0xFF);
    entry->base_high = (uint8_t)((base >> 24) & 0xFF);

    entry->limit_low = (uint16_t)(limit & 0xFFFF);
    entry->granularity = (uint8_t)((limit >> 16) & 0x0F);
    entry->granularity |= (gran & 0xF0);

    entry->access = access;
}

static void set_tss_desc(struct tss_descriptor *desc, uint64_t base, uint32_t limit) {
    desc->limit_low = (uint16_t)(limit & 0xFFFF);
    desc->base_low = (uint16_t)(base & 0xFFFF);
    desc->base_middle = (uint8_t)((base >> 16) & 0xFF);
    desc->access = 0x89; /* Present (0x80) | 64-bit TSS Available (0x09) */
    desc->granularity = (uint8_t)((limit >> 16) & 0x0F);
    desc->base_high = (uint8_t)((base >> 24) & 0xFF);
    desc->base_upper = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    desc->reserved = 0;
}

void gdt_set_kernel_stack(uint64_t stack_top) {
    tss.rsp0 = stack_top;
}

uint64_t gdt_get_kernel_stack(void) {
    return tss.rsp0;
}

void gdt_init(void) {
    /* 1. Null descriptor */
    set_gdt_entry(&gdt.null_desc, 0, 0, 0, 0);

    /*
     * 2. Kernel Code 64-bit (Selector 0x08)
     * Access: Present (0x80) | Ring 0 (0x00) | Code/Data (0x10) | Executable (0x08) | Readable (0x02) = 0x9A
     * Granularity: Long Mode 64-bit (0x20) | Granularity 4K (0x80) = 0xA0
     */
    set_gdt_entry(&gdt.kernel_code, 0, 0xFFFFF, 0x9A, 0xA0);

    /*
     * 3. Kernel Data 64-bit (Selector 0x10)
     * Access: Present (0x80) | Ring 0 (0x00) | Code/Data (0x10) | Writable (0x02) = 0x92
     * Granularity: Granularity 4K (0x80) = 0xC0
     */
    set_gdt_entry(&gdt.kernel_data, 0, 0xFFFFF, 0x92, 0xC0);

    /*
     * 4. User Data 64-bit (Selector 0x18, RPL 3 -> 0x1B)
     * Access: Present (0x80) | Ring 3 (0x60) | Code/Data (0x10) | Writable (0x02) = 0xF2
     * Granularity: 0xC0
     */
    set_gdt_entry(&gdt.user_data, 0, 0xFFFFF, 0xF2, 0xC0);

    /*
     * 5. User Code 64-bit (Selector 0x20, RPL 3 -> 0x23)
     * Access: Present (0x80) | Ring 3 (0x60) | Code/Data (0x10) | Executable (0x08) | Readable (0x02) = 0xFA
     * Granularity: Long Mode 64-bit (0x20) = 0xA0
     */
    set_gdt_entry(&gdt.user_code, 0, 0xFFFFF, 0xFA, 0xA0);

    /*
     * 6. TSS Initialization
     */
    for (size_t i = 0; i < sizeof(tss); i++) {
        ((uint8_t *)&tss)[i] = 0;
    }
    /* Setup IST1 for Double Fault handler */
    tss.ist1 = (uint64_t)double_fault_stack + sizeof(double_fault_stack);
    tss.iomap_base = sizeof(tss);

    set_tss_desc(&gdt.tss_desc, (uint64_t)&tss, sizeof(tss) - 1);

    /* Setup GDT pointer */
    g_gdt_ptr.limit = sizeof(struct gdt_table) - 1;
    g_gdt_ptr.base = (uint64_t)&gdt;

    /* Flush and reload CS, DS, ES, SS, FS, GS, and TR */
    gdt_flush((uint64_t)&g_gdt_ptr, GDT_KERNEL_CODE_SEL, GDT_KERNEL_DATA_SEL, GDT_TSS_SEL);
}
