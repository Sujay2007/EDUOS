#include "keyboard.h"
#include "io.h"
#include "pic.h"
#include "idt.h"
#include "serial.h"
#include "thread.h"

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
static volatile uint16_t kbd_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;
static uint8_t extended;
static int last_serial;
int keyboard_input_is_serial(void) { return last_serial; }
static unsigned serial_escape, serial_parameter;

static void push_key(unsigned char ch) {
    uint32_t next = (kbd_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != kbd_tail) { kbd_buffer[kbd_head] = ch; kbd_head = next; }
    thread_wake_input();
}

static void keyboard_isr(struct interrupt_frame *frame) {
    (void)frame;

    uint8_t scancode = inb(KBD_DATA_PORT);
    pic_send_eoi(1);

    if (scancode == 0xe0) { extended = 1; return; }
    if (extended) {
        extended = 0;
        if (scancode & 0x80) return;
        switch (scancode) {
            case 0x49: push_key(KEY_PAGE_UP); break;
            case 0x51: push_key(KEY_PAGE_DOWN); break;
            case 0x48: push_key(KEY_UP); break;
            case 0x50: push_key(KEY_DOWN); break;
            case 0x4b: push_key(KEY_LEFT); break;
            case 0x4d: push_key(KEY_RIGHT); break;
            case 0x47: push_key(KEY_HOME); break;
            case 0x4f: push_key(KEY_END); break;
            default: break;
        }
        return;
    }

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
            push_key(ch);
        }
    }
}

void keyboard_drain_serial(void) {
    int drained = 0;
    while (serial_has_rx()) {
        int ser_ch = serial_getchar_nonblock();
        if (ser_ch >= 0) {
            if (serial_escape == 1) {
                serial_escape = ser_ch == '[' || ser_ch == 'O' ? 2 : 0;
                serial_parameter = 0;
                if (serial_escape) continue;
            } else if (serial_escape == 2) {
                if (ser_ch >= '0' && ser_ch <= '9') {
                    serial_parameter = serial_parameter < 100 ? serial_parameter * 10 + ser_ch - '0' : 100;
                    continue;
                }
                unsigned char key = ser_ch == 'A' ? KEY_UP : ser_ch == 'B' ? KEY_DOWN :
                    ser_ch == 'H' ? KEY_HOME : ser_ch == 'F' ? KEY_END :
                    ser_ch == '~' && serial_parameter == 5 ? KEY_PAGE_UP :
                    ser_ch == '~' && serial_parameter == 6 ? KEY_PAGE_DOWN :
                    ser_ch == '~' && serial_parameter == 1 ? KEY_HOME :
                    ser_ch == '~' && serial_parameter == 4 ? KEY_END : 0;
                serial_escape = 0;
                if (key) {
                    uint32_t next=(kbd_head+1)%KEYBOARD_BUFFER_SIZE;
                    if(next!=kbd_tail) {kbd_buffer[kbd_head]=key|256; kbd_head=next;thread_wake_input();}
                }
                continue;
            }
            if (ser_ch == 27) { serial_escape = 1; continue; }
            /* Normalize CR/LF and Backspace */
            if (ser_ch == '\r') {
                ser_ch = '\n';
            } else if (ser_ch == 127) {
                ser_ch = '\b';
            }
            uint32_t next_head = (kbd_head + 1) % KEYBOARD_BUFFER_SIZE;
            if (next_head != kbd_tail) {
                kbd_buffer[kbd_head] = (unsigned char)ser_ch | 256;
                kbd_head = next_head;
                drained = 1;
            }
        }
    }
    if (drained) {
        thread_wake_input();
    }
}

static void com1_isr(struct interrupt_frame *frame) {
    (void)frame;
    keyboard_drain_serial();
    pic_send_eoi(4);
}

int keyboard_init(void) {
    /* Drain any pending bytes from keyboard data port */
    while (inb(KBD_STATUS_PORT) & 0x01) {
        inb(KBD_DATA_PORT);
    }

    /* Drain any pending bytes from serial port */
    while (serial_has_rx()) {
        serial_getchar_nonblock();
    }

    /* Reset buffer indices and state */
    kbd_head = 0;
    kbd_tail = 0;
    shift_pressed = 0;
    caps_lock_active = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    extended = 0;
    serial_escape = serial_parameter = 0;

    /* Register ISR for IRQ1 (vector 33) */
    idt_register_handler(33, keyboard_isr);

    /* Unmask IRQ1 on Master PIC */
    pic_clear_mask(1);

    /* Register ISR for IRQ4 (vector 36) for COM1 */
    idt_register_handler(36, com1_isr);

    /* Enable COM1 RX interrupt and unmask IRQ4 on Master PIC */
    serial_enable_rx_interrupt();
    pic_clear_mask(4);

    return 0;
}

int keyboard_getchar_nonblock(void) {
    uint64_t rflags = interrupts_save_and_disable();

    if (serial_has_rx()) {
        keyboard_drain_serial();
    }

    /* 1. Check PS/2 keyboard ring buffer */
    if (kbd_head != kbd_tail) {
        uint16_t event = kbd_buffer[kbd_tail];
        last_serial = (event & 256) != 0;
        char ch = (unsigned char)event;
        kbd_tail = (kbd_tail + 1) % KEYBOARD_BUFFER_SIZE;
        interrupts_restore(rflags);
        return (int)(uint8_t)ch;
    }

    interrupts_restore(rflags);
    return -1;
}

char keyboard_getchar(void) {
    for (;;) {
        uint64_t rflags = interrupts_save_and_disable();

        /* 1. Check PS/2 keyboard ring buffer */
        if (kbd_head != kbd_tail) {
            uint16_t event = kbd_buffer[kbd_tail];
        last_serial = (event & 256) != 0;
        char ch = (unsigned char)event;
            kbd_tail = (kbd_tail + 1) % KEYBOARD_BUFFER_SIZE;
            interrupts_restore(rflags);
            return ch;
        }

        /* 2. Check COM1 serial input */
        if (serial_has_rx()) {
            keyboard_drain_serial();
            if (kbd_head != kbd_tail) {
                uint16_t event = kbd_buffer[kbd_tail];
        last_serial = (event & 256) != 0;
        char ch = (unsigned char)event;
                kbd_tail = (kbd_tail + 1) % KEYBOARD_BUFFER_SIZE;
                interrupts_restore(rflags);
                return ch;
            }
        }

        /* 3. Block current thread until keyboard or serial interrupt arrives */
        thread_wait_input();
        interrupts_restore(rflags);
    }
}

int keyboard_has_char(void) {
    return (kbd_head != kbd_tail) || serial_has_rx();
}
