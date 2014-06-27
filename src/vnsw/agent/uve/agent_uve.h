/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_uve_h
#define vnsw_agent_uve_h

#include <base/connection_info.h>
#include <uve/stats_collector.h>
#include <uve/agent_stats_collector.h>
#include <uve/flow_stats_collector.h>
#include <uve/vn_uve_table.h>
#include <uve/vm_uve_table.h>
#include <uve/vrouter_uve_entry.h>
#include <boost/scoped_ptr.hpp>

class VrouterStatsCollector;

//The class to drive UVE module initialization for agent.
//Defines objects required for statistics collection from vrouter and
//objects required for sending UVE information to collector.
class AgentUve {
public:
    static const uint64_t kBandwidthInterval = (1000000); // time in microseconds
    AgentUve(Agent *agent, uint64_t intvl);
    virtual ~AgentUve();

    void Shutdown();
    uint64_t bandwidth_intvl() const { return bandwidth_intvl_; } 
    VnUveTable* vn_uve_table() const { return vn_uve_table_.get(); }
    VmUveTable* vm_uve_table() const { return vm_uve_table_.get(); }
    VrouterUveEntry* vrouter_uve_entry() const { 
        return vrouter_uve_entry_.get();
    }
    AgentStatsCollector *agent_stats_collector() const {
        return agent_stats_collector_.get();
    }
    VrouterStatsCollector *vrouter_stats_collector() const {
        return vrouter_stats_collector_.get();
    }
    FlowStatsCollector *flow_stats_collector() const {
        return flow_stats_collector_.get();
    }
    // Update flow port bucket information
    void NewFlow(const FlowEntry *flow);
    void DeleteFlow(const FlowEntry *flow);

    void Init();
    void RegisterDBClients();
    static AgentUve *GetInstance() {return singleton_;}
protected:
    boost::scoped_ptr<VnUveTable> vn_uve_table_;
    boost::scoped_ptr<VmUveTable> vm_uve_table_;
    boost::scoped_ptr<VrouterUveEntry> vrouter_uve_entry_;
    boost::scoped_ptr<AgentStatsCollector> agent_stats_collector_;

private:
    void VrouterAgentConnectivityStatus(const std::vector<ConnectionInfo> &c,
        ConnectivityStatus::type &cstatus, std::string &message);
    uint8_t ExpectedConnections(uint8_t &num_c_nodes, uint8_t &num_d_servers);
    void UpdateStatus(const ConnectionInfo &info, ConnectivityStatus::type &c,
                      std::string &message);

    static AgentUve *singleton_;
    Agent *agent_;
    uint64_t bandwidth_intvl_; //in microseconds
    boost::scoped_ptr<VrouterStatsCollector> vrouter_stats_collector_;
    boost::scoped_ptr<FlowStatsCollector> flow_stats_collector_;
    ConnectionStateManager<VrouterAgentStatus, VrouterAgentProcessStatus>
        *connection_state_manager_;
    DISALLOW_COPY_AND_ASSIGN(AgentUve);
};

#endif //vnsw_agent_uve_h
