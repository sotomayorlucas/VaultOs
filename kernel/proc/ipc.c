#include "ipc.h"
#include "../arch/x86_64/pit.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "../../include/vaultos/error_codes.h"

static ipc_message_t queue[IPC_QUEUE_SIZE];
static uint16_t queue_head = 0;
static uint16_t queue_tail = 0;
static uint64_t next_msg_id = 1;

void ipc_init(void) {
    memset(queue, 0, sizeof(queue));
    queue_head = 0;
    queue_tail = 0;
    next_msg_id = 1;
    kprintf("[IPC] Message passing initialized\n");
}

int ipc_send(uint64_t dst_pid, const ipc_message_t *msg) {
    uint16_t next = (queue_head + 1) % IPC_QUEUE_SIZE;
    if (next == queue_tail) return VOS_ERR_FULL;

    queue[queue_head] = *msg;
    queue[queue_head].msg_id = next_msg_id++;
    queue[queue_head].dst_pid = dst_pid;
    queue[queue_head].timestamp = pit_get_ticks();
    queue_head = next;

    return VOS_OK;
}

int ipc_recv(uint64_t pid, ipc_message_t *msg) {
    /* Scan queue for message addressed to this pid */
    uint16_t idx = queue_tail;
    while (idx != queue_head) {
        if (queue[idx].dst_pid == pid || queue[idx].dst_pid == 0) {
            *msg = queue[idx];
            /* Remove from queue (shift remaining) */
            uint16_t next = (idx + 1) % IPC_QUEUE_SIZE;
            while (next != queue_head) {
                queue[idx] = queue[next];
                idx = next;
                next = (next + 1) % IPC_QUEUE_SIZE;
            }
            queue_head = (queue_head - 1 + IPC_QUEUE_SIZE) % IPC_QUEUE_SIZE;
            return VOS_OK;
        }
        idx = (idx + 1) % IPC_QUEUE_SIZE;
    }
    return VOS_ERR_NOTFOUND;
}
