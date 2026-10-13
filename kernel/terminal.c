#include "terminal.h"
#include "serial.h"

void terminal_init(void) {
    /* Serial port is already initialized, but ensure clean terminal state */
}

void terminal_putchar(char c) {
    if (c == '\n') {
        serial_putchar('\r');
        serial_putchar('\n');
    } else {
        serial_putchar(c);
    }
}

void terminal_write(const char *str) {
    if (str == NULL) {
        return;
    }
    while (*str) {
        terminal_putchar(*str++);
    }
}

void terminal_writeln(const char *str) {
    terminal_write(str);
    terminal_putchar('\n');
}

void terminal_write_dec(uint64_t val) {
    char buf[21];
    int idx = 0;

    if (val == 0) {
        terminal_putchar('0');
        return;
    }

    while (val > 0) {
        buf[idx++] = (char)('0' + (val % 10));
        val /= 10;
    }

    while (idx > 0) {
        terminal_putchar(buf[--idx]);
    }
}

void terminal_write_hex(uint64_t val) {
    static const char hex_digits[] = "0123456789abcdef";
    terminal_write("0x");
    for (int i = 60; i >= 0; i -= 4) {
        terminal_putchar(hex_digits[(val >> i) & 0xf]);
    }
}

void terminal_clear(void) {
    /* ANSI VT100: Clear screen (\033[2J) and move cursor to row 1, col 1 (\033[H) */
    serial_write("\033[2J\033[H");
}

void terminal_backspace(void) {
    /* Destructive backspace sequence */
    serial_write("\b \b");
}
