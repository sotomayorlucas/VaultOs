#ifndef VAULTOS_LIST_H
#define VAULTOS_LIST_H

#include "types.h"

/* Intrusive doubly-linked list (Linux kernel style) */
typedef struct list_node {
    struct list_node *next;
    struct list_node *prev;
} list_node_t;

#define LIST_INIT(name) { &(name), &(name) }

static inline void list_init(list_node_t *head) {
    head->next = head;
    head->prev = head;
}

static inline void list_add(list_node_t *head, list_node_t *node) {
    node->next = head->next;
    node->prev = head;
    head->next->prev = node;
    head->next = node;
}

static inline void list_add_tail(list_node_t *head, list_node_t *node) {
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}

static inline void list_del(list_node_t *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

static inline bool list_empty(list_node_t *head) {
    return head->next == head;
}

static inline size_t list_count(list_node_t *head) {
    size_t count = 0;
    list_node_t *pos = head->next;
    while (pos != head) {
        count++;
        pos = pos->next;
    }
    return count;
}

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, tmp, head) \
    for (pos = (head)->next, tmp = pos->next; pos != (head); pos = tmp, tmp = pos->next)

#endif /* VAULTOS_LIST_H */
