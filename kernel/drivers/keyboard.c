#include "keyboard.h"
#include "../arch/x86_64/port_io.h"
#include "../arch/x86_64/pic.h"
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/cpu.h"
#include "../lib/printf.h"

#define KB_DATA_PORT 0x60
#define KB_STATUS_PORT 0x64

/* Ring buffer for keyboard input */
static char kb_buffer[KB_BUFFER_SIZE];
static volatile uint16_t kb_head = 0;
static volatile uint16_t kb_tail = 0;

static bool shift_pressed = false;
static bool caps_lock = false;

/* Scancode Set 1 to ASCII (US layout) */
static const char scancode_to_ascii[128] = {
    0, 0x1B, '1', '2', '3', '4', '5', '6',     /* 0x00-0x07 */
    '7', '8', '9', '0', '-', '=', '\b', '\t',   /* 0x08-0x0F */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',     /* 0x10-0x17 */
    'o', 'p', '[', ']', '\n', 0, 'a', 's',       /* 0x18-0x1F */
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',     /* 0x20-0x27 */
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',     /* 0x28-0x2F */
    'b', 'n', 'm', ',', '.', '/', 0, '*',         /* 0x30-0x37 */
    0, ' ', 0, 0, 0, 0, 0, 0,                     /* 0x38-0x3F */
    0, 0, 0, 0, 0, 0, 0, '7',                     /* 0x40-0x47 */
    '8', '9', '-', '4', '5', '6', '+', '1',       /* 0x48-0x4F */
    '2', '3', '0', '.', 0, 0, 0, 0,               /* 0x50-0x57 */
    0, 0, 0, 0, 0, 0, 0, 0,                       /* 0x58-0x5F */
    0, 0, 0, 0, 0, 0, 0, 0,                       /* 0x60-0x67 */
    0, 0, 0, 0, 0, 0, 0, 0,                       /* 0x68-0x6F */
    0, 0, 0, 0, 0, 0, 0, 0,                       /* 0x70-0x77 */
    0, 0, 0, 0, 0, 0, 0, 0,                       /* 0x78-0x7F */
};

static const char scancode_to_ascii_shift[128] = {
    0, 0x1B, '!', '@', '#', '$', '%', '^',
    '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*',
    0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7',
    '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

static void kb_push(char c) {
    uint16_t next = (kb_head + 1) % KB_BUFFER_SIZE;
    if (next != kb_tail) {
        kb_buffer[kb_head] = c;
        kb_head = next;
    }
}

static void keyboard_handler(interrupt_frame_t *frame) {
    (void)frame;
    uint8_t scancode = inb(KB_DATA_PORT);

    /* Key release (bit 7 set) */
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        if (released == 0x2A || released == 0x36) { /* Left/Right Shift */
            shift_pressed = false;
        }
        pic_send_eoi(1);
        return;
    }

    /* Key press */
    switch (scancode) {
    case 0x2A: /* Left Shift */
    case 0x36: /* Right Shift */
        shift_pressed = true;
        break;
    case 0x3A: /* Caps Lock */
        caps_lock = !caps_lock;
        break;
    default: {
        char c;
        if (shift_pressed) {
            c = scancode_to_ascii_shift[scancode];
        } else {
            c = scancode_to_ascii[scancode];
        }

        /* Apply caps lock to letters */
        if (caps_lock && c >= 'a' && c <= 'z') c -= 32;
        else if (caps_lock && c >= 'A' && c <= 'Z') c += 32;

        if (c) kb_push(c);
        break;
    }
    }

    pic_send_eoi(1);
}

void keyboard_init(void) {
    kb_head = 0;
    kb_tail = 0;

    /* Register IRQ 1 handler */
    irq_register_handler(1, keyboard_handler);
    pic_unmask(1);

    kprintf("[KB] Keyboard initialized (PS/2, US layout)\n");
}

bool keyboard_has_input(void) {
    return kb_head != kb_tail;
}

char keyboard_getchar(void) {
    /* Busy wait for input */
    while (!keyboard_has_input()) {
        hlt(); /* Sleep until next interrupt */
    }
    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
    return c;
}

char keyboard_getchar_nonblock(void) {
    if (!keyboard_has_input()) return 0;
    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
    return c;
}
