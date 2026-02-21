#ifndef VAULTOS_COMPLETE_H
#define VAULTOS_COMPLETE_H

#include "../kernel/lib/types.h"

#define MAX_COMPLETIONS    32
#define MAX_COMPLETION_LEN 64

typedef struct {
    char     matches[MAX_COMPLETIONS][MAX_COMPLETION_LEN];
    uint32_t count;
    char     common_prefix[MAX_COMPLETION_LEN]; /* Longest common prefix */
} completion_result_t;

/* Find completions for the word at cursor_pos in the given line */
void complete_find(const char *line, uint32_t cursor_pos, completion_result_t *result);

#endif /* VAULTOS_COMPLETE_H */
