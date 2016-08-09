#include "base/task.h"
#include "base/timer.h"
#include <boost/bind.hpp>
#include "io/event_manager.h"

class TaskTbbKeepAwake {
public:
    TaskTbbKeepAwake() : tbb_awake_count_(0), tbb_awake_timer_(NULL) { }

    bool StartTbbKeepAwakeTask(TaskScheduler *ts, EventManager *event_mgr,
                               uint32_t tbbKeepawakeTimeout = 20) {
        uint32_t task_id = ts->GetTaskId("Task::TbbKeepAwake");
        tbb_awake_timer_ = TimerManager::CreateTimer(*event_mgr->io_service(),
                                                     "TBB Keep Awake",
                                                     task_id, 0);

        bool ret = tbb_awake_timer_->Start(tbbKeepawakeTimeout,
                       boost::bind(&TaskTbbKeepAwake::TbbKeepAwake, this));
        return ret;
    }

    bool TbbKeepAwake() {
        tbb_awake_count_++;
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
    Timer *tbb_awake_timer_;
};
