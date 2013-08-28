/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BASE__TASK_ANNOTATIONS_H__
#define __BASE__TASK_ANNOTATIONS_H__

#include <stdlib.h>
#include <set>

#include <boost/scoped_ptr.hpp>

#include "base/task.h"
class ConcurrencyChecker {
public:
    static bool disable_;
    ConcurrencyChecker(const char *task_ids[], size_t count);
    void Check();
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

#if defined(DEBUG)
#define CHECK_CONCURRENCY(...)                            \
    do {                                                  \
        if (ConcurrencyChecker::disable_) break;          \
        const char *_X_array[] = { __VA_ARGS__ };         \
        ConcurrencyChecker checker(                       \
            _X_array, sizeof(_X_array) / sizeof(char *)); \
        checker.Check();                                  \
    } while (0)
#else
#define CHECK_CONCURRENCY(...)
#endif

#endif
