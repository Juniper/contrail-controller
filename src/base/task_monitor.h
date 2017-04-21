/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BASE_TASK_MONITOR_H__
#define __BASE_TASK_MONITOR_H__

/****************************************************************************
 * Monitor module for TBB.
 * It is observed that TBB randomly goes into a state where no tasks are
 * scheduled. The monitor here tracks the tasks enqueued and executed.
 *
 * If the monitor identifes that TBB is in a lockup state, then it will
 * assert the process
 ****************************************************************************/
#include <boost/asio/placeholders.hpp>
#include <boost/system/error_code.hpp>

#include "util.h"

class TaskScheduler;
class TimerImpl;
class EventManager;

class TaskMonitor {
public:
    TaskMonitor(TaskScheduler *scheduler, uint64_t tbb_keepawake_time_msec,
                uint64_t inactivity_time_msec, uint64_t poll_interval_msec);
    ~TaskMonitor();

    void Start(EventManager *evm);
    void Terminate();
    void Run(const boost::system::error_code &ec);

    uint64_t last_activity() const { return last_activity_; }
    uint64_t last_enqueue_count() const { return last_enqueue_count_; }
    uint64_t last_done_count() const { return last_done_count_; }

    uint64_t inactivity_time_usec() const { return inactivity_time_usec_; }
    uint64_t inactivity_time_msec() const { return inactivity_time_usec_/1000; }
    uint64_t tbb_keepawake_time_msec() const { return tbb_keepawake_time_msec_;}
    uint64_t poll_interval_msec() const { return poll_interval_msec_; }
    uint64_t poll_count() const { return poll_count_; }
private:
    friend class TaskMonitorTest;

    void UpdateTimers();
    void Restart();
    bool Monitor(uint64_t t, uint64_t enqueue_count, uint64_t done_count);

    TaskScheduler *scheduler_;
    // Monitor terminated
    bool cancelled_;
    // ASIO Timer implementation class
    std::auto_ptr<TimerImpl> timer_impl_;

    // inactivity in usec after which monitor must invoke assert()
    uint64_t inactivity_time_usec_;
    // interval at which tbb must be monitored
    uint64_t poll_interval_msec_;
    // TBB keepawake time
    uint64_t tbb_keepawake_time_msec_;
    // time of last activity seen
    uint64_t last_activity_;
    // TaskScheduler->enqueue_count() at last_change_
    uint64_t last_enqueue_count_;
    // TaskScheduler->done_count() at last_change_
    uint64_t last_done_count_;
    // Number of times monitor is run
    uint64_t poll_count_;
    DISALLOW_COPY_AND_ASSIGN(TaskMonitor);
};

#endif  //  __BASE_TASK_MONITOR_H__
