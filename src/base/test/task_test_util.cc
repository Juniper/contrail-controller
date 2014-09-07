/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/test/task_test_util.h"

#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "base/task.h"
#include "io/event_manager.h"
#include "testing/gunit.h"

using namespace boost::posix_time;

namespace task_util {

//
// Wait for the scheduler to become idle. Default Timeout is 30 seconds
//
// Use environment variable WAIT_FOR_IDLE to tune the value appropriately
// based on the test load and the running environment (pprof, valgrind, etc.)
//
void WaitForIdle(long wait_seconds, bool running_only) {
    static const long kTimeoutUsecs = 1000;
    static long envWaitTime;

    if (!envWaitTime) {
        if (getenv("WAIT_FOR_IDLE")) {
            envWaitTime = atoi(getenv("WAIT_FOR_IDLE"));
        } else {
            envWaitTime = wait_seconds;
        }
    }

    if (envWaitTime > wait_seconds) wait_seconds = envWaitTime;

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    for (long i = 0; i < ((wait_seconds * 1000000)/kTimeoutUsecs); i++) {
        if (scheduler->IsEmpty(running_only)) {
            return;
        }
        usleep(kTimeoutUsecs);
    }
    EXPECT_TRUE(scheduler->IsEmpty(running_only));
}

static void TimeoutHandler(const boost::system::error_code &error) {
}

void WaitForCondition(EventManager *evm, boost::function<bool(void)> condition,
                      const int timeout) {
    ptime start(microsec_clock::universal_time());
    boost::asio::deadline_timer timer(*evm->io_service());

    while (true) {
        ptime now(microsec_clock::universal_time());
        time_duration elapsed = now - start;
        if (elapsed.seconds() > timeout) {
            break;
        }
        TaskScheduler *scheduler = TaskScheduler::GetInstance();

        if (scheduler->IsEmpty()) {
            if ((condition)()) {
                break;
            }
            int deadline = timeout - elapsed.seconds();
            if (deadline <= 0) {
                deadline = 1;
            }
            timer.expires_from_now(seconds(deadline));
        } else {
            timer.expires_from_now(milliseconds(1));
        }
        timer.async_wait(TimeoutHandler);
        evm->RunOnce();
    }
}

void BusyWork(EventManager *evm, const int timeout) {
    ptime start(microsec_clock::universal_time());
    boost::asio::deadline_timer timer(*evm->io_service());
    while (true) {
        ptime now(microsec_clock::universal_time());
        time_duration elapsed = now - start;
        if (elapsed.seconds() > timeout) {
            break;
        }
        timer.expires_from_now(milliseconds(5));
        timer.async_wait(TimeoutHandler);
        evm->RunOnce();
    }
}

void TaskSchedulerStop() {
    TaskScheduler::GetInstance()->Stop();
}

void TaskSchedulerStart() {
    TaskScheduler::GetInstance()->Start();
}

TaskSchedulerLock::TaskSchedulerLock() {
    TaskScheduler::GetInstance()->Stop();
    WaitForIdle(30, true);
}

TaskSchedulerLock::~TaskSchedulerLock() {
    TaskScheduler::GetInstance()->Start();
}

}  // namespace task_util
