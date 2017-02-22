#include "base/task.h"
#include "base/timer.h"
#include <boost/bind.hpp>
#include "io/event_manager.h"

class TaskTbbKeepAwake {
public:
    TaskTbbKeepAwake() : tbb_awake_count_(0), tbb_awake_val_(0),
                         timeout_changed_(false), tbb_awake_timer_(NULL) { }

    bool StartTbbKeepAwakeTask(TaskScheduler *ts, EventManager *event_mgr,
                               const std::string task_name,
                               uint32_t tbbKeepawakeTimeout = 1000) {
        uint32_t task_id = ts->GetTaskId(task_name);
        tbb_awake_timer_ = TimerManager::CreateTimer(*event_mgr->io_service(),
                                                     "TBB Keep Awake",
                                                     task_id, 0);
        tbb_awake_val_ = tbbKeepawakeTimeout;
        bool ret = tbb_awake_timer_->Start(tbbKeepawakeTimeout,
                       boost::bind(&TaskTbbKeepAwake::TbbKeepAwake, this));
        return ret;
    }

    void ModifyTbbKeepAwakeTimeout(uint32_t timeout) {
        tbb::mutex::scoped_lock lock(mutex_);
        if (tbb_awake_val_ != timeout) {
            timeout_changed_ = true;
            tbb_awake_val_ = timeout;
        }
    }

    bool TbbKeepAwake() {
        tbb_awake_count_++;
        if (timeout_changed_) {
            tbb_awake_timer_->Reschedule(tbb_awake_val_);
            timeout_changed_ = false;
        }
        return true;
    }

    void ShutTbbKeepAwakeTask() {
        if (tbb_awake_timer_) {
            tbb_awake_timer_->Cancel();
            TimerManager::DeleteTimer(tbb_awake_timer_);
        }
    }

private:
    uint64_t tbb_awake_count_;
    uint32_t tbb_awake_val_;
    bool timeout_changed_;
    Timer *tbb_awake_timer_;
    tbb::mutex mutex_;
};
