#ifndef PIC_H
#define PIC_H

#include <stdint.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define PIC_EOI      0x20

/*
 * Remap legacy PIC IRQ 0..15 to IDT vectors offset1 (32) and offset2 (40).
 */
void pic_remap(uint8_t offset1, uint8_t offset2);

/*
 * Send End of Interrupt (EOI) for an IRQ (0..15).
 */
void pic_send_eoi(uint8_t irq);

/*
 * Mask (disable) or unmask (enable) a specific IRQ line on the 8259 PIC.
 */
void pic_set_mask(uint8_t irq);
void pic_clear_mask(uint8_t irq);

/*
 * Disable the 8259 PIC entirely by masking all 16 lines.
 */
void pic_disable(void);

#endif /* PIC_H */
