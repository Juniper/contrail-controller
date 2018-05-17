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
extern bool task_timer_cleanup(void *data);

void task_timer_init(task *tp)
{
    tp->cleanup_timer = NULL;
    thread_new_circular_thread(&tp->deleted_timers);
}

void task_timer_cleanup_deleted(task *tp)
{
    TaskMap *agent_task = (TaskMap *)tp->agent_task;

    Timer * cleanup_timer;
    cleanup_timer = TimerManager::CreateTimer(agent_task->io_,
                                    "Cleanup Timer",
                                    TaskScheduler::GetInstance()->
                                    GetTaskId(agent_task->name_),
                                    agent_task->instance_, true);
    tp->cleanup_timer = (void *)cleanup_timer;
    cleanup_timer->Start(0, boost::bind(task_timer_cleanup, tp));
}

task_timer *task_timer_create_idle_leaf(task *tp, const char *name,
                flag_t flags, task_timer *parent,
                timer_callback tjob, void *data)
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

    timer->tp = tp;
    timer->callback = tjob;
    timer->agent_timer_map = timer_map;
    timer->tdata = data;

    return timer;
}

void task_timer_delete(task_timer *timer)
{
    TimerMap *timer_map = (TimerMap *)timer->agent_timer_map;

    if (!TimerManager::DeleteTimer(timer_map->agent_timer_)) {
        thread_circular_add_top(&timer->tp->deleted_timers,
                                    &timer->deleted_entry);
        task_timer_cleanup_deleted(timer->tp);
        return;
    }

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
    // If running in the timer context, Agent Timer cannot be started
    // again. Will inform Agent Timer to restart based on the 'oneshot'
    // and 'rescheduled' flags in the function 'task_timer_callback'.
    if (agent_timer->fired()) {
        agent_timer->Reschedule(timer->timeout);
        timer->rescheduled = TRUE;
    } else {
        agent_timer->Start(timer->timeout,
                    boost::bind(task_timer_callback, timer_map));
    }

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
    bool reschedule = false;
    if (timer->rescheduled == TRUE) {
        reschedule = true;
        timer->rescheduled = FALSE;
    }
    if (!reschedule) {
        reschedule = (!timer->oneshot ? true : false);
    }

    return reschedule;
}

bool task_timer_cleanup(void *data)
{
    task *tp = (task *)data;
    task_timer *timer = NULL;
    thread *thread_ptr = NULL;

    while (TRUE) {
        thread_ptr = thread_circular_top(&tp->deleted_timers);
        if (!thread_ptr) break;

        timer = task_timer_list_entry(thread_ptr);
        thread_remove(thread_ptr);
        TimerMap *timer_map = (TimerMap *)timer->agent_timer_map;
        if (!TimerManager::DeleteTimer(timer_map->agent_timer_)) {
            assert(0);
        }
        delete timer_map;

        free(timer);
    }

    tp->cleanup_timer = NULL;
    return false;
}

