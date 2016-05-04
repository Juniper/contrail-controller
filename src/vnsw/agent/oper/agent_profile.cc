/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>
#include <vnc_cfg_types.h>
#include <base/util.h>

#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include <oper/agent_profile_types.h>
#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <oper/operdb_init.h>
#include <oper/agent_profile.h>

#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <filter/acl.h>
#include "db/db.h"

using namespace std;

AgentProfile::AgentProfile(Agent *agent, bool enable) :
    agent_(agent), timer_(NULL), enable_(enable) {

    TaskScheduler *task = TaskScheduler::GetInstance();
    timer_ = TimerManager::CreateTimer
        (*(agent_->event_manager())->io_service(), "Agent Profile",
         task->GetTaskId("agent_profile"), 0);
    if (enable) {
        timer_->Start(kProfileTimeout, boost::bind(&AgentProfile::TimerRun,
                                                   this));
    }
    time(&start_time_);
}

AgentProfile::~AgentProfile() {
    TimerManager::DeleteTimer(timer_);
}

bool AgentProfile::TimerRun() {
    ProfileData *data = GetLastProfileData();
    data->Get(agent_);
    if (pkt_flow_stats_cb_.empty() == false) {
        pkt_flow_stats_cb_(data);
    }
    Log();
    return true;
}

string GetProfileString(DBTable *table, const char *name) {
    stringstream str;
    str << setw(16) << name
        << " Size " << setw(6) << table->Size()
        << " Enqueue " << setw(6) << table->enqueue_count()
        << " Input " << setw(6) << table->input_count()
        << " Notify " << setw(6) << table->notify_count();
    return str.str();
}

void AgentProfile::Log() {
}

ProfileData *AgentProfile::GetLastProfileData() {
    uint16_t index = seconds_history_index_ % kSecondsHistoryCount;
    seconds_history_index_++;
    return &seconds_history_data_[index];
}

ProfileData *AgentProfile::GetProfileData(uint16_t index) {
    return &seconds_history_data_[index];
}
//////////////////////////////////////////////////////////////////////////////
// ProfileData collection routines
//////////////////////////////////////////////////////////////////////////////
void ProfileData::DBTableStats::Reset() {
    db_entry_count_ = 0;
    walker_count_ = 0;
    enqueue_count_ = 0;
    input_count_ = 0;
    notify_count_ = 0;
}

void ProfileData::DBTableStats::Get(const DBTable *table) {
    db_entry_count_ = table->Size();
    walker_count_ = table->walker_count();
    enqueue_count_ = table->enqueue_count();
    input_count_ = table->input_count();
    notify_count_ = table->notify_count();
}

void ProfileData::DBTableStats::Accumulate(const DBTableBase *table) {
    db_entry_count_ += table->Size();
    walker_count_ += table->walker_count();
    enqueue_count_ += table->enqueue_count();
    input_count_ += table->input_count();
    notify_count_ += table->notify_count();
}

void ProfileData::Get(Agent *agent) {
    std::ostringstream str;
    str << boost::posix_time::second_clock::local_time();
    time_ = str.str();

    DB::TableMap::const_iterator itr =
        agent->db()->const_begin();
    DB::TableMap::const_iterator itrend =
        agent->db()->const_end();

    profile_stats_table_.clear();
    for ( ;itr != itrend; ++itr) {
        if(itr->first.rfind(kV4UnicastRouteDbTableSuffix) !=
                   std::string::npos) {
           inet4_routes_.Accumulate(itr->second);
        } else if (itr->first.rfind(kV6UnicastRouteDbTableSuffix) !=
                   std::string::npos) {
           inet6_routes_.Accumulate(itr->second);
        } else if (itr->first.rfind(kL2RouteDbTableSuffix) !=
                   std::string::npos) {
           bridge_routes_.Accumulate(itr->second);
        } else if (itr->first.rfind(kMcastRouteDbTableSuffix) !=
                   std::string::npos) {
           multicast_routes_.Accumulate(itr->second);
        } else if (itr->first.rfind(kEvpnRouteDbTableSuffix) !=
                   std::string::npos) {
           evpn_routes_.Accumulate(itr->second);
        } else {
            ProfileData::DBTableStats stats;
            stats.Get(dynamic_cast<DBTable*>(itr->second));
            profile_stats_table_.insert(make_pair(itr->first,stats));
        }
    }

    TaskScheduler *sched = TaskScheduler::GetInstance();
    task_stats_[0] = *sched->GetTaskGroupStats(sched->GetTaskId("Agent::FlowHandler"));
    task_stats_[1] = *sched->GetTaskGroupStats(sched->GetTaskId("db::DBTable"));
    task_stats_[2] = *sched->GetTaskGroupStats(sched->GetTaskId("Agent::StatsCollector"));
    task_stats_[3] = *sched->GetTaskGroupStats(sched->GetTaskId("io::ReaderTask"));
    task_stats_[4] = *sched->GetTaskGroupStats(sched->GetTaskId("Agent::PktFlowResponder"));
    task_stats_[5] = *sched->GetTaskGroupStats(sched->GetTaskId("sandesh::RecvQueue"));
    task_stats_[6] = *sched->GetTaskGroupStats(sched->GetTaskId("bgp::Config"));
    task_stats_[7] = *sched->GetTaskGroupStats(sched->GetTaskId("Agent::KSync"));
}

//////////////////////////////////////////////////////////////////////////////
// Sandesh ProfileData routines
//////////////////////////////////////////////////////////////////////////////
static void DBStatsToSandesh(SandeshDBTableStats *stats, const string &table,
                             const ProfileData::DBTableStats &db_stats) {
    stats->set_table(table);
    stats->set_db_entry_count(db_stats.db_entry_count_);
    stats->set_input_count(db_stats.input_count_);
    stats->set_walker_count(db_stats.walker_count_);
    stats->set_enqueue_count(db_stats.enqueue_count_);
    stats->set_notify_count(db_stats.notify_count_);
}

static void GetDBTableStats(SandeshDBTableStatsInfo *stats, int index,
                            ProfileData *data) {
    stats->set_index(index);
    stats->set_time_str(data->time_);
    std::vector<SandeshDBTableStats> db_stats_list;

    SandeshDBTableStats db_stats;
    std::map<std::string, ProfileData::DBTableStats >::iterator itr =
        data->profile_stats_table_.begin();

    DBStatsToSandesh(&db_stats, "Ipv4 Unicast route", data->inet4_routes_);
    db_stats_list.push_back(db_stats);
    DBStatsToSandesh(&db_stats, "Ipv6 Unicast route", data->inet6_routes_);
    db_stats_list.push_back(db_stats);
    DBStatsToSandesh(&db_stats, "Multicast route", data->multicast_routes_);
    db_stats_list.push_back(db_stats);
    DBStatsToSandesh(&db_stats, "Evpn route", data->evpn_routes_);
    db_stats_list.push_back(db_stats);
    DBStatsToSandesh(&db_stats, "Bridge", data->bridge_routes_);
    db_stats_list.push_back(db_stats);
    while (itr != data->profile_stats_table_.end()) {
        if(itr->first.find(kInterfaceDbTablePrefix) != std::string::npos) {
           DBStatsToSandesh(&db_stats, "Interface", itr->second);
           db_stats_list.push_back(db_stats);
        } else if (itr->first.find(kMplsDbTablePrefix) != std::string::npos) {
           DBStatsToSandesh(&db_stats, "Mpls", itr->second);
           db_stats_list.push_back(db_stats);
        } else if (itr->first.find(kLoadBalnceDbTablePrefix) !=
                   std::string::npos) {
           DBStatsToSandesh(&db_stats, "Loadbalancer", itr->second);
           db_stats_list.push_back(db_stats);
        } else if (itr->first.find(kVnDbTablePrefix) != std::string::npos) {
           DBStatsToSandesh(&db_stats, "Vn", itr->second);
           db_stats_list.push_back(db_stats);
        } else if (itr->first.find(kVmDbTablePrefix) != std::string::npos) {
           DBStatsToSandesh(&db_stats, "Vm", itr->second);
           db_stats_list.push_back(db_stats);
        } else if (itr->first.find(kVrfDbTablePrefix) != std::string::npos) {
           DBStatsToSandesh(&db_stats, "Vrf", itr->second);
           db_stats_list.push_back(db_stats);
        } else if (itr->first.find(kAclDbTablePrefix) != std::string::npos) {
           DBStatsToSandesh(&db_stats, "Acl", itr->second);
           db_stats_list.push_back(db_stats);
        }
        ++itr;
    }

    stats->set_stats(db_stats_list);
}

void SandeshDBTableStatsRequest::HandleRequest() const {
    SandeshDBTableStatsList *resp = new SandeshDBTableStatsList();
    resp->set_context(context());

    Agent *agent = Agent::GetInstance();
    AgentProfile *profile = agent->oper_db()->agent_profile();
    uint16_t end = profile->seconds_history_index();
    uint16_t start = 0;
    if (end > AgentProfile::kSecondsHistoryCount)
        start = end - AgentProfile::kSecondsHistoryCount;

    std::vector<SandeshDBTableStatsInfo> stats_list;
    for (uint16_t i = start; i < end; i++) {
        uint16_t index = i % AgentProfile::kSecondsHistoryCount;
        ProfileData *data = profile->GetProfileData(index);
        SandeshDBTableStatsInfo stats;
        GetDBTableStats(&stats, index, data);
        stats_list.push_back(stats);
    }
    resp->set_stats(stats_list);

    resp->Response();
}

static void GetFlowStats(SandeshFlowStats *stats, int index,
                         ProfileData *data) {
    stats->set_index(index);
    stats->set_time_str(data->time_);
    stats->set_add_count(data->flow_.add_count_);
    stats->set_del_count(data->flow_.del_count_);
    stats->set_reval_count(data->flow_.reval_count_);
}

void SandeshFlowStatsRequest::HandleRequest() const {
    SandeshFlowStatsList *resp = new SandeshFlowStatsList();
    resp->set_context(context());

    Agent *agent = Agent::GetInstance();
    AgentProfile *profile = agent->oper_db()->agent_profile();
    uint16_t end = profile->seconds_history_index();
    uint16_t start = 0;
    if (end > AgentProfile::kSecondsHistoryCount)
        start = end - AgentProfile::kSecondsHistoryCount;

    std::vector<SandeshFlowStats> stats_list;
    for (uint16_t i = start; i < end; i++) {
        uint16_t index = i % AgentProfile::kSecondsHistoryCount;
        ProfileData *data = profile->GetProfileData(index);
        SandeshFlowStats stats;
        GetFlowStats(&stats, index, data);
        stats_list.push_back(stats);
    }
    resp->set_stats(stats_list);
    resp->Response();
}

static void GetTaskStats(TaskProfileStats *stats, int index,
                         ProfileData *data) {
    stats->set_index(index);

    TaskStats *task_stats = NULL;
    // Flow Handler
    task_stats = &data->task_stats_[0];
    stats->set_flow_wait(task_stats->wait_count_);
    stats->set_flow_run(task_stats->enqueue_count_);
    stats->set_flow_defer(task_stats->defer_count_);

    // DB
    task_stats = &data->task_stats_[1];
    stats->set_db_wait(task_stats->wait_count_);
    stats->set_db_run(task_stats->enqueue_count_);
    stats->set_db_defer(task_stats->defer_count_);

    // Stats Collector
    task_stats = &data->task_stats_[2];
    stats->set_stats_wait(task_stats->wait_count_);
    stats->set_stats_run(task_stats->enqueue_count_);
    stats->set_stats_defer(task_stats->defer_count_);

    // Io-Reader
    task_stats = &data->task_stats_[3];
    stats->set_io_wait(task_stats->wait_count_);
    stats->set_io_run(task_stats->enqueue_count_);
    stats->set_io_defer(task_stats->defer_count_);

    // Agent::PktFlowResponder
    task_stats = &data->task_stats_[4];
    stats->set_flow_resp_wait(task_stats->wait_count_);
    stats->set_flow_resp_run(task_stats->enqueue_count_);
    stats->set_flow_resp_defer(task_stats->defer_count_);

    // Sadnesh::RecvQueue
    task_stats = &data->task_stats_[5];
    stats->set_sandesh_rcv_wait(task_stats->wait_count_);
    stats->set_sandesh_rcv_run(task_stats->enqueue_count_);
    stats->set_sandesh_rcv_defer(task_stats->defer_count_);

    // bgp::Config
    task_stats = &data->task_stats_[6];
    stats->set_bgp_cfg_wait(task_stats->wait_count_);
    stats->set_bgp_cfg_run(task_stats->enqueue_count_);
    stats->set_bgp_cfg_defer(task_stats->defer_count_);

    // KSync
    task_stats = &data->task_stats_[7];
    stats->set_ksync_wait(task_stats->wait_count_);
    stats->set_ksync_run(task_stats->enqueue_count_);
    stats->set_ksync_defer(task_stats->defer_count_);
}

void SandeshTaskStatsRequest::HandleRequest() const {
    SandeshTaskStatsList *resp = new SandeshTaskStatsList();
    resp->set_context(context());

    Agent *agent = Agent::GetInstance();
    AgentProfile *profile = agent->oper_db()->agent_profile();
    uint16_t end = profile->seconds_history_index();
    uint16_t start = 0;
    if (end > AgentProfile::kSecondsHistoryCount)
        start = end - AgentProfile::kSecondsHistoryCount;

    std::vector<TaskProfileStats> stats_list;
    for (uint16_t i = start; i < end; i++) {
        uint16_t index = i % AgentProfile::kSecondsHistoryCount;
        ProfileData *data = profile->GetProfileData(index);
        TaskProfileStats stats;
        GetTaskStats(&stats, index, data);
        stats_list.push_back(stats);
    }
    resp->set_stats(stats_list);
    resp->Response();
}
