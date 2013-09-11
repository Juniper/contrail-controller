/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrouter_stats_h
#define vnsw_agent_vrouter_stats_h

#include <cmn/agent_cmn.h>
#include <uve/uve_init.h>
#include <uve/stats_collector.h>

class VrouterStatsCollector : public StatsCollector {
public:
    VrouterStatsCollector(boost::asio::io_service &io) :
        StatsCollector(StatsCollector::VrouterStatsCollector, io, 
                       StatsCollector::stats_coll_time, 
                       "Vrouter stats collector") {}
    virtual ~VrouterStatsCollector() { };

    bool Run();
private:
    DISALLOW_COPY_AND_ASSIGN(VrouterStatsCollector);
};

#endif //vnsw_agent_vrouter_stats_h
