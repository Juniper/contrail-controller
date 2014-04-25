/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

//  Timer implementation using ASIO and Task infrastructure. 
//  Registers an ASIO timer. On ASIO timer expiry, a task will be created to
//  run the timer. Supports user specified task-id.
//
//  Operations supported
//  - Create a timer by allocating an object of type Timer
//  - Start a timer
//    If a timer is already running, it is a no op. In order to auto-restart
//    a timer, return 'tru'e from timer callback, false otherwise
//
//    There can be atmost one "Task" outstanding for the timer.
//
//  - Cancel a timer
//    Cancels a running timer. Both ASIO registerations and task spawned will
//    be cancelled.
//
//    If timer is already fired, "Cancel" api will fail and return 'false'
//
//  - Delete a timer
//    Cancels the timer and triggers deletion of the timer. Application should
//    not access the timer after its deleted
//
//  Concurrency aspects:
//  - Timer is allocated by application
//  - Applications must call TimerManager::DeleteTimer() to delete the timer
//  - All operations on timer are protected by mutex
//  - When timer is running, it can have references from ASIO and Task. 
//    Timer class will keep of reference from ASIO and Task. Timer will
//    be deleted when both the references go away. (via intrusive pointer)
//

#ifndef TIMER_H_
#define TIMER_H_

#include <tbb/mutex.h>

#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>
#include <set>
#include <boost/asio/monotonic_deadline_timer.hpp>

#include <base/task.h>

class Timer : public boost::asio::monotonic_deadline_timer {
private:
	// Task used to fire the timer
    class TimerTask;

public:
    typedef boost::function<bool(void)> Handler;
    typedef boost::function<void(std::string, std::string, std::string)>
        ErrorHandler;

    Timer(boost::asio::io_service &service, const std::string &name,
          int task_id, int task_instance, bool delete_on_completion = false);
    virtual ~Timer();

    // Start a timer
    // If timer already started, try cancelling and restart
    //
    // Timer callback routine handler should return true to start the timer
    // again, false otherwise
    //
    // XXX Timer cannot be Start()ed from timer callback routine itself.
    // Return true from the callback in order to post the timer again. This
    // would use the same time, the timer was initially started with
    //
    bool Start(int time, Handler handler, ErrorHandler error_handler = NULL);

    //Can be called only from callback
    bool Reschedule(int time);

    // Cancel a running timer
    bool Cancel();

    bool running() const { 
        tbb::mutex::scoped_lock lock(mutex_);
        return (state_ == Running);
    }

    bool fired() const { 
        tbb::mutex::scoped_lock lock(mutex_);
        return (state_ == Fired);
    }

    bool cancelled() const {
        tbb::mutex::scoped_lock lock(mutex_);
        return (state_ == Cancelled); 
    }

    bool IsDeleteOnCompletion() const {
        return delete_on_completion_;
    }

    // Only for state machine test
    // XXX: Don't use in production code
    void Fire() { 
        tbb::mutex::scoped_lock lock(mutex_);
        if (handler_ && !handler_.empty()) {
            SetState(Fired);
            handler_();
            SetState(Init);
        }
    }

private:
    friend class TimerManager;
    friend class TimerTest;

    friend void intrusive_ptr_add_ref(Timer *timer);
    friend void intrusive_ptr_release(Timer *timer);
    typedef boost::intrusive_ptr<Timer> TimerPtr;

    enum TimerState {
        Init            = 0,
        Running         = 1,
        Fired           = 2,
        Cancelled       = 3,
    };

    // ASIO callback on timer expiry. Start a task to serve the timer
    static void StartTimerTask(boost::asio::monotonic_deadline_timer* t, TimerPtr t_ptr,
                               int time, uint32_t seq_no,
                               const boost::system::error_code &ec);

    void SetState(TimerState s) { state_ = s; }
    static int GetTimerInstanceId() { return -1; }
    static int GetTimerTaskId() {
        static int timer_task_id = -1;

        if (timer_task_id == -1) {
            TaskScheduler *scheduler = TaskScheduler::GetInstance();
            timer_task_id = scheduler->GetTaskId("timer::TimerTask");
        }
        return timer_task_id;
    }

    std::string name_;
    Handler handler_;
    ErrorHandler error_handler_;
    mutable tbb::mutex mutex_;
    TimerState state_;
    TimerTask *timer_task_;
    int time_;
    int task_id_;
    int task_instance_;
    uint32_t seq_no_;
    bool delete_on_completion_;
    tbb::atomic<int> refcount_;
};

inline void intrusive_ptr_add_ref(Timer *timer) {
    timer->refcount_.fetch_and_increment();
}

inline void intrusive_ptr_release(Timer *timer) {
    int prev = timer->refcount_.fetch_and_decrement();
    if (prev == 1) {
        delete timer;
    }
}

//
// TimerManager is the place holder for all the Timer objects
// instantiated in the life time of a process
//
// Timer objects are help in TimerSet until all the cleanup is complete
// and only then should they be deleted via DeleteTimer() API
//
// Since Timer objects are also held by boost::asio routines, they are
// protected using intrusive pointers
// 
// This is similar to how TcpSession objects are managed via TcpSessionPtr
//
class TimerManager {
public:
    static Timer *CreateTimer(boost::asio::io_service &service,
                              const std::string &name,
                              int task_id = Timer::GetTimerTaskId(),
                              int task_instance = Timer::GetTimerInstanceId(),
                              bool delete_on_completion = false);
    static bool DeleteTimer(Timer *Timer);

private:
    friend class TimerTest;

    typedef boost::intrusive_ptr<Timer> TimerPtr;
    struct TimerPtrCmp {
        bool operator()(const TimerPtr &lhs,
                        const TimerPtr &rhs) const {
            return lhs.get() < rhs.get();
        }
    };
    typedef std::set<TimerPtr, TimerPtrCmp> TimerSet;
    static void AddTimer(Timer *Timer);

    static tbb::mutex mutex_;
    static TimerSet timer_ref_;
};

#endif /* TIMER_H_ */
