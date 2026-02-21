#ifndef VAULTOS_IPC_H
#define VAULTOS_IPC_H

#include "../lib/types.h"

#define IPC_MSG_MAX_SIZE 512
#define IPC_QUEUE_SIZE   64

typedef struct {
    uint64_t msg_id;
    uint64_t src_pid;
    uint64_t dst_pid;
    uint32_t type;
    uint8_t  payload[IPC_MSG_MAX_SIZE];
    uint32_t payload_size;
    uint64_t timestamp;
} ipc_message_t;

void ipc_init(void);
int  ipc_send(uint64_t dst_pid, const ipc_message_t *msg);
int  ipc_recv(uint64_t pid, ipc_message_t *msg);

#endif /* VAULTOS_IPC_H */
