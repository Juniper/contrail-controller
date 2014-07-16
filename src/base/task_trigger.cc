/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/task_trigger.h"

#include "base/task.h"

class TaskTrigger::WorkerTask : public Task {
public:
    explicit WorkerTask(TaskTrigger *parent)
        : Task(parent->task_id_, parent->task_instance_), parent_(parent) {
    }
    bool Run() {
        if (parent_->disabled()) {
            parent_->Reset();
            return true;
        }
        bool done = (parent_->func_)();
        if (done) {
            parent_->Reset();
        }
        return done;
    }
private:
    TaskTrigger *parent_;
};

TaskTrigger::TaskTrigger(FunctionPtr func, int task_id, int task_instance)
    : func_(func), task_id_(task_id), task_instance_(task_instance) {
    trigger_ = false;
    disabled_ = false;
}

TaskTrigger::~TaskTrigger() {
    assert(!trigger_);
}

void TaskTrigger::Set() {
    bool current = trigger_.fetch_and_store(true);
    if (!current) {
        WorkerTask *task = new WorkerTask(this);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(task);
    }
}

void TaskTrigger::Reset() {
    trigger_ = false;
}
