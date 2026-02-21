#include "history.h"
#include "../kernel/lib/string.h"

static char history_buf[HISTORY_SIZE][HISTORY_LINE_MAX];
static uint32_t history_count = 0;  /* Total entries added */
static uint32_t history_head = 0;   /* Next write position (circular) */
static int32_t  history_nav = -1;   /* Navigation index, -1 = not navigating */

void history_init(void) {
    memset(history_buf, 0, sizeof(history_buf));
    history_count = 0;
    history_head = 0;
    history_nav = -1;
}

void history_add(const char *line) {
    if (!line || strlen(line) == 0) return;

    /* Skip duplicates of the most recent entry */
    if (history_count > 0) {
        uint32_t last = (history_head + HISTORY_SIZE - 1) % HISTORY_SIZE;
        if (strcmp(history_buf[last], line) == 0) return;
    }

    strncpy(history_buf[history_head], line, HISTORY_LINE_MAX - 1);
    history_buf[history_head][HISTORY_LINE_MAX - 1] = '\0';
    history_head = (history_head + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) history_count++;
}

const char *history_prev(void) {
    if (history_count == 0) return NULL;

    if (history_nav == -1) {
        /* Start navigating from the most recent entry */
        history_nav = (int32_t)((history_head + HISTORY_SIZE - 1) % HISTORY_SIZE);
    } else {
        /* Go further back */
        int32_t prev = (history_nav + HISTORY_SIZE - 1) % HISTORY_SIZE;

        /* Don't go past the oldest entry */
        uint32_t oldest = 0;
        if (history_count >= HISTORY_SIZE) {
            oldest = history_head; /* Oldest is at head (about to be overwritten) */
        }
        if ((uint32_t)prev == oldest && history_count >= HISTORY_SIZE) {
            /* At the oldest, check if wrapping would go past */
            if ((uint32_t)history_nav == oldest) return history_buf[history_nav];
        }

        /* Check if we've gone too far back */
        uint32_t start = (history_head + HISTORY_SIZE - history_count) % HISTORY_SIZE;
        /* Walk from start to head and see if prev is in range */
        bool in_range = false;
        for (uint32_t i = 0; i < history_count; i++) {
            if ((start + i) % HISTORY_SIZE == (uint32_t)prev) {
                in_range = true;
                break;
            }
        }
        if (!in_range) return history_buf[history_nav]; /* Already at oldest */

        history_nav = prev;
    }

    return history_buf[history_nav];
}

const char *history_next(void) {
    if (history_nav == -1) return NULL;

    int32_t next = (history_nav + 1) % HISTORY_SIZE;

    /* If we've reached head, we're past the newest entry */
    if ((uint32_t)next == history_head) {
        history_nav = -1;
        return NULL; /* Signal to restore saved line */
    }

    history_nav = next;
    return history_buf[history_nav];
}

void history_reset_nav(void) {
    history_nav = -1;
}
