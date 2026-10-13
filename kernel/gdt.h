#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* Standard x86-64 Selectors */
#define GDT_KERNEL_CODE_SEL 0x08
#define GDT_KERNEL_DATA_SEL 0x10
#define GDT_USER_DATA_SEL   0x18
#define GDT_USER_CODE_SEL   0x20
#define GDT_TSS_SEL         0x28

/*
 * 64-bit Task State Segment (TSS) structure
 */
struct __attribute__((packed)) tss64 {
    uint32_t reserved0;
    uint64_t rsp0;        /* Ring 0 stack pointer for privilege transitions */
    uint64_t rsp1;        /* Ring 1 stack pointer */
    uint64_t rsp2;        /* Ring 2 stack pointer */
    uint64_t reserved1;
    uint64_t ist1;        /* Interrupt Stack Table 1 (Double Fault) */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;  /* I/O Map Base Address */
};

/*
 * GDT Pointer for LGDT
 */
struct __attribute__((packed)) gdt_ptr {
    uint16_t limit;
    uint64_t base;
};

/*
 * Initialize and load the x86-64 GDT and TSS.
 */
void gdt_init(void);

/*
 * Update RSP0 in the TSS (used for future multitasking / user-mode transitions).
 */
void gdt_set_kernel_stack(uint64_t stack_top);

#endif /* GDT_H */
