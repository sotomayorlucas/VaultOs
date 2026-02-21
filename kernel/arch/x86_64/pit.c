#include "pit.h"
#include "pic.h"
#include "port_io.h"
#include "idt.h"
#include "../../lib/printf.h"

static volatile uint64_t pit_ticks = 0;
static uint32_t pit_hz = 1000;

/* Forward declaration */
extern void scheduler_tick(void);

static void pit_handler(interrupt_frame_t *frame) {
    (void)frame;
    pit_ticks++;

    /* Call scheduler on every tick */
    scheduler_tick();

    pic_send_eoi(0);
}

void pit_init(uint32_t target_hz) {
    pit_hz = target_hz;
    uint32_t divisor = PIT_FREQUENCY / target_hz;

    /* Channel 0, Access mode: lobyte/hibyte, Mode 2 (rate generator) */
    outb(PIT_CMD, 0x34);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    /* Register handler and unmask IRQ 0 */
    irq_register_handler(0, pit_handler);
    pic_unmask(0);

    kprintf("[PIT] Timer initialized at %u Hz (divisor=%u)\n", target_hz, divisor);
}

uint64_t pit_get_ticks(void) {
    return pit_ticks;
}

uint64_t pit_get_uptime_ms(void) {
    return pit_ticks * 1000 / pit_hz;
}
