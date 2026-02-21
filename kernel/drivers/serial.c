#include "serial.h"
#include "../arch/x86_64/port_io.h"

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);  /* Disable interrupts */
    outb(COM1_PORT + 3, 0x80);  /* Enable DLAB (set baud rate divisor) */
    outb(COM1_PORT + 0, 0x01);  /* 115200 baud (divisor = 1) */
    outb(COM1_PORT + 1, 0x00);  /* Hi byte */
    outb(COM1_PORT + 3, 0x03);  /* 8 bits, no parity, one stop bit */
    outb(COM1_PORT + 2, 0xC7);  /* Enable FIFO, clear, 14-byte threshold */
    outb(COM1_PORT + 4, 0x0B);  /* IRQs enabled, RTS/DSR set */
}

static int serial_transmit_empty(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

void serial_putchar(char c) {
    while (!serial_transmit_empty());
    outb(COM1_PORT, c);
    /* Also send \r before \n for proper terminal behavior */
    if (c == '\n') {
        while (!serial_transmit_empty());
        outb(COM1_PORT, '\r');
    }
}

void serial_write(const char *str) {
    while (*str) {
        serial_putchar(*str++);
    }
}
