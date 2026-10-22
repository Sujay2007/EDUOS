#include "terminal.h"
#include "serial.h"
#include "graphics.h"
#include "keyboard.h"

unsigned terminal_columns(void) { return gterminal_columns(); }
unsigned terminal_rows(void) { return gterminal_rows(); }
void terminal_follow(void) { gterminal_navigate(GT_FOLLOW); }
int terminal_navigation(unsigned char key) {
    enum gterminal_navigation action;
    switch (key) {
        case KEY_PAGE_UP: action = GT_PAGE_UP; break;
        case KEY_PAGE_DOWN: action = GT_PAGE_DOWN; break;
        case KEY_UP: action = GT_UP; break;
        case KEY_DOWN: action = GT_DOWN; break;
        case KEY_HOME: action = GT_HOME; break;
        case KEY_END: action = GT_FOLLOW; break;
        default: return 0;
    }
    gterminal_navigate(action);
    return 1;
}

void terminal_init(void) {
    gterminal_init();
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
