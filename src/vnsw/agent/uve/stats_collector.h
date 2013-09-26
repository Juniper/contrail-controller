/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_stats_coll_h_
#define vnsw_agent_stats_coll_h_

#include <boost/asio.hpp>
#include <map>
#include "base/timer.h"
#include "base/queue_task.h"

class StatsCollector {
public:
    enum StatsInstance {
        FlowStatsCollector,
        AgentStatsCollector,
        VrouterStatsCollector,
    };

    StatsCollector(uint32_t instance, boost::asio::io_service &io, 
                   int exp, std::string timer_name) : run_counter_(0),
                   timer_(TimerManager::CreateTimer(io, timer_name.c_str(), 
                          TaskScheduler::GetInstance()->GetTaskId
                          ("Agent::StatsCollector"), instance)), 
                   timer_restart_trigger_(new TaskTrigger(
                          boost::bind(&StatsCollector::RestartTimer, this),
                          TaskScheduler::GetInstance()->GetTaskId
                          ("Agent::StatsCollector"), instance)), 
                   expiry_time_(exp) {
        timer_->Start(expiry_time_, 
                      boost::bind(&StatsCollector::TimerExpiry, this));
    };

    virtual ~StatsCollector() { 
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
        timer_restart_trigger_->Reset();
        delete timer_restart_trigger_;
    }

    virtual bool Run() = 0;
    int GetExpiryTime() const { return expiry_time_; }
    void SetExpiryTime(int time) {
        if (time != expiry_time_) {
            expiry_time_ = time;
            timer_restart_trigger_->Set();
        }
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
