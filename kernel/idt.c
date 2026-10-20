#include "idt.h"
#include "gdt.h"
#include "pic.h"
#include "serial.h"
#include "thread.h"
#include "process.h"
#include <stddef.h>

extern void serial_write(const char *text);
extern void serial_write_hex64(uint64_t value);

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr g_idt_ptr;
static isr_handler_t isr_handlers[IDT_ENTRIES];

/* External assembly ISR stub declarations */
extern void isr_stub_0(void);
extern void isr_stub_1(void);
extern void isr_stub_2(void);
extern void isr_stub_3(void);
extern void isr_stub_4(void);
extern void isr_stub_5(void);
extern void isr_stub_6(void);
extern void isr_stub_7(void);
extern void isr_stub_8(void);
extern void isr_stub_9(void);
extern void isr_stub_10(void);
extern void isr_stub_11(void);
extern void isr_stub_12(void);
extern void isr_stub_13(void);
extern void isr_stub_14(void);
extern void isr_stub_15(void);
extern void isr_stub_16(void);
extern void isr_stub_17(void);
extern void isr_stub_18(void);
extern void isr_stub_19(void);
extern void isr_stub_20(void);
extern void isr_stub_21(void);
extern void isr_stub_22(void);
extern void isr_stub_23(void);
extern void isr_stub_24(void);
extern void isr_stub_25(void);
extern void isr_stub_26(void);
extern void isr_stub_27(void);
extern void isr_stub_28(void);
extern void isr_stub_29(void);
extern void isr_stub_30(void);
extern void isr_stub_31(void);

extern void isr_stub_32(void);
extern void isr_stub_33(void);
extern void isr_stub_34(void);
extern void isr_stub_35(void);
extern void isr_stub_36(void);
extern void isr_stub_37(void);
extern void isr_stub_38(void);
extern void isr_stub_39(void);
extern void isr_stub_40(void);
extern void isr_stub_41(void);
extern void isr_stub_42(void);
extern void isr_stub_43(void);
extern void isr_stub_44(void);
extern void isr_stub_45(void);
extern void isr_stub_46(void);
extern void isr_stub_47(void);
extern void isr_stub_48(void);
extern void isr_stub_128(void);

extern void user_fault_handler(struct interrupt_frame *frame);

static const char *exception_messages[32] = {
    "Divide-by-zero Error",
    "Debug",
    "Non-maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

void idt_set_gate(uint8_t vector, void *isr, uint8_t ist, uint8_t attributes) {
    uint64_t addr = (uint64_t)isr;
    idt[vector].isr_low = (uint16_t)(addr & 0xFFFF);
    idt[vector].kernel_cs = GDT_KERNEL_CODE_SEL;
    idt[vector].ist = ist & 0x07;
    idt[vector].attributes = attributes;
    idt[vector].isr_mid = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vector].isr_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vector].reserved = 0;
}

void idt_register_handler(uint8_t vector, isr_handler_t handler) {
    isr_handlers[vector] = handler;
}

static inline uint64_t read_cr2(void) {
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

extern struct interrupt_frame *scheduler_on_interrupt(struct interrupt_frame *frame);

struct interrupt_frame *isr_dispatch(struct interrupt_frame *frame) {
    uint64_t vec = frame->vector;

    if (vec < IDT_ENTRIES && isr_handlers[vec] != NULL) {
        isr_handlers[vec](frame);
    } else if (vec < 32) {
        if ((frame->cs & 3) == 3) {
            struct thread *curr = thread_get_current();
            if (curr != NULL && curr->process != NULL && curr->process->pid != 0) {
                uint64_t cr2 = (vec == 14) ? read_cr2() : 0;
                process_record_fault(curr->process->pid, vec, frame->rip, cr2, frame->error_code);
                serial_write("  [PROCESS FAULT] PID ");
                serial_write_dec(curr->process->pid);
                serial_write(" ('");
                serial_write(curr->process->name);
                serial_write("') faulted: Vector ");
                serial_write_dec(vec);
                serial_write(" (");
                serial_write(exception_messages[vec]);
                serial_write(") at RIP ");
                serial_write_hex64(frame->rip);
                if (vec == 14) {
                    serial_write(" CR2 ");
                    serial_write_hex64(cr2);
                }
                serial_write(" ErrorCode ");
                serial_write_hex64(frame->error_code);
                serial_write(" -> Terminating Process\r\n");
                process_exit(curr->process, -(int)(vec + 100));
                return frame;
            }

            /* Handled single-shot user-mode exception cleanly without crashing kernel */
            user_fault_handler(frame);
            return frame;
        }

        /* If unhandled CPU exception (0..31), print diagnostic and halt safely */
        serial_write("\r\n====================================================================\r\n");
        serial_write("!!! CPU EXCEPTION OCCURRED !!!\r\n");
        serial_write("  Vector    : ");
        serial_write_hex64(vec);
        serial_write(" (");
        serial_write(exception_messages[vec]);
        serial_write(")\r\n");

        serial_write("  RIP       : ");
        serial_write_hex64(frame->rip);
        serial_write("\r\n  CS        : ");
        serial_write_hex64(frame->cs);
        serial_write("\r\n  RFLAGS    : ");
        serial_write_hex64(frame->rflags);
        serial_write("\r\n  RSP       : ");
        serial_write_hex64(frame->rsp);
        serial_write("\r\n  Error Code: ");
        serial_write_hex64(frame->error_code);
        serial_write("\r\n");

        if (vec == 14) { /* Page Fault */
            uint64_t cr2 = read_cr2();
            serial_write("  CR2 (Fault Address): ");
            serial_write_hex64(cr2);
            serial_write("\r\n");
        }

        serial_write("System halted safely.\r\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    } else if (vec >= 32 && vec <= 47) {
        /* Unhandled hardware interrupt: send EOI to prevent cascade lockup */
        pic_send_eoi((uint8_t)(vec - 32));
    }

    /* Notify scheduler for preemption, sleep check, or voluntary yield */
    return scheduler_on_interrupt(frame);
}

void idt_init(void) {
    /* Clear IDT entries and handlers */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt[i].isr_low = 0;
        idt[i].kernel_cs = 0;
        idt[i].ist = 0;
        idt[i].attributes = 0;
        idt[i].isr_mid = 0;
        idt[i].isr_high = 0;
        idt[i].reserved = 0;
        isr_handlers[i] = NULL;
    }

    /* Install CPU exception gates (0..31) */
    void *exception_stubs[32] = {
        isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3,
        isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7,
        isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11,
        isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15,
        isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19,
        isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
        isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27,
        isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
    };

    for (int i = 0; i < 32; i++) {
        /* Vector 8 (Double Fault) uses IST1 configured in TSS */
        uint8_t ist = (i == 8) ? 1 : 0;
        idt_set_gate((uint8_t)i, exception_stubs[i], ist, IDT_ATTR_INTERRUPT_GATE);
    }

    /* Install IRQ gates (32..47) */
    void *irq_stubs[16] = {
        isr_stub_32, isr_stub_33, isr_stub_34, isr_stub_35,
        isr_stub_36, isr_stub_37, isr_stub_38, isr_stub_39,
        isr_stub_40, isr_stub_41, isr_stub_42, isr_stub_43,
        isr_stub_44, isr_stub_45, isr_stub_46, isr_stub_47
    };

    for (int i = 0; i < 16; i++) {
        idt_set_gate((uint8_t)(32 + i), irq_stubs[i], 0, IDT_ATTR_INTERRUPT_GATE);
    }

    /* Install Software Yield / Scheduling Gate (Vector 48) */
    idt_set_gate(48, isr_stub_48, 0, IDT_ATTR_INTERRUPT_GATE);

    /* Install System Call Gate (Vector 128 / 0x80) with DPL 3 */
    idt_set_gate(SYSCALL_VECTOR, isr_stub_128, 0, IDT_ATTR_USER_GATE);

    /* Remap PIC to vector 32 (IRQ 0..7) and 40 (IRQ 8..15), and mask all IRQs initially */
    pic_remap(32, 40);
    pic_disable();

    /* Load IDT */
    g_idt_ptr.limit = sizeof(idt) - 1;
    g_idt_ptr.base = (uint64_t)&idt;

    __asm__ volatile ("lidt %0" : : "m"(g_idt_ptr));
}
