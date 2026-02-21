#include "line_editor.h"
#include "../kernel/drivers/keyboard.h"
#include "../kernel/drivers/framebuffer.h"
#include "../kernel/lib/string.h"

char *line_read(char *buf, size_t buf_size) {
    size_t pos = 0;
    memset(buf, 0, buf_size);

    while (pos < buf_size - 1) {
        char c = keyboard_getchar();

        switch (c) {
        case '\n':  /* Enter */
            fb_putchar('\n');
            buf[pos] = '\0';
            return buf;

        case '\b':  /* Backspace */
            if (pos > 0) {
                pos--;
                buf[pos] = '\0';
                fb_putchar('\b');
            }
            break;

        case 0x1B:  /* Escape - ignore */
            break;

        default:
            if (c >= 0x20 && c < 0x7F) {
                buf[pos++] = c;
                fb_putchar(c);
            }
            break;
        }
    }

    buf[pos] = '\0';
    return buf;
}
