/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_stats_coll_h_
#define vnsw_agent_stats_coll_h_

#include <boost/asio.hpp>
#include <map>
#include "base/timer.h"
#include "base/queue_task.h"

//The base class for statistics collector classes.
//Defines Timer functionality to trigger the stats collection events
class StatsCollector {
public:
    enum StatsInstance {
        FlowStatsCollector,
        AgentStatsCollector,
    };

    StatsCollector(int task_id, int32_t instance, boost::asio::io_service &io,
                   int exp, std::string timer_name) : run_counter_(0),
                   timer_(TimerManager::CreateTimer(io, timer_name.c_str(),
                          task_id, instance)),
                   timer_restart_trigger_(new TaskTrigger(
                          boost::bind(&StatsCollector::RestartTimer, this),
                          task_id, instance)),
                   expiry_time_(exp) {
        timer_->Start(expiry_time_,
                      boost::bind(&StatsCollector::TimerExpiry, this));
    };

    virtual ~StatsCollector() {
        Shutdown();
        timer_restart_trigger_->Reset();
        delete timer_restart_trigger_;
    }

    virtual bool Run() = 0;
    void Shutdown() {
        if (timer_) {
            timer_->Cancel();
            TimerManager::DeleteTimer(timer_);
            timer_ = NULL;
        }
    }

    // To be used by UT only
    void TestStartStopTimer (bool stop) {
        if (timer_ != NULL) {
            if (stop) {
                // UTs call TestStartStopTimer() without exclusion. Hence,
                // timer->Cancel() can potentially fail. Retry in that case
                int i = 0;
                for (; i < 8; i++) {
                    if (timer_->Cancel())
                        break;
                    usleep(1000);
                }
                assert(i < 8);
            } else {
                timer_->Start(expiry_time_,
                              boost::bind(&StatsCollector::TimerExpiry, this));
            }
        }
    }

    int expiry_time() const { return expiry_time_; }
    void set_expiry_time(int time) {
        if (time != expiry_time_) {
            expiry_time_ = time;
            timer_restart_trigger_->Set();
        }
    }

    void RescheduleTimer(int time) {
        timer_->Reschedule(time);
    }

    int run_counter_; //used only in UT code

private:
    bool RestartTimer() {
        timer_->Cancel();
        timer_->Start(expiry_time_,
                      boost::bind(&StatsCollector::TimerExpiry, this));
        return true;
    }
    bool TimerExpiry() {
        Run();
        /* Return true to request auto-restart of timer */
        return true;
    }

    Timer *timer_;
    TaskTrigger *timer_restart_trigger_;
    int expiry_time_;
    DISALLOW_COPY_AND_ASSIGN(StatsCollector);
};

#endif // vnsw_agent_stats_coll_h_
