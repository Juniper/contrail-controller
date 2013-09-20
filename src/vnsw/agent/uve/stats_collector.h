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
    static const uint32_t stats_coll_time = (2000); // time in milliseconds

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
                   expiry_time_(exp) {
        timer_->Start(expiry_time_, 
                      boost::bind(&StatsCollector::TimerExpiry, this));
    };

    virtual ~StatsCollector() { 
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
    }

    virtual bool Run() = 0;

    int run_counter_; //used only in UT code
private:
    bool TimerExpiry() {
        Run();
        /* Return true to request auto-restart of timer */
        return true;
    }

    Timer *timer_;
    int expiry_time_;
    DISALLOW_COPY_AND_ASSIGN(StatsCollector);
};

#endif // vnsw_agent_stats_coll_h_
