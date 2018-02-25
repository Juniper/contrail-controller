/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_task_api_h
#define vnsw_agent_task_api_h

#include "task_thread_api.h"

typedef struct task_ {
    void *agent_task;
    void *cleanup_timer;
    thread deleted_timers;
} task;

#endif /* vnsw_agent_task_api_h */
