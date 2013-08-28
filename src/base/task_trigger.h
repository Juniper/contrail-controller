/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__task_trigger__
#define __ctrlplane__task_trigger__

#include <boost/function.hpp>
#include <tbb/atomic.h>

class TaskTrigger {
public:
    typedef boost::function<bool(void)> FunctionPtr;
    TaskTrigger(FunctionPtr func, int task_id, int task_instance);
    ~TaskTrigger();
    void Set();
    void Reset();

private:
    class WorkerTask;

    FunctionPtr func_;
    int task_id_;
    int task_instance_;
    tbb::atomic<bool> trigger_;
};

#endif /* defined(__ctrlplane__task_trigger__) */
