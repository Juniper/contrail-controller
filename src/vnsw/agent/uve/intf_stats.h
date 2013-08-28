/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_intf_stats_h
#define vnsw_agent_intf_stats_h

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_sock.h>

#include <cmn/agent_cmn.h>
#include <uve/uve_init.h>
#include <uve/stats_collector.h>
#include "vr_message.h"

class IntfStatsCollector : public StatsCollector {
public:
    struct Stats {
        Stats() : name(""), speed(0), duplexity(0), in_pkts(0), in_bytes(0), 
                  out_pkts(0), out_bytes(0), prev_in_bytes(0), prev_out_bytes(0), 
                  prev_5min_in_bytes(0), prev_5min_out_bytes(0), 
                  prev_10min_in_bytes(0), prev_10min_out_bytes(10), 
                  stats_time(0) {}
        std::string name;
        int32_t  speed;
        int32_t  duplexity;
        uint64_t in_pkts;
        uint64_t in_bytes;
        uint64_t out_pkts;
        uint64_t out_bytes;
        uint64_t prev_in_bytes;
        uint64_t prev_out_bytes;
        uint64_t prev_5min_in_bytes;
        uint64_t prev_5min_out_bytes;
        uint64_t prev_10min_in_bytes;
        uint64_t prev_10min_out_bytes;
        uint64_t stats_time;
    };

    // Map from intf-uuid to intf-name
    typedef std::map<const Interface *, Stats> UuidToStatsTree;
    typedef std::pair<const Interface *, Stats> UuidStatsPair;

    IntfStatsCollector(boost::asio::io_service &io, int intvl) : 
        StatsCollector(StatsCollector::IntfStatsCollector, io, intvl) {}
    virtual ~IntfStatsCollector() { 
        assert(uuid_stats_tree_.size() == 0);
    };

    bool SendIntfBulkGet();
    bool Run();

    void AddStatsEntry(const Interface *intf);
    void DelStatsEntry(const Interface *intf);
    void SendStats();
    Stats* GetStats(const Interface *intf);
private:
    void SendAsync(char* buf, uint32_t buf_len);
    UuidToStatsTree uuid_stats_tree_;
    DISALLOW_COPY_AND_ASSIGN(IntfStatsCollector);
};

class IntfStatsSandeshContext: public AgentSandeshContext {
public:
    IntfStatsSandeshContext() : marker_id_(-1) {}
    virtual void IfMsgHandler(vr_interface_req *req);
    virtual void NHMsgHandler(vr_nexthop_req *req) {
        assert(0);
    }
    virtual void RouteMsgHandler(vr_route_req *req) {
        assert(0);
    }
    virtual void MplsMsgHandler(vr_mpls_req *req) {
        assert(0);
    }
    virtual void MirrorMsgHandler(vr_mirror_req *req) {
        assert(0);
    }
    virtual void VrfAssignMsgHandler(vr_vrf_assign_req *req) {
        assert(0);
    }
    virtual void VrfStatsMsgHandler(vr_vrf_stats_req *req) {
        assert(0);
    }
    virtual void DropStatsMsgHandler(vr_drop_stats_req *req) {
        assert(0);
    }
    /* Vr-response is expected from kernel and mock code.
     * For each dump response we get vr_response with negative 
     * value for errors and positive value (indicating number of 
     * entries being sent) for success.
     */
    virtual int VrResponseMsgHandler(vr_response *r);
    virtual void FlowMsgHandler(vr_flow_req *req) {
        assert(0);
    }
    int GetMarker() const { return marker_id_; }
    void SetMarker(int id) { marker_id_ = id; }
    bool MoreData() const { return (response_code_ & VR_MESSAGE_DUMP_INCOMPLETE); }
    void SetResponseCode(int value) { response_code_ = value; }
private:
    int marker_id_;
    int response_code_;
};

class IntfStatsIoContext: public IoContext {
public:
    IntfStatsIoContext(int msg_len, char *msg, uint32_t seqno, 
                       IntfStatsSandeshContext *ctx, IoContext::IoContextWorkQId id) 
        : IoContext(msg, msg_len, seqno, ctx, id) {}
    void Handler();
    void ErrorHandler(int err);
};

#endif //vnsw_agent_intf_stats_h
