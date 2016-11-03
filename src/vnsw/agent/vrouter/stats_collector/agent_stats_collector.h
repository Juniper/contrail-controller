/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_stats_collector_h
#define vnsw_agent_stats_collector_h

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>

#include <cmn/agent_cmn.h>
#include <uve/stats_collector.h>
#include <boost/scoped_ptr.hpp>
#include <vrouter/stats_collector/agent_stats_sandesh_context.h>

//Defines the functionality to periodically poll interface, vrf and drop
//statistics from vrouter and updates its data-structures with this
//information. Stats collection request runs in the context of
//"Agent::StatsCollector" which has exclusion with "db::DBTable",
//"sandesh::RecvQueue", "bgp::Config" & "Agent::KSync"
//Stats collection response runs in the context of "Agent::Uve" which has
//exclusion with "db::DBTable"
class AgentStatsCollector : public StatsCollector {
public:
    enum StatsType {
        InterfaceStatsType,
        VrfStatsType,
        DropStatsType
    };

    AgentStatsCollector(boost::asio::io_service &io, Agent *agent);
    virtual ~AgentStatsCollector();
    Agent* agent() const { return agent_; }

    void SendInterfaceBulkGet();
    void SendVrfStatsBulkGet();
    void SendDropStatsBulkGet();
    bool Run();
    void RegisterDBClients();
    void Shutdown(void);
    virtual IoContext *AllocateIoContext(char* buf, uint32_t buf_len,
                                         StatsType type, uint32_t seq);
protected:
    boost::scoped_ptr<AgentStatsSandeshContext> intf_stats_sandesh_ctx_;
    boost::scoped_ptr<AgentStatsSandeshContext> vrf_stats_sandesh_ctx_;
    boost::scoped_ptr<AgentStatsSandeshContext> drop_stats_sandesh_ctx_;
private:
    void SendAsync(char* buf, uint32_t buf_len, StatsType type);
    bool SendRequest(Sandesh &encoder, StatsType type);

    Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(AgentStatsCollector);
};

#endif //vnsw_agent_stats_collector_h
