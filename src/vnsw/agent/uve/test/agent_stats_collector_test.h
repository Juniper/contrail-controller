/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_stats_collector_test_h
#define vnsw_agent_stats_collector_test_h

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>

#include <cmn/agent_cmn.h>
#include <uve/stats_collector.h>
#include <boost/scoped_ptr.hpp>
#include <vrouter/stats_collector/agent_stats_collector.h>
#include <vrouter/stats_collector/interface_stats_io_context.h>
#include <vrouter/stats_collector/vrf_stats_io_context.h>
#include <vrouter/stats_collector/drop_stats_io_context.h>
#include <vrouter/stats_collector/agent_stats_sandesh_context.h>

class AgentStatsCollectorTest : public AgentStatsCollector {
public:
    AgentStatsCollectorTest(boost::asio::io_service &io, Agent *agent) :
        AgentStatsCollector(io, agent), interface_stats_responses_(0),
        vrf_stats_responses_(0), drop_stats_responses_(0),
        interface_stats_errors_(0), vrf_stats_errors_(0), drop_stats_errors_(0) {
    }
    virtual ~AgentStatsCollectorTest() {
    }

    IoContext *AllocateIoContext(char* buf, uint32_t buf_len,
                                 StatsType type, uint32_t seq);
    void Test_DeleteVrfStatsEntry(int vrf_id);
    int interface_stats_responses_;
    int vrf_stats_responses_;
    int drop_stats_responses_;
    int interface_stats_errors_;
    int vrf_stats_errors_;
    int drop_stats_errors_;
};

class InterfaceStatsIoContextTest: public InterfaceStatsIoContext {
public:
    InterfaceStatsIoContextTest(int msg_len, char *msg, uint32_t seqno,
                                AgentStatsSandeshContext *ctx,
                                IoContext::Type type)
        : InterfaceStatsIoContext(msg_len, msg, seqno, ctx, type) {}
    virtual ~InterfaceStatsIoContextTest() {}
    void Handler();
    void ErrorHandler(int err);
private:
    DISALLOW_COPY_AND_ASSIGN(InterfaceStatsIoContextTest);
};

class VrfStatsIoContextTest: public VrfStatsIoContext {
public:
    VrfStatsIoContextTest(int msg_len, char *msg, uint32_t seqno,
                          AgentStatsSandeshContext *ctx,
                          IoContext::Type type)
        : VrfStatsIoContext(msg_len, msg, seqno, ctx, type) {}
    virtual ~VrfStatsIoContextTest() {}
    void Handler();
    void ErrorHandler(int err);
private:
    DISALLOW_COPY_AND_ASSIGN(VrfStatsIoContextTest);
};

class DropStatsIoContextTest: public DropStatsIoContext {
public:
    DropStatsIoContextTest(int msg_len, char *msg, uint32_t seqno,
                           AgentStatsSandeshContext *ctx,
                           IoContext::Type type)
        : DropStatsIoContext(msg_len, msg, seqno, ctx, type) {}
    virtual ~DropStatsIoContextTest() {}
    void Handler();
    void ErrorHandler(int err);
private:
    DISALLOW_COPY_AND_ASSIGN(DropStatsIoContextTest);
};

#endif //vnsw_agent_stats_collector_test_h
