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

    enum Event {
        CollectStats,
    };

    StatsCollector(uint32_t instance, boost::asio::io_service &io, 
                   int exp, std::string timer_name) : run_counter_(0),
                   work_queue_(TaskScheduler::GetInstance()->GetTaskId(
                       "Agent::StatsCollector"), instance, 
                       boost::bind(&StatsCollector::HandleMsg, this, _1)),
                   timer_(NULL), expiry_time_(exp) {
        timer_ = TimerManager::CreateTimer(io, timer_name.c_str());
        timer_->Start(expiry_time_, 
                      boost::bind(&StatsCollector::TimerExpiry, this));
    };

    virtual ~StatsCollector() { 
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
    }

    virtual bool Run() = 0;

    void StartTimer() {
        timer_->Cancel();
        timer_->Start(expiry_time_, 
                      boost::bind(&StatsCollector::TimerExpiry, this));
    }
    int run_counter_; //used only in UT code
private:
    bool TimerExpiry() {
        return work_queue_.Enqueue(CollectStats);
        /* Return false to suppress auto-restart of timer */
        return false;
    }

    bool HandleMsg(Event event) {
        switch(event) {
        case StatsCollector::CollectStats:
            Run();
            StartTimer();
            break;

        default:
            break;
        }
        return true;
    }

    WorkQueue<Event> work_queue_;
    Timer *timer_;
    int expiry_time_;
    DISALLOW_COPY_AND_ASSIGN(StatsCollector);
};

#endif // vnsw_agent_stats_coll_h_
