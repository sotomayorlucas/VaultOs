#ifndef VAULTOS_DISPLAY_H
#define VAULTOS_DISPLAY_H

#include "../kernel/db/database.h"

/* Display query result as formatted table (with interactive paging for large results) */
void display_result(query_result_t *result);

#endif /* VAULTOS_DISPLAY_H */
