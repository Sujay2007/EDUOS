#include "pit.h"
#include "pic.h"
#include "idt.h"

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND_PORT  0x43
#define PIT_BASE_FREQUENCY 1193182ULL

static volatile uint64_t timer_ticks = 0;
static uint32_t timer_hz = 0;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void pit_irq_handler(struct interrupt_frame *frame) {
    (void)frame;
    timer_ticks++;
    /* Send EOI for IRQ0 */
    pic_send_eoi(0);
}

void pit_init(uint32_t target_hz) {
    if (target_hz == 0) {
        target_hz = PIT_DEFAULT_HZ;
    }
    timer_hz = target_hz;

    uint32_t divisor = (uint32_t)(PIT_BASE_FREQUENCY / target_hz);
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 65535) {
        divisor = 65535;
    }

    /* Register IRQ0 handler in IDT (Vector 32) */
    idt_register_handler(32, pit_irq_handler);

    /*
     * Command Byte 0x36:
     * Channel 0 (00b) | Access mode: lo/hi byte (11b) | Mode 3: Square Wave (011b) | Binary (0b)
     */
    outb(PIT_COMMAND_PORT, 0x36);
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    /* Unmask IRQ0 on PIC */
    pic_clear_mask(0);
}

uint64_t timer_get_ticks(void) {
    return timer_ticks;
}

uint32_t timer_get_frequency(void) {
    return timer_hz;
}
