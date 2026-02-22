#ifndef VAULTOS_SCRIPT_H
#define VAULTOS_SCRIPT_H

#include "../kernel/lib/types.h"

#define SCRIPT_MAX_LINES  64
#define SCRIPT_LINE_MAX   255

/* Save a script: multi-line input mode until user types "end".
 * Returns number of lines saved, or -1 on error. */
int script_save(const char *name);

/* Run a script: fetch lines from ObjectTable and execute each.
 * Returns number of lines executed, or -1 if not found. */
int script_run(const char *name);

/* List unique script names to console. */
void script_list(void);

/* Show script content with line numbers. Also works for generic objects. */
void script_cat(const char *name);

/* Entry point for running a script as a spawned process.
 * Reads script name from process_t.name (after "script:" prefix). */
void script_process_entry(void);

#endif /* VAULTOS_SCRIPT_H */
