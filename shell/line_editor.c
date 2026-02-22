#include "line_editor.h"
#include "history.h"
#include "complete.h"
#include "friendly.h"
#include "tui.h"
#include "../kernel/drivers/keyboard.h"
#include "../kernel/drivers/framebuffer.h"
#include "../kernel/lib/string.h"
#include "../kernel/lib/printf.h"
#include "../kernel/arch/x86_64/pit.h"
#include "../kernel/arch/x86_64/cpu.h"

/* SQL keywords for syntax highlighting */
static const char *highlight_keywords[] = {
    "SELECT", "INSERT", "INTO", "DELETE", "UPDATE",
    "FROM", "WHERE", "AND", "SET", "VALUES",
    "SHOW", "TABLES", "DESCRIBE",
    "GRANT", "REVOKE", "ON", "TO",
    "READ", "WRITE", "ALL", "OR",
    /* Friendly command verbs */
    "INFO", "FIND", "COUNT", "ADD", "DEL",
    "CREATE", "OPEN", "LIST", "RM", "CAT",
    "PS", "KILL", "SPAWN", "MSG", "INBOX",
    "SAVE", "RUN", "SCRIPTS",
    "SECURITY", "CRYPTO", "AUDIT",
    NULL
};

/* Editor state */
static struct {
    char     buf[LINE_MAX];
    uint32_t len;
    uint32_t cursor;
    uint32_t prompt_col;
    uint32_t prompt_row;
    char     saved_line[LINE_MAX];
    bool     in_history;
} ed;

static bool is_keyword(const char *word, size_t wlen) {
    for (int i = 0; highlight_keywords[i]; i++) {
        if (strlen(highlight_keywords[i]) == wlen &&
            strncasecmp(highlight_keywords[i], word, wlen) == 0)
            return true;
    }
    return false;
}

/* Get the color for a character at position `pos` in the edit buffer */
static uint32_t get_char_color(uint32_t pos) {
    char c = ed.buf[pos];

    /* String literals in green */
    /* Count quotes before this position */
    bool in_string = false;
    for (uint32_t i = 0; i < pos; i++) {
        if (ed.buf[i] == '\'') in_string = !in_string;
    }
    if (in_string || c == '\'') return COLOR_GREEN;

    /* Numbers in cyan */
    if (c >= '0' && c <= '9') {
        /* Check if part of a word (then it's an identifier) */
        bool standalone = (pos == 0 || ed.buf[pos - 1] == ' ' || ed.buf[pos - 1] == ','
                          || ed.buf[pos - 1] == '(');
        if (standalone) return COLOR_CYAN;
    }

    /* Keywords: find the word boundaries */
    if (isalpha(c) || c == '_') {
        /* Find word start */
        uint32_t ws = pos;
        while (ws > 0 && (isalnum(ed.buf[ws - 1]) || ed.buf[ws - 1] == '_')) ws--;
        /* Find word end */
        uint32_t we = pos;
        while (we < ed.len && (isalnum(ed.buf[we]) || ed.buf[we] == '_')) we++;

        if (is_keyword(ed.buf + ws, we - ws))
            return COLOR_VOS_HL;

        /* Table aliases highlighted in cyan */
        size_t wlen = we - ws;
        const table_alias_t *al = friendly_get_aliases();
        for (int i = 0; al[i].alias; i++) {
            if (strlen(al[i].alias) == wlen &&
                strncasecmp(al[i].alias, ed.buf + ws, wlen) == 0)
                return COLOR_CYAN;
        }
    }

    /* Operators in yellow */
    if (c == '=' || c == '<' || c == '>' || c == '!' || c == '*')
        return COLOR_YELLOW;

    return COLOR_VOS_FG;
}

/* Redraw the entire input line with syntax highlighting */
static void redraw_line(void) {
    uint32_t col = ed.prompt_col;
    uint32_t row = ed.prompt_row;
    uint32_t max_cols = fb_get_cols();

    /* Draw each character with appropriate color */
    for (uint32_t i = 0; i < ed.len && col < max_cols; i++) {
        uint32_t color = get_char_color(i);
        fb_draw_cell(col, row, ed.buf[i], color, COLOR_VOS_BG);
        col++;
    }

    /* Clear rest of line */
    while (col < max_cols) {
        fb_draw_cell(col, row, ' ', COLOR_VOS_FG, COLOR_VOS_BG);
        col++;
    }

    /* Draw cursor: character at cursor position with inverted colors */
    uint32_t cursor_col = ed.prompt_col + ed.cursor;
    if (cursor_col < max_cols) {
        char cursor_char = (ed.cursor < ed.len) ? ed.buf[ed.cursor] : ' ';
        fb_draw_cell(cursor_col, row, cursor_char, COLOR_VOS_BG, COLOR_VOS_FG);
    }
}

/* Erase cursor (restore normal rendering at cursor position) */
static void erase_cursor(void) {
    uint32_t cursor_col = ed.prompt_col + ed.cursor;
    uint32_t max_cols = fb_get_cols();
    if (cursor_col < max_cols) {
        if (ed.cursor < ed.len) {
            uint32_t color = get_char_color(ed.cursor);
            fb_draw_cell(cursor_col, ed.prompt_row, ed.buf[ed.cursor], color, COLOR_VOS_BG);
        } else {
            fb_draw_cell(cursor_col, ed.prompt_row, ' ', COLOR_VOS_FG, COLOR_VOS_BG);
        }
    }
}

/* Show completion matches below the input line */
static void show_completions(const completion_result_t *comp) {
    uint32_t row = ed.prompt_row + 1;
    uint32_t max_cols = fb_get_cols();
    const tui_layout_t *lay = tui_get_layout();

    if (row >= lay->content_bottom) return;

    uint32_t col = ed.prompt_col;
    for (uint32_t i = 0; i < comp->count && row < lay->content_bottom; i++) {
        /* Draw match */
        uint32_t c = col;
        const char *match = comp->matches[i];
        while (*match && c < max_cols) {
            fb_draw_cell(c, row, *match, COLOR_CYAN, COLOR_DKGRAY);
            c++;
            match++;
        }
        while (c < col + 20 && c < max_cols) {
            fb_draw_cell(c, row, ' ', COLOR_CYAN, COLOR_DKGRAY);
            c++;
        }
        row++;
    }
}

/* Clear completion display */
static void clear_completions(uint32_t count) {
    uint32_t row = ed.prompt_row + 1;
    const tui_layout_t *lay = tui_get_layout();

    for (uint32_t i = 0; i < count && row < lay->content_bottom; i++) {
        fb_clear_rows(row, row + 1);
        row++;
    }
}

void line_editor_init(void) {
    /* Nothing needed currently */
}

char *line_read(char *buf, size_t buf_size, uint8_t *out_fkey) {
    if (out_fkey) *out_fkey = 0;
    memset(&ed, 0, sizeof(ed));
    fb_get_cursor(&ed.prompt_col, &ed.prompt_row);
    history_reset_nav();

    /* Draw initial cursor */
    redraw_line();

    uint32_t last_comp_count = 0;
    uint64_t last_refresh = pit_get_uptime_ms();

    while (1) {
        /* Non-blocking read with periodic status refresh */
        char c = 0;
        while (!c) {
            c = keyboard_getchar_nonblock();
            if (!c) {
                uint64_t now = pit_get_uptime_ms();
                if (now - last_refresh >= 500) {
                    tui_refresh_status();
                    last_refresh = now;
                }
                hlt();
            }
        }

        /* Clear old completions if any */
        if (last_comp_count > 0) {
            clear_completions(last_comp_count);
            last_comp_count = 0;
        }

        uint8_t key = (uint8_t)c;

        switch (key) {
        case '\n': /* Enter */
            erase_cursor();
            ed.buf[ed.len] = '\0';
            /* Move cursor to end of text and newline */
            fb_set_cursor(ed.prompt_col + ed.len, ed.prompt_row);
            fb_putchar('\n');
            memcpy(buf, ed.buf, ed.len + 1);
            if (ed.len > 0) history_add(ed.buf);
            return buf;

        case '\b': /* Backspace */
            if (ed.cursor > 0) {
                memmove(&ed.buf[ed.cursor - 1], &ed.buf[ed.cursor],
                        ed.len - ed.cursor);
                ed.cursor--;
                ed.len--;
                ed.buf[ed.len] = '\0';
                redraw_line();
            }
            break;

        case KEY_DELETE:
            if (ed.cursor < ed.len) {
                memmove(&ed.buf[ed.cursor], &ed.buf[ed.cursor + 1],
                        ed.len - ed.cursor - 1);
                ed.len--;
                ed.buf[ed.len] = '\0';
                redraw_line();
            }
            break;

        case KEY_LEFT:
            if (ed.cursor > 0) {
                ed.cursor--;
                redraw_line();
            }
            break;

        case KEY_RIGHT:
            if (ed.cursor < ed.len) {
                ed.cursor++;
                redraw_line();
            }
            break;

        case KEY_HOME:
            ed.cursor = 0;
            redraw_line();
            break;

        case KEY_END:
            ed.cursor = ed.len;
            redraw_line();
            break;

        case KEY_UP: {
            if (!ed.in_history) {
                memcpy(ed.saved_line, ed.buf, LINE_MAX);
                ed.in_history = true;
            }
            const char *prev = history_prev();
            if (prev) {
                strncpy(ed.buf, prev, LINE_MAX - 1);
                ed.buf[LINE_MAX - 1] = '\0';
                ed.len = strlen(ed.buf);
                ed.cursor = ed.len;
                redraw_line();
            }
            break;
        }

        case KEY_DOWN: {
            const char *next = history_next();
            if (next) {
                strncpy(ed.buf, next, LINE_MAX - 1);
                ed.buf[LINE_MAX - 1] = '\0';
            } else {
                memcpy(ed.buf, ed.saved_line, LINE_MAX);
                ed.in_history = false;
            }
            ed.len = strlen(ed.buf);
            ed.cursor = ed.len;
            redraw_line();
            break;
        }

        case '\t': { /* Tab completion */
            completion_result_t comp;
            complete_find(ed.buf, ed.cursor, &comp);

            if (comp.count == 1) {
                /* Single match: insert remaining characters */
                /* Find the word start */
                uint32_t ws = ed.cursor;
                while (ws > 0 && ed.buf[ws - 1] != ' ' && ed.buf[ws - 1] != '('
                              && ed.buf[ws - 1] != ',')
                    ws--;

                uint32_t word_len = ed.cursor - ws;
                const char *match = comp.matches[0];
                uint32_t match_len = strlen(match);

                if (match_len > word_len) {
                    uint32_t insert_len = match_len - word_len;
                    if (ed.len + insert_len < LINE_MAX - 1) {
                        /* Make room */
                        memmove(&ed.buf[ed.cursor + insert_len],
                                &ed.buf[ed.cursor],
                                ed.len - ed.cursor);
                        memcpy(&ed.buf[ed.cursor], match + word_len, insert_len);
                        ed.len += insert_len;
                        ed.cursor += insert_len;
                        ed.buf[ed.len] = '\0';

                        /* Add space after completion if at end */
                        if (ed.cursor == ed.len && ed.len < LINE_MAX - 2) {
                            ed.buf[ed.len] = ' ';
                            ed.len++;
                            ed.cursor++;
                            ed.buf[ed.len] = '\0';
                        }
                    }
                }
                redraw_line();
            } else if (comp.count > 1) {
                /* Multiple matches: insert common prefix and show options */
                uint32_t ws = ed.cursor;
                while (ws > 0 && ed.buf[ws - 1] != ' ' && ed.buf[ws - 1] != '('
                              && ed.buf[ws - 1] != ',')
                    ws--;

                uint32_t word_len = ed.cursor - ws;
                uint32_t prefix_len = strlen(comp.common_prefix);

                if (prefix_len > word_len) {
                    uint32_t insert_len = prefix_len - word_len;
                    if (ed.len + insert_len < LINE_MAX - 1) {
                        memmove(&ed.buf[ed.cursor + insert_len],
                                &ed.buf[ed.cursor],
                                ed.len - ed.cursor);
                        memcpy(&ed.buf[ed.cursor],
                               comp.common_prefix + word_len, insert_len);
                        ed.len += insert_len;
                        ed.cursor += insert_len;
                        ed.buf[ed.len] = '\0';
                    }
                }
                redraw_line();
                show_completions(&comp);
                last_comp_count = comp.count;
            }
            break;
        }

        case KEY_F1: case KEY_F2: case KEY_F3: case KEY_F4:
        case KEY_F5: case KEY_F6: case KEY_F7: case KEY_F8:
        case KEY_F9: case KEY_F10:
            if (out_fkey) *out_fkey = key;
            erase_cursor();
            buf[0] = '\0';
            return buf;

        case 0x0C: /* Ctrl+L = clear content area */
        {
            const tui_layout_t *lay = tui_get_layout();
            fb_clear_rows(lay->content_top, lay->content_bottom);
            fb_set_cursor(0, lay->content_top);
            /* Return empty to let shell redraw prompt */
            erase_cursor();
            buf[0] = '\0';
            if (out_fkey) *out_fkey = KEY_F5; /* Treat as clear */
            return buf;
        }

        case 0x1B: /* Escape: clear line */
            ed.len = 0;
            ed.cursor = 0;
            ed.buf[0] = '\0';
            ed.in_history = false;
            history_reset_nav();
            redraw_line();
            break;

        default:
            if (c >= 0x20 && c < 0x7F && ed.len < LINE_MAX - 1) {
                /* Insert character at cursor */
                memmove(&ed.buf[ed.cursor + 1], &ed.buf[ed.cursor],
                        ed.len - ed.cursor);
                ed.buf[ed.cursor] = c;
                ed.cursor++;
                ed.len++;
                ed.buf[ed.len] = '\0';
                redraw_line();
            }
            break;
        }
    }
}
