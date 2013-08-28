/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_stats_coll_h_
#define vnsw_agent_stats_coll_h_

#include <boost/asio.hpp>
#include <map>
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
                   int exp = stats_coll_time) : run_counter_(0),
                   work_queue_(TaskScheduler::GetInstance()->GetTaskId(
                       "Agent::StatsCollector"), instance, 
                       boost::bind(&StatsCollector::HandleMsg, this, _1)),
                   timer_(io), expiry_time_(exp) {
        StartTimer();
    };

    virtual ~StatsCollector() { 
        boost::system::error_code ec;
        timer_.cancel(ec);
        assert(ec.value() == 0);
    }

    virtual bool Run() = 0;

    void StartTimer() {
        boost::system::error_code ec;
        timer_.expires_from_now(boost::posix_time::millisec(expiry_time_), ec);
        assert(ec.value() == 0);
        timer_.async_wait(boost::bind(&StatsCollector::TimerExpiry, this,
                                      boost::asio::placeholders::error));
    }
    int run_counter_; //used only in UT code
private:
    bool EnqueueMessage(Event event) {
        return work_queue_.Enqueue(event);
    };

    void TimerExpiry(const boost::system::error_code &ec) {
        if (!ec)
            SendTimerMsg();
        else {
            LOG(ERROR, "StatsCollector : timer error " << ec);
            if (ec != boost::system::errc::operation_canceled) {
                StartTimer();
            }
        }
    }

    void SendTimerMsg() {
        if (!EnqueueMessage(StatsCollector::CollectStats)) {
            LOG(ERROR, "StatsCollector : failed to enqueue; message dropped");
        }
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
    // TODO: This should use base/timer.h
    boost::asio::deadline_timer timer_;
    static tbb::atomic<bool> shutdown_;
    int expiry_time_;
    DISALLOW_COPY_AND_ASSIGN(StatsCollector);
};

#endif // vnsw_agent_stats_coll_h_
