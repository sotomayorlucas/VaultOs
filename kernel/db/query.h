#ifndef VAULTOS_QUERY_H
#define VAULTOS_QUERY_H

#include "database.h"

/* Execute a query string and return results */
query_result_t *query_execute(const char *input, uint64_t caller_pid);

#endif /* VAULTOS_QUERY_H */
