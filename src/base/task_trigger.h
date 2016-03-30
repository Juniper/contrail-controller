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
    // For Test only
    void set_disable() {
        bool current = disabled_.fetch_and_store(true);
        assert(!current);
    }
    void set_enable() {
        bool current = disabled_.fetch_and_store(false);
        assert(current);
        Set();
    }
    bool disabled() {
        return disabled_;
    }

    // For Test only
    void set_deferred() {
        bool current = deferred_.fetch_and_store(true);
        assert(!current);
    }
    void clear_deferred() {
        bool current = deferred_.fetch_and_store(false);
        assert(current);
    }
    bool deferred() const { return deferred_; }
    bool IsSet() const { return trigger_; }

private:
    class WorkerTask;

    FunctionPtr func_;
    int task_id_;
    int task_instance_;
    tbb::atomic<bool> trigger_;
    tbb::atomic<bool> disabled_;
    tbb::atomic<bool> deferred_;
};

#endif /* defined(__ctrlplane__task_trigger__) */
