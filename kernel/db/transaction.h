#ifndef VAULTOS_TRANSACTION_H
#define VAULTOS_TRANSACTION_H

#include "../lib/types.h"

typedef enum { TXN_ACTIVE, TXN_COMMITTED, TXN_ABORTED } txn_state_t;

typedef struct {
    uint64_t    txn_id;
    txn_state_t state;
    uint64_t    pid;
} transaction_t;

transaction_t *txn_begin(uint64_t pid);
int            txn_commit(transaction_t *txn);
int            txn_abort(transaction_t *txn);

#endif /* VAULTOS_TRANSACTION_H */
