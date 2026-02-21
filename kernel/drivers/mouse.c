#include "mouse.h"
#include "../arch/x86_64/port_io.h"
#include "../arch/x86_64/pic.h"
#include "../arch/x86_64/idt.h"
#include "../lib/printf.h"

/* i8042 controller ports */
#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

/* i8042 controller commands */
#define PS2_CMD_READ_CONFIG   0x20
#define PS2_CMD_WRITE_CONFIG  0x60
#define PS2_CMD_DISABLE_AUX   0xA7
#define PS2_CMD_ENABLE_AUX    0xA8
#define PS2_CMD_SEND_TO_AUX   0xD4

/* Mouse commands */
#define MOUSE_CMD_SET_DEFAULTS    0xF6
#define MOUSE_CMD_ENABLE_REPORT   0xF4
#define MOUSE_CMD_SET_SAMPLE_RATE 0xF3
#define MOUSE_CMD_GET_DEVICE_ID   0xF2
#define MOUSE_CMD_RESET           0xFF

/* Event ring buffer */
#define MOUSE_BUF_SIZE 64
static mouse_event_t event_buf[MOUSE_BUF_SIZE];
static volatile uint32_t buf_head = 0;
static volatile uint32_t buf_tail = 0;

/* Packet assembly state */
static uint8_t packet[3];
static uint8_t packet_idx = 0;

/* Absolute position tracking */
static int16_t abs_x = 512;  /* Start at center of 1024 */
static int16_t abs_y = 384;  /* Start at center of 768 */
static uint16_t screen_w = 1024;
static uint16_t screen_h = 768;

static void mouse_wait_input(void) {
    int timeout = 100000;
    while (timeout--) {
        if (inb(PS2_STATUS) & 0x02) continue;
        return;
    }
}

static void mouse_wait_output(void) {
    int timeout = 100000;
    while (timeout--) {
        if (inb(PS2_STATUS) & 0x01) return;
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait_input();
    outb(PS2_CMD, PS2_CMD_SEND_TO_AUX);
    mouse_wait_input();
    outb(PS2_DATA, data);
}

static uint8_t mouse_read(void) {
    mouse_wait_output();
    return inb(PS2_DATA);
}

static void mouse_irq_handler(interrupt_frame_t *frame) {
    (void)frame;
    uint8_t status = inb(PS2_STATUS);

    /* Bit 5 = auxiliary data (mouse), bit 0 = output buffer full */
    if (!(status & 0x21)) {
        pic_send_eoi(12);
        return;
    }

    uint8_t data = inb(PS2_DATA);

    /* First byte must have bit 3 set (always-1 bit in PS/2 protocol) */
    if (packet_idx == 0 && !(data & 0x08)) {
        pic_send_eoi(12);
        return;
    }

    packet[packet_idx++] = data;

    if (packet_idx >= 3) {
        packet_idx = 0;

        /* Decode packet */
        uint8_t buttons = packet[0] & 0x07;
        int16_t dx = (int16_t)packet[1] - ((packet[0] & 0x10) ? 256 : 0);
        int16_t dy = (int16_t)packet[2] - ((packet[0] & 0x20) ? 256 : 0);
        dy = -dy; /* Invert Y: PS/2 has Y-up, screen has Y-down */

        /* Update absolute position */
        abs_x += dx;
        abs_y += dy;
        if (abs_x < 0) abs_x = 0;
        if (abs_y < 0) abs_y = 0;
        if (abs_x >= screen_w) abs_x = screen_w - 1;
        if (abs_y >= screen_h) abs_y = screen_h - 1;

        /* Push event to ring buffer */
        uint32_t next = (buf_head + 1) % MOUSE_BUF_SIZE;
        if (next != buf_tail) {
            event_buf[buf_head].dx = dx;
            event_buf[buf_head].dy = dy;
            event_buf[buf_head].buttons = buttons;
            buf_head = next;
        }
    }

    pic_send_eoi(12);
}

void mouse_init(void) {
    /* Enable auxiliary device */
    mouse_wait_input();
    outb(PS2_CMD, PS2_CMD_ENABLE_AUX);

    /* Read controller config */
    mouse_wait_input();
    outb(PS2_CMD, PS2_CMD_READ_CONFIG);
    uint8_t config = mouse_read();

    /* Enable IRQ 12 (bit 1) and disable auxiliary clock disable (bit 5) */
    config |= 0x02;   /* Enable aux IRQ */
    config &= ~0x20;  /* Enable aux clock */

    mouse_wait_input();
    outb(PS2_CMD, PS2_CMD_WRITE_CONFIG);
    mouse_wait_input();
    outb(PS2_DATA, config);

    /* Set defaults */
    mouse_write(MOUSE_CMD_SET_DEFAULTS);
    mouse_read(); /* ACK */

    /* Enable data reporting */
    mouse_write(MOUSE_CMD_ENABLE_REPORT);
    mouse_read(); /* ACK */

    /* Register IRQ handler and unmask */
    irq_register_handler(12, mouse_irq_handler);
    pic_unmask(12);

    /* Also unmask IRQ 2 (cascade) if not already */
    pic_unmask(2);

    kprintf("[MOUSE] PS/2 mouse initialized\n");
}

bool mouse_poll(mouse_event_t *ev) {
    if (buf_tail == buf_head) return false;

    *ev = event_buf[buf_tail];
    buf_tail = (buf_tail + 1) % MOUSE_BUF_SIZE;
    return true;
}

void mouse_get_position(int16_t *x, int16_t *y) {
    if (x) *x = abs_x;
    if (y) *y = abs_y;
}

void mouse_set_bounds(uint16_t width, uint16_t height) {
    screen_w = width;
    screen_h = height;
    if (abs_x >= screen_w) abs_x = screen_w - 1;
    if (abs_y >= screen_h) abs_y = screen_h - 1;
}
