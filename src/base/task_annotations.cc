/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/task_annotations.h"

#include "base/task.h"

bool ConcurrencyChecker::enable_ = getenv("CONCURRENCY_CHECK_ENABLE") != NULL &&
    !strcasecmp(getenv("CONCURRENCY_CHECK_ENABLE"), "true");

ConcurrencyChecker::ConcurrencyChecker(const char *task_ids[], size_t count) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    for (size_t i = 0; i < count; i++) {
        id_set_.insert(scheduler->GetTaskId(task_ids[i]));
    }
}

ConcurrencyChecker::ConcurrencyChecker() {
}

void ConcurrencyChecker::Check() {
    Task *current = Task::Running();
    assert(current != NULL);
    assert(id_set_.count(current->GetTaskId()) > 0);
}

void ConcurrencyChecker::CheckIfMainThr() {
    Task *current = Task::Running();
    assert(current == NULL);
}

class ConcurrencyScope::ScopeTask : public Task {
public:
    ScopeTask(int task_id) : Task(task_id) {
    };
    bool Run() {
        return false;
    }
    std::string Description() const { return "ConcurrencyScope::ScopeTask"; }
    ~ScopeTask() {
    }
};

ConcurrencyScope::ConcurrencyScope(int task_id) {
    SetRunningTask(task_id);
}

ConcurrencyScope::ConcurrencyScope(const std::string &name) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    SetRunningTask(scheduler->GetTaskId(name));
}

void ConcurrencyScope::SetRunningTask(int task_id) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    unit_test_task_.reset(new ScopeTask(task_id));
    scheduler->SetRunningTask(unit_test_task_.get());
}

ConcurrencyScope::~ConcurrencyScope() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->ClearRunningTask();
}
