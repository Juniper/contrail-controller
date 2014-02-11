/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <fstream>
#include <tbb/task.h>
#include <base/task.h>
#include <base/logging.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <base/sandesh/task_types.h>

using namespace std;

class TaskEntry;
class Task;
class TaskGroup;

// TaskEntry key is in format task-id:instance
void DecodeTaskEntryKey(const string &key, int *task_id, int *instance_id) {
    *task_id = 0;
    *instance_id = 0;
    sscanf(key.c_str(), "%d:%d", task_id, instance_id);
}

// TaskEntry key is in format task-id:instance:seqno
void DecodeTaskKey(const string &key, int *task_id, int *instance_id,
                   int *seqno) {
    *task_id = 0;
    *instance_id = 0;
    *seqno = 0;
    sscanf(key.c_str(), "%d:%d:%d", task_id, instance_id, seqno);
}

static void SetStats(SandeshTaskStats &dest, TaskStats *src) {
    dest.set_wait_count(src->wait_count_);
    dest.set_run_count(src->run_count_);
    dest.set_defer_count(src->defer_count_);
}

void SandeshTaskSchedulerReq::HandleRequest() const {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    tbb::mutex::scoped_lock lock(scheduler->mutex_);

    SandeshTaskSchedulerResp *resp = new SandeshTaskSchedulerResp;
    resp->set_running(scheduler->running_);
    resp->set_seqno(scheduler->seqno_);
    resp->set_thread_count(scheduler->hw_thread_count_);

    std::vector<SandeshTaskGroupNameSummary> list;
    for (TaskScheduler::TaskIdMap::const_iterator it = 
            scheduler->id_map_.begin();
         it != scheduler->id_map_.end(); it++) {
        SandeshTaskGroupNameSummary entry;
        entry.set_task_id(it->second);
        entry.set_name(it->first);
        list.push_back(entry);
    }
    resp->set_task_group_list(list);

    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}

void SandeshTaskGroupReq::HandleRequest() const {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    tbb::mutex::scoped_lock lock(scheduler->mutex_);

    SandeshTaskGroupResp *resp = new SandeshTaskGroupResp;
    TaskGroup *group = scheduler->QueryTaskGroup(get_task_id());

    if (group != NULL) {
        resp->set_task_id(get_task_id());
        SandeshTaskStats stats;
        SetStats(stats, scheduler->GetTaskGroupStats(get_task_id()));
        resp->set_summary_stats(stats);
        scheduler->GetTaskGroupSandeshData(get_task_id(), resp);
    }

    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}

void SandeshTaskEntryReq::HandleRequest() const {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    tbb::mutex::scoped_lock lock(scheduler->mutex_);

    SandeshTaskEntryResp *resp = new SandeshTaskEntryResp;
    int task_id;
    int instance_id;
    DecodeTaskEntryKey(get_key(), &task_id, &instance_id);
    scheduler->GetTaskEntrySandeshData(task_id, instance_id, resp);

    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}

void SandeshTaskReq::HandleRequest() const {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    tbb::mutex::scoped_lock lock(scheduler->mutex_);

    SandeshTaskResp *resp = new SandeshTaskResp;
    int task_id;
    int instance_id;
    int seqno;
    DecodeTaskKey(get_key(), &task_id, &instance_id, &seqno);
    scheduler->GetTaskSandeshData(task_id, instance_id, seqno, resp);

    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}
