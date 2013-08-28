/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __EVENT_MANAGER_TEST_H__
#define __EVENT_MANAGER_TEST_H__

#include "io/event_manager.h"

#include <boost/scoped_ptr.hpp>
#include <pthread.h>
#include <tbb/atomic.h>

#include "base/logging.h"
#include "base/task.h"

class ServerThread {
public:
    explicit ServerThread(EventManager *evm)
    : thread_id_(pthread_self()), evm_(evm), tbb_scheduler_(NULL) {
    }
    void Run() {
        tbb_scheduler_.reset(
            new tbb::task_scheduler_init(TaskScheduler::GetThreadCount() + 1));
        running_ = true;
        evm_->Run();
        running_ = false;
        tbb_scheduler_->terminate();
    }
    static void *ThreadRun(void *objp) {
        ServerThread *obj = reinterpret_cast<ServerThread *>(objp);
        obj->Run();
        return NULL;
    }
    void Start() {
        int res = pthread_create(&thread_id_, NULL, &ThreadRun, this);
        assert(res == 0);
    }
    void Join() {
        int res = pthread_join(thread_id_, NULL);
        assert(res == 0);
    }
    
private:
    pthread_t thread_id_;
    tbb::atomic<bool> running_;
    EventManager *evm_;
    boost::scoped_ptr<tbb::task_scheduler_init> tbb_scheduler_;
};


#endif
