/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_task_timer_api_h
#define vnsw_agent_task_timer_api_h

#include "mcast_common.h"
#include <time.h>
#include "task_int.h"

typedef struct task_timer_ task_timer;

typedef void (*timer_callback)(task_timer *, time_t);

typedef struct task_timer_root_ {
} task_timer_root;

typedef struct task_timer_ {
    void *agent_timer_map;
    timer_callback callback;
    int timeout;
    boolean oneshot;
    void *tdata;
} task_timer;

typedef struct utime_t_ {
    time_t ut_sec;
    time_t ut_usec;
} utime_t;

#define MSECS_PER_SEC   1000
#define USECS_PER_MSEC  1000

extern task_timer *task_timer_create_idle_leaf(task *tp, const char *name,
                flag_t flags, task_timer *parent,
                timer_callback tjob, void_t data);
extern void task_timer_delete(task_timer *timer);
extern void task_timer_smear_auto_parent_timers(task_timer_root *root);
extern void task_timer_uset_alt_root_auto_parent_oneshot(task_timer_root *root,
                task_timer *timer, utime_t *offset, u_int jitter);
extern void task_timer_reset(task_timer *timer);
extern task_timer_root *task_timer_get_auto_parent_root(void);
extern void *task_timer_data(task_timer *timer);
extern void task_timer_utime_left(task_timer *timer, utime_t *remaining);
extern boolean task_timer_running(task_timer *timer);

#endif /* vnsw_agent_task_timer_api_h */
