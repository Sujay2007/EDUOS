#include "keyboard.h"
#include "io.h"
#include "pic.h"
#include "idt.h"
#include "serial.h"

#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64

/* Scancode set 1 translation tables */
static const char scancode_ascii_unshifted[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static const char scancode_ascii_shifted[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

/* Keyboard modifier states */
static volatile uint8_t shift_pressed = 0;
static volatile uint8_t caps_lock_active = 0;
static volatile uint8_t ctrl_pressed = 0;
static volatile uint8_t alt_pressed = 0;

/* Circular ring buffer for received ASCII characters */
static volatile char kbd_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;

static void keyboard_isr(struct interrupt_frame *frame) {
    (void)frame;

    uint8_t scancode = inb(KBD_DATA_PORT);
    pic_send_eoi(1);

    uint8_t is_break = (scancode & 0x80) != 0;
    uint8_t make_code = scancode & 0x7F;

    if (make_code == 0x2A || make_code == 0x36) {
        /* Left Shift (0x2A) or Right Shift (0x36) */
        shift_pressed = !is_break;
        return;
    }

    if (make_code == 0x1D) {
        /* Control */
        ctrl_pressed = !is_break;
        return;
    }

    if (make_code == 0x38) {
        /* Alt */
        alt_pressed = !is_break;
        return;
    }

    if (make_code == 0x3A) {
        /* Caps Lock toggles on press only */
        if (!is_break) {
            caps_lock_active = !caps_lock_active;
        }
        return;
    }

    if (!is_break && make_code < 128) {
        char ch = 0;
        char unshifted = scancode_ascii_unshifted[make_code];
        char shifted = scancode_ascii_shifted[make_code];

        if (unshifted >= 'a' && unshifted <= 'z') {
            /* For letters, Shift and Caps Lock interact */
            int uppercase = (shift_pressed ^ caps_lock_active);
            ch = uppercase ? shifted : unshifted;
        } else {
            /* For numbers and symbols, only Shift applies */
            ch = shift_pressed ? shifted : unshifted;
        }

        if (ch != 0) {
            uint32_t next_head = (kbd_head + 1) % KEYBOARD_BUFFER_SIZE;
            if (next_head != kbd_tail) {
                kbd_buffer[kbd_head] = ch;
                kbd_head = next_head;
            }
        }
    }
}

int keyboard_init(void) {
    /* Drain any pending bytes from keyboard data port */
    while (inb(KBD_STATUS_PORT) & 0x01) {
        inb(KBD_DATA_PORT);
    }

    /* Reset buffer indices and state */
    kbd_head = 0;
    kbd_tail = 0;
    shift_pressed = 0;
    caps_lock_active = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;

    /* Register ISR for IRQ1 (vector 33) */
    idt_register_handler(33, keyboard_isr);

    /* Unmask IRQ1 on Master PIC */
    pic_clear_mask(1);

    return 0;
}

int keyboard_getchar_nonblock(void) {
    uint64_t rflags = interrupts_save_and_disable();

    /* 1. Check PS/2 keyboard ring buffer */
    if (kbd_head != kbd_tail) {
        char ch = kbd_buffer[kbd_tail];
        kbd_tail = (kbd_tail + 1) % KEYBOARD_BUFFER_SIZE;
        interrupts_restore(rflags);
        return (int)(uint8_t)ch;
    }

    interrupts_restore(rflags);

    /* 2. Check COM1 serial input */
    int ser_ch = serial_getchar_nonblock();
    if (ser_ch >= 0) {
        /* Normalize CR/LF and Backspace */
        if (ser_ch == '\r') {
            return '\n';
        }
        if (ser_ch == 127) {
            return '\b';
        }
        return ser_ch;
    }

    return -1;
}

char keyboard_getchar(void) {
    for (;;) {
        int c = keyboard_getchar_nonblock();
        if (c >= 0) {
            return (char)c;
        }
        __asm__ volatile ("hlt");
    }
}

int keyboard_has_char(void) {
    return (kbd_head != kbd_tail) || serial_has_rx();
}
