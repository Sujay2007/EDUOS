#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stddef.h>

#define COM1_PORT 0x3F8

/*
 * Initialize the COM1 UART serial port (38400 baud, 8N1).
 */
void serial_init(void);

/*
 * Output a single character over COM1.
 */
void serial_putchar(char c);

/*
 * Output a null-terminated string over COM1.
 */
void serial_write(const char *text);

/*
 * Output a 64-bit hexadecimal value with 0x prefix.
 */
void serial_write_hex64(uint64_t value);

/*
 * Output an unsigned 64-bit integer in decimal format.
 */
void serial_write_dec(uint64_t value);

/*
 * Check if data is available to read on COM1.
 * Returns 1 if data is available, 0 otherwise.
 */
int serial_has_rx(void);

/*
 * Read a single character from COM1 if available (non-blocking).
 * Returns character cast to int, or -1 if no data is available.
 */
int serial_getchar_nonblock(void);

/*
 * Read a character from COM1 (blocking).
 */
char serial_getchar(void);

#endif /* SERIAL_H */
