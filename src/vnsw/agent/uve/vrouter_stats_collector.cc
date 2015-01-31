/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <db/db.h>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <uve/stats_collector.h>
#include <uve/vrouter_stats_collector.h>

VrouterStatsCollector::VrouterStatsCollector(boost::asio::io_service &io,
                                             AgentUveBase *uve) :
    StatsCollector(TaskScheduler::GetInstance()->GetTaskId("Agent::Uve"),
                   0, io, uve->agent()->params()->vrouter_stats_interval(),
                   "Vrouter stats collector"),
    agent_uve_(uve) {
}

VrouterStatsCollector::~VrouterStatsCollector() {
}

bool VrouterStatsCollector::Run() {
    run_counter_++;
    agent_uve_->vrouter_uve_entry()->SendVrouterMsg();
    /* We have not registered for ACL notification. So total_acl_rules
     * of VN is periodically polled from VN ACL and VN UVE is sent if there
     * are any changes.
     */
    agent_uve_->vn_uve_table()->SendVnAclRuleCount();
    return true;
}

void VrouterStatsCollector::Shutdown() {
    StatsCollector::Shutdown();
}
