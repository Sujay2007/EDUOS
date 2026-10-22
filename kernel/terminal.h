#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>
#include <stddef.h>

/*
 * Initialize the terminal abstraction subsystem.
 */
void terminal_init(void);

/*
 * Output a single character with automatic \n -> \r\n translation.
 */
void terminal_putchar(char c);

/*
 * Output a string to the terminal.
 */
void terminal_write(const char *str);

/*
 * Output a string followed by a newline.
 */
void terminal_writeln(const char *str);

/*
 * Output an unsigned 64-bit integer in decimal format.
 */
void terminal_write_dec(uint64_t val);

/*
 * Output a 64-bit unsigned integer in hexadecimal format (0x...).
 */
void terminal_write_hex(uint64_t val);

/*
 * Clear the terminal screen using ANSI escape codes (\033[2J\033[H).
 */
void terminal_clear(void);

/*
 * Visually erase the previous character on the terminal display (\b \b).
 */
void terminal_backspace(void);
unsigned terminal_columns(void);
unsigned terminal_rows(void);
/* Navigation does not enter the shell command buffer or write serial text. */
int terminal_navigation(unsigned char key);
void terminal_follow(void);

#endif /* TERMINAL_H */
