#include "serial.h"
#include "io.h"
#include "graphics.h"

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00); /* Disable all interrupts */
    outb(COM1_PORT + 3, 0x80); /* Enable DLAB (set baud rate divisor) */
    outb(COM1_PORT + 0, 0x03); /* Set divisor to 3 (lo byte) 38400 baud */
    outb(COM1_PORT + 1, 0x00); /*                  (hi byte) */
    outb(COM1_PORT + 3, 0x03); /* 8 bits, no parity, one stop bit */
    outb(COM1_PORT + 2, 0xC7); /* Enable FIFO, clear them, with 14-byte threshold */
    outb(COM1_PORT + 4, 0x0B); /* IRQs enabled (OUT2), RTS/DTR set */
    outb(COM1_PORT + 1, 0x01); /* Enable Received Data Available Interrupt (IER bit 0) */
}

void serial_putchar(char c) {
    while ((inb(COM1_PORT + 5) & 0x20) == 0) {
        __asm__ volatile ("pause");
    }
    outb(COM1_PORT, (uint8_t)c);
    /* Mirror every output path, including commands that bypass terminal_write. */
    gterminal_putchar(c);
}

void serial_write(const char *text) {
    if (text == NULL) {
        return;
    }
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

void serial_write_dec(uint64_t value) {
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

int serial_has_rx(void) {
    return (inb(COM1_PORT + 5) & 0x01) != 0;
}

int serial_getchar_nonblock(void) {
    if (serial_has_rx()) {
        return (int)(uint8_t)inb(COM1_PORT);
    }
    return -1;
}

char serial_getchar(void) {
    while (!serial_has_rx()) {
        __asm__ volatile ("pause");
    }
    return (char)inb(COM1_PORT);
}

void serial_enable_rx_interrupt(void) {
    outb(COM1_PORT + 1, 0x01);
}
