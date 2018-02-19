/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <string>
#include "base/timer.h"

#include "task_map.h"
#include "task_timer_map.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "task_timer_api.h"
#ifdef __cplusplus
}
#endif

extern bool task_timer_callback(void *agent_timer_map);

task_timer *task_timer_create_idle_leaf(task *tp, const char *name,
                flag_t flags, task_timer *parent,
                timer_callback tjob, void_t data)
{
    task_timer *timer = (task_timer *)malloc(sizeof(task_timer));
    if (!timer) {
        return NULL;
    }

    TaskMap *agent_task = (TaskMap *)tp->agent_task;
    TimerMap *timer_map = new TimerMap();

    timer_map->timer_name_ = name;
    timer_map->timer_ = timer;
    timer_map->agent_timer_ = TimerManager::CreateTimer(agent_task->io_,
                                    timer_map->timer_name_,
                                    TaskScheduler::GetInstance()->
                                    GetTaskId(agent_task->name_),
                                    agent_task->instance_, false);

    timer->callback = tjob;
    timer->agent_timer_map = timer_map;
    timer->tdata = data;

    return timer;
}

void task_timer_delete(task_timer *timer)
{
    TimerMap *timer_map = (TimerMap *)timer->agent_timer_map;

    TimerManager::DeleteTimer(timer_map->agent_timer_);

    delete timer_map;

    free(timer);

    return;
}

void task_timer_smear_auto_parent_timers(task_timer_root *root)
{
    return;
}

void task_timer_uset_alt_root_auto_parent_oneshot(task_timer_root *root,
                task_timer *timer, utime_t *offset, u_int jitter)
{
    TimerMap *timer_map = (TimerMap *)timer->agent_timer_map;

    Timer *agent_timer = timer_map->agent_timer_;

    timer->timeout = offset->ut_sec*1000 + offset->ut_usec/1000;

    task_timer_reset(timer);
    agent_timer->Start(timer->timeout,
                    boost::bind(task_timer_callback, timer_map));

    timer->oneshot = TRUE;

    return;
}

void task_timer_reset(task_timer *timer)
{
    TimerMap *timer_map = (TimerMap *)timer->agent_timer_map;
    Timer *agent_timer = (Timer *)timer_map->agent_timer_;

    agent_timer->Cancel();

    return;
}

task_timer_root *task_timer_get_auto_parent_root(void)
{
    return NULL;
}

void *task_timer_data(task_timer *timer)
{
    return timer->tdata;
}

void task_timer_utime_left(task_timer *timer, utime_t *remaining)
{
    TimerMap *timer_map = (TimerMap *)timer->agent_timer_map;
    Timer *agent_timer = (Timer *)timer_map->agent_timer_;

    int elapsed = timer->timeout - agent_timer->GetElapsedTime();

    remaining->ut_usec = elapsed*1000;
    remaining->ut_sec = 0;

    return;
}

boolean task_timer_running(task_timer *timer)
{
    TimerMap *timer_map = (TimerMap *)timer->agent_timer_map;
    Timer *agent_timer = (Timer *)timer_map->agent_timer_;

    return agent_timer->running() ? TRUE : FALSE;
}

bool task_timer_callback(void *agent_timer_map)
{
    TimerMap *timer_map = (TimerMap *)agent_timer_map;
    task_timer *timer = (task_timer *)timer_map->timer_;

    timer->callback(timer, 0);

    return !timer->oneshot ? true : false;
}

