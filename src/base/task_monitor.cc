/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#include <boost/bind.hpp>
#include "logging.h"
#include "io/event_manager.h"
#include "timer_impl.h"
#include "time_util.h"
#include "task.h"
#include "task_monitor.h"

#define kPollIntervalMultiplier 2
#define kInactivityMultiplier 50

TaskMonitor::TaskMonitor(TaskScheduler *scheduler,
                         uint64_t tbb_keepawake_time_msec,
                         uint64_t inactivity_time_msec,
                         uint64_t poll_interval_msec) :
    scheduler_(scheduler), cancelled_(false), timer_impl_(NULL),
    inactivity_time_usec_(inactivity_time_msec * 1000),
    poll_interval_msec_(poll_interval_msec),
    tbb_keepawake_time_msec_(tbb_keepawake_time_msec),
    last_activity_(ClockMonotonicUsec()),
    last_enqueue_count_(0), last_done_count_(0), poll_count_(0) {
}

TaskMonitor::~TaskMonitor() {
}

void TaskMonitor::UpdateTimers() {
    // Ensure polling interval for monitor is atleast 2*keep-awake-time
    // It ensures when monitor is invoked, there is some job enqueued
    // and executed
    if ((tbb_keepawake_time_msec_ * kPollIntervalMultiplier) > poll_interval_msec_) {
        poll_interval_msec_ =
            kPollIntervalMultiplier * tbb_keepawake_time_msec_;
    }

    // Ensure monitor timeout is atleast 50 * poll-interval
    if ((poll_interval_msec_ * kInactivityMultiplier * 1000) >
        inactivity_time_usec_) {
        inactivity_time_usec_ =
            poll_interval_msec_ * kInactivityMultiplier * 1000;
    }
    return;
}

void TaskMonitor::Start(EventManager *evm) {
    if (inactivity_time_usec_ == 0 || poll_interval_msec_ == 0)
        return;

    UpdateTimers();
    timer_impl_.reset(new TimerImpl(*evm->io_service()));
    Restart();
    return;
}

void TaskMonitor::Terminate() {
    cancelled_ = true;
}

void TaskMonitor::Restart() {
    boost::system::error_code ec;
    timer_impl_->expires_from_now(poll_interval_msec_, ec);
    if (ec) {
        assert(0);
    }
    timer_impl_->async_wait(boost::bind(&TaskMonitor::Run, this,
                                        boost::asio::placeholders::error));
}

bool TaskMonitor::Monitor(uint64_t t, uint64_t enqueue_count,
                          uint64_t done_count) {
    // New tasks were spawned by TBB. Treat as activity seen
    if (done_count != last_done_count_) {
        last_done_count_ = done_count;
        last_enqueue_count_ = enqueue_count;
        last_activity_ = t;
        return true;
    }

    // No change in done_count_. Now validate enqueue_count.
    // Note: We cannot match enqueue_count_ and done_count_. Both these numbers
    // are updated by multiple threads and are not atomic numbers. As a result
    // they can potentially be out-of-sync
    //
    // If no new tasks are enqueued, then assume there is no more tasks
    // to run. Treat it similar to seeing activity
    if (enqueue_count == last_enqueue_count_) {
        last_done_count_ = done_count;
        last_enqueue_count_ = enqueue_count;
        last_activity_ = t;
        return true;
    }

    // Enqueues are happening, check if inactivity exceeds configured time
    return ((t - last_activity_) <= inactivity_time_usec_);
}

void TaskMonitor::Run(const boost::system::error_code &ec) {
    poll_count_++;

    // ASIO API aborted. Just restart ASIO timer again
    if (ec && ec.value() == boost::asio::error::operation_aborted) {
        Restart();
        return;
    }

    if (cancelled_)
        return;

    if (Monitor(ClockMonotonicUsec(), scheduler_->enqueue_count(),
                scheduler_->done_count())) {
        Restart();
        return;
    }
    LOG(ERROR, "!!!! ERROR !!!! Task Monitor failed");
    assert(0);
}
