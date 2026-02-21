#include "transaction.h"
#include "../../include/vaultos/error_codes.h"
#include "../lib/string.h"

static uint64_t next_txn_id = 1;
static transaction_t current_txn;

transaction_t *txn_begin(uint64_t pid) {
    current_txn.txn_id = next_txn_id++;
    current_txn.state = TXN_ACTIVE;
    current_txn.pid = pid;
    return &current_txn;
}

int txn_commit(transaction_t *txn) {
    if (txn->state != TXN_ACTIVE) return VOS_ERR_TXN_ABORT;
    txn->state = TXN_COMMITTED;
    return VOS_OK;
}

int txn_abort(transaction_t *txn) {
    txn->state = TXN_ABORTED;
    return VOS_OK;
}
