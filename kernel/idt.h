#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#define IDT_ENTRIES 256

/* Gate Types and Attributes */
#define IDT_ATTR_INTERRUPT_GATE 0x8E /* Present, DPL 0, 64-bit Interrupt Gate (interrupts disabled) */
#define IDT_ATTR_TRAP_GATE      0x8F /* Present, DPL 0, 64-bit Trap Gate */
#define IDT_ATTR_USER_GATE      0xEE /* Present, DPL 3, 64-bit Interrupt Gate */

/*
 * 64-bit Interrupt Descriptor Entry
 */
struct __attribute__((packed)) idt_entry {
    uint16_t isr_low;      /* Low 16 bits of ISR address */
    uint16_t kernel_cs;    /* Kernel code segment selector (0x08) */
    uint8_t  ist;          /* Bits 0..2: IST index (0 = none, 1..7 = IST1..7) */
    uint8_t  attributes;   /* Type and attributes (P, DPL, Gate Type) */
    uint16_t isr_mid;      /* Middle 16 bits of ISR address */
    uint32_t isr_high;     /* Upper 32 bits of ISR address */
    uint32_t reserved;     /* Must be zero */
};

/*
 * IDT Pointer for LIDT
 */
struct __attribute__((packed)) idt_ptr {
    uint16_t limit;
    uint64_t base;
};

/*
 * Interrupt Stack Frame passed to C interrupt/exception handlers
 */
struct __attribute__((packed)) interrupt_frame {
    /* Pushed by common assembly stub (registers) */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    /* Pushed by individual ISR stub */
    uint64_t vector;
    uint64_t error_code;

    /* Pushed automatically by CPU on interrupt entry */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

/* Handler function type */
typedef void (*isr_handler_t)(struct interrupt_frame *frame);

/*
 * Initialize and load the IDT.
 */
void idt_init(void);

/*
 * Register an ISR handler for an interrupt vector.
 */
void idt_register_handler(uint8_t vector, isr_handler_t handler);

/*
 * Set an IDT gate entry.
 */
void idt_set_gate(uint8_t vector, void *isr, uint8_t ist, uint8_t attributes);

/*
 * Enable / Disable CPU maskable interrupts.
 */
static inline void interrupts_enable(void) {
    __asm__ volatile ("sti");
}

static inline void interrupts_disable(void) {
    __asm__ volatile ("cli");
}

static inline uint64_t interrupts_save_and_disable(void) {
    uint64_t rflags;
    __asm__ volatile (
        "pushfq\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(rflags)
        :
        : "memory"
    );
    return rflags;
}

static inline void interrupts_restore(uint64_t rflags) {
    __asm__ volatile (
        "push %0\n\t"
        "popfq"
        :
        : "r"(rflags)
        : "memory"
    );
}

#endif /* IDT_H */
