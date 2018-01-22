/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_task_thread_api_h
#define vnsw_agent_task_thread_api_h

#include "mcast_common.h"

typedef struct thread_ {
    struct thread_ *next;
    struct thread_ *prev;
} thread;

#define THREAD_TO_STRUCT(function, structure, member)                   \
    static inline structure * (function)(thread *address) {             \
        if (address) {                                                  \
            return (structure *)((char*)address - offsetof(structure, member));\
        }                                                               \
        return NULL;                                                    \
    }

extern void thread_new_circular_thread(thread *head);
extern boolean thread_circular_thread_empty(thread *head);
extern boolean thread_circular_thread_head(thread *head, thread *node);
extern boolean thread_node_on_thread(const thread *node);
extern thread *thread_circular_top(thread *head);
extern void thread_circular_add_top(thread *head, thread *node);
extern void thread_circular_add_bottom(thread *head, thread *node);
extern thread *thread_next_node(thread *node);
extern thread *thread_circular_thread_next(thread *head, thread *node);
extern thread *thread_circular_dequeue_top(thread *head);
extern void thread_remove(thread *node);

#define FOR_ALL_CIRCULAR_THREAD_ENTRIES(head, current)                  \
    for ((current) = thread_next_node(head);                            \
            !thread_circular_thread_head(head, (current));              \
            (current) = thread_next_node(current))

#endif /* vnsw_agent_task_thread_api_h */
