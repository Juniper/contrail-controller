/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <fstream>
#include <tbb/task.h>
#include <base/task.h>
#include <base/task_monitor.h>
#include <base/logging.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <base/sandesh/task_types.h>

using namespace std;
void SandeshTaskRequest::HandleRequest() const {
    SandeshTaskScheduler *resp = new SandeshTaskScheduler;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->GetSandeshData(resp);
    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}

void SandeshTaskMonitorRequest::HandleRequest() const {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    SandeshMonitorResponse *resp = new SandeshMonitorResponse;
    resp->set_context(context());
    resp->set_more(false);

    const TaskMonitor *monitor = scheduler->task_monitor();
    if (monitor && monitor->inactivity_time_usec() &&
        monitor->poll_interval_msec()) {
        resp->set_running(true);
        resp->set_inactivity_time_msec(monitor->inactivity_time_msec());
        resp->set_poll_interval_msec(monitor->poll_interval_msec());
        resp->set_poll_count(monitor->poll_count());
        resp->set_last_activity(monitor->last_activity());
        resp->set_last_enqueue_count(monitor->last_enqueue_count());
        resp->set_last_done_count(monitor->last_done_count());
        resp->set_tbb_keepawake_time(monitor->tbb_keepawake_time_msec());
    } else {
        resp->set_running(false);
    }

    resp->Response();
}
