#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#define KEYBOARD_BUFFER_SIZE 128

/* Special Keycodes */
#define KEY_NONE        0
#define KEY_BACKSPACE   '\b'
#define KEY_TAB         '\t'
#define KEY_ENTER       '\n'
#define KEY_ESC         27
#define KEY_CAPSLOCK    0x80
#define KEY_LSHIFT      0x81
#define KEY_RSHIFT      0x82
#define KEY_LCTRL       0x83
#define KEY_LALT        0x84
#define KEY_UP          0x85
#define KEY_DOWN        0x86
#define KEY_LEFT        0x87
#define KEY_RIGHT       0x88

struct key_event {
    uint8_t scancode;
    char ascii;
    uint8_t pressed; /* 1 = pressed (make), 0 = released (break) */
    uint8_t shift;
    uint8_t caps;
    uint8_t ctrl;
    uint8_t alt;
};

/*
 * Initialize the PS/2 keyboard controller, register IRQ1 handler, and clear buffers.
 */
int keyboard_init(void);

/*
 * Poll for an available ASCII character (from PS/2 keyboard or serial input).
 * Returns -1 if no character is available.
 */
int keyboard_getchar_nonblock(void);

/*
 * Wait for and return the next ASCII character.
 */
char keyboard_getchar(void);

/*
 * Check if the keyboard buffer contains any pending characters.
 */
int keyboard_has_char(void);

#endif /* KEYBOARD_H */
