/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/timer.h"

class Timer::TimerTask : public Task {
public:
    TimerTask(TimerPtr timer, boost::system::error_code ec)
        : Task(timer->task_id_, timer->task_instance_), timer_(timer), ec_(ec) {
    }

    virtual ~TimerTask() {
    }

    // Invokes user callback.
    // Timer could have been cancelled or delete when task was enqueued
    virtual bool Run() {
        {
            tbb::mutex::scoped_lock lock(timer_->mutex_);

            // cancelled task .. ignore
            if (task_cancelled()) {
                // Cancelled timer's task releases the ownership of the timer
                timer_ = NULL;
                return true;
            }

            // Conditions to invoke user callback met. Fire it
            timer_->SetState(Timer::Fired);
        }

        bool restart = false;

        // TODO: Is this error needed by user?
        if (ec_ && !timer_->error_handler_.empty()) {
            timer_->error_handler_(timer_->name_,
                                   std::string(ec_.category().name()),
                                   ec_.message());
        } else {
            restart = timer_->handler_();
        }

        OnTaskCancel();

        if (restart) {
            timer_->Start(timer_->time_, timer_->handler_,
                          timer_->error_handler_);
        }
        return true;
    }

    // Task Cancelled/Destroyed when it was Fired.
    void OnTaskCancel() {
        if (!timer_) {
            return;
        }
        tbb::mutex::scoped_lock lock(timer_->mutex_);

        if (timer_->timer_task_ != this) {
            assert(!timer_->timer_task_);
        }

        timer_->timer_task_ = NULL;
        timer_->SetState(Timer::Init);
    }

private:
    TimerPtr timer_;
    boost::system::error_code ec_;
    DISALLOW_COPY_AND_ASSIGN(TimerTask);
};

Timer::Timer(boost::asio::io_service &service, const std::string &name,
          int task_id, int task_instance)
    : boost::asio::monotonic_deadline_timer(service), name_(name), handler_(NULL),
    error_handler_(NULL), state_(Init), timer_task_(NULL), time_(0),
    task_id_(task_id), task_instance_(task_instance), seq_no_(0) {
    refcount_ = 0;
}

Timer::~Timer() {
    assert(state_ != Running && state_ != Fired);
}

//
// Start a timer
//
// If the timer is already running, return silently
//
bool Timer::Start(int time, Handler handler, ErrorHandler error_handler) {
    tbb::mutex::scoped_lock lock(mutex_);

    if (time < 0) {
        return true;
    }

    if (state_ == Running || state_ == Fired) {
        return true;
    }

    // Restart the timer
    handler_ = handler;
    seq_no_++;
    error_handler_ = error_handler;
    boost::system::error_code ec;
    expires_from_now(boost::posix_time::milliseconds(time), ec);
    if (ec) {
        return false;
    }

    SetState(Running);
    async_wait(boost::bind(Timer::StartTimerTask, this, TimerPtr(this), time, 
                           seq_no_, boost::asio::placeholders::error));
    return true;
}

// Cancel a running timer
bool Timer::Cancel() {
    tbb::mutex::scoped_lock lock(mutex_);

    // A fired timer cannot be cancelled
    if (state_ == Fired) {
        return false;
    }

    // Cancel Task. If Task cancel succeeds, there will be no callback.
    // Reset TaskRef if call succeeds.
    if (timer_task_) {
        TaskScheduler::CancelReturnCode rc = 
            TaskScheduler::GetInstance()->Cancel(timer_task_);
        assert(rc != TaskScheduler::FAILED);
        timer_task_ = NULL;
    }

    SetState(Cancelled);
    return true;
}

// ASIO callback on timer expiry. Start a task to serve the timer
void Timer::StartTimerTask(boost::asio::monotonic_deadline_timer* t, TimerPtr timer,
                           int time, uint32_t seq_no, 
                           const boost::system::error_code &ec) {
    tbb::mutex::scoped_lock lock(timer->mutex_);

    if (timer->state_ == Cancelled) {
        return;
    }

    // If timer was cancelled, no callback is invoked
    if (ec && ec.value() == boost::asio::error::operation_aborted) {
        return;
    }

    // Timer could have fired for previous run. Validate the seq_no_
    if (timer->seq_no_ != seq_no) {
        return;
    }
    // Start a task and add Task reference.
    assert(timer->timer_task_ == NULL);
    timer->timer_task_ = new TimerTask(timer, ec);
    timer->time_ = time;
    TaskScheduler::GetInstance()->Enqueue(timer->timer_task_);
}

//
// TimerManager class routines
//
TimerManager::TimerSet TimerManager::timer_ref_;
tbb::mutex TimerManager::mutex_;

Timer *TimerManager::CreateTimer(
            boost::asio::io_service &service, const std::string &name,
            int task_id, int task_instance) {
    Timer *timer = new Timer(service, name, task_id, task_instance);
    AddTimer(timer);
    return timer;
}

void TimerManager::AddTimer(Timer *timer) {
    tbb::mutex::scoped_lock lock(mutex_);
    timer_ref_.insert(TimerPtr(timer));

    return;
}

//
// Delete a timer object from the data base, by removing the intrusive
// reference. If any other objects has a reference to this timer such as
// boost::asio, the timer object deletion is automatically deferred
//
bool TimerManager::DeleteTimer(Timer *timer) {
    if (!timer || timer->fired()) return false;

    timer->Cancel();
    tbb::mutex::scoped_lock lock(mutex_);
    timer_ref_.erase(TimerPtr(timer));

    return true;
}

