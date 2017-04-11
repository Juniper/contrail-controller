/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BASE_TASK_ANNOTATIONS_H_
#define SRC_BASE_TASK_ANNOTATIONS_H_

#include <stdlib.h>
#include <set>
#include <string>

#include <boost/scoped_ptr.hpp>

#include "base/task.h"
class ConcurrencyChecker {
public:
    static bool enable_;
    ConcurrencyChecker();
    ConcurrencyChecker(const char *task_ids[], size_t count);
    void Check();
    void CheckIfMainThr();
    static bool IsInMainThr() { return (Task::Running() == NULL); }
private:
    typedef std::set<int> TaskIdSet;
    TaskIdSet id_set_;
};

class ConcurrencyScope {
public:
    explicit ConcurrencyScope(int task_id);
    explicit ConcurrencyScope(const std::string &task_id);
    ~ConcurrencyScope();
private:
    class ScopeTask;
    void SetRunningTask(int task_id);
    boost::scoped_ptr<ScopeTask> unit_test_task_;
};

#define CHECK_CONCURRENCY(...)                            \
    do {                                                  \
        if (!ConcurrencyChecker::enable_) break;          \
        const char *_X_array[] = { __VA_ARGS__ };         \
        ConcurrencyChecker checker(                       \
            _X_array, sizeof(_X_array) / sizeof(char *)); \
        checker.Check();                                  \
    } while (0)

#define CHECK_CONCURRENCY_MAIN_THR()                      \
    do {                                                  \
        if (!ConcurrencyChecker::enable_) break;          \
        ConcurrencyChecker checker;                       \
        checker.CheckIfMainThr();                         \
    } while (0)

#endif  // SRC_BASE_TASK_ANNOTATIONS_H_
