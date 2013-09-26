/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_stats_h
#define vnsw_agent_stats_h

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_sock.h>

#include <cmn/agent_cmn.h>
#include <uve/stats_collector.h>
#include "vr_message.h"

class AgentStatsCollector : public StatsCollector {
public:
    static const uint32_t AgentStatsInterval = (30 * 1000); // time in milliseconds
    struct IfStats {
        IfStats() : name(""), speed(0), duplexity(0), in_pkts(0), in_bytes(0), 
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
    struct VrfStats {
        VrfStats() : name(""), discards(0), resolves(0), receives(0), 
                     tunnels(0), composites(0), encaps(0),
                     prev_discards(0), prev_resolves(0), prev_receives(0), 
                     prev_tunnels(0), prev_composites(0), prev_encaps(0),
                     k_discards(0), k_resolves(0), k_receives(0), 
                     k_tunnels(0), k_composites(0), k_encaps(0) {}
        std::string name;
        uint64_t discards;
        uint64_t resolves;
        uint64_t receives;
        uint64_t tunnels;
        uint64_t composites;
        uint64_t encaps;
        uint64_t prev_discards;
        uint64_t prev_resolves;
        uint64_t prev_receives;
        uint64_t prev_tunnels;
        uint64_t prev_composites;
        uint64_t prev_encaps;
        uint64_t k_discards;
        uint64_t k_resolves;
        uint64_t k_receives;
        uint64_t k_tunnels;
        uint64_t k_composites;
        uint64_t k_encaps;
    };

    enum StatsType {
        IntfStatsType,
        VrfStatsType,
        DropStatsType
    };
    typedef std::map<const Interface *, IfStats> IntfToIfStatsTree;
    typedef std::pair<const Interface *, IfStats> IfStatsPair;
    typedef std::map<int, VrfStats> VrfIdToVrfStatsTree;
    typedef std::pair<int, VrfStats> VrfStatsPair;

    AgentStatsCollector(boost::asio::io_service &io, int intvl) : 
        StatsCollector(StatsCollector::AgentStatsCollector, io, intvl, "Agent Stats collector"), 
        vrf_stats_responses_(0), drop_stats_responses_(0) {
        AddNamelessVrfStatsEntry();
    }
    virtual ~AgentStatsCollector() { 
        assert(if_stats_tree_.size() == 0);
    };

    void SendIntfBulkGet();
    void SendVrfStatsBulkGet();
    void SendDropStatsBulkGet();
    bool Run();

    void AddIfStatsEntry(const Interface *intf);
    void DelIfStatsEntry(const Interface *intf);
    void AddUpdateVrfStatsEntry(const VrfEntry *intf);
    void DelVrfStatsEntry(const VrfEntry *intf);
    void SendStats();
    IfStats* GetIfStats(const Interface *intf);
    VrfStats* GetVrfStats(int vrf_id);
    vr_drop_stats_req GetDropStats() const { return drop_stats_; }
    void SetDropStats(vr_drop_stats_req &req) { drop_stats_ = req; }
    std::string GetNamelessVrf() { return "__untitled__"; }
    int GetNamelessVrfId() { return -1; }
    int vrf_stats_responses_; //used only in UT code
    int drop_stats_responses_; //used only in UT code
private:
    void AddNamelessVrfStatsEntry();
    void SendAsync(char* buf, uint32_t buf_len, StatsType type);
    bool SendRequest(Sandesh &encoder, StatsType type);

    IntfToIfStatsTree if_stats_tree_;
    VrfIdToVrfStatsTree vrf_stats_tree_;
    vr_drop_stats_req drop_stats_;
    DISALLOW_COPY_AND_ASSIGN(AgentStatsCollector);
};

class AgentStatsSandeshContext: public AgentSandeshContext {
public:
    AgentStatsSandeshContext() : marker_id_(-1) {}
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
    virtual void VrfStatsMsgHandler(vr_vrf_stats_req *req);
    virtual void DropStatsMsgHandler(vr_drop_stats_req *req);
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
                       AgentStatsSandeshContext *ctx, IoContext::IoContextWorkQId id) 
        : IoContext(msg, msg_len, seqno, ctx, id) {}
    void Handler();
    void ErrorHandler(int err);
};

class VrfStatsIoContext: public IoContext {
public:
    VrfStatsIoContext(int msg_len, char *msg, uint32_t seqno, 
                      AgentStatsSandeshContext *ctx, IoContext::IoContextWorkQId id) 
        : IoContext(msg, msg_len, seqno, ctx, id) {}
    void Handler();
    void ErrorHandler(int err);
};

class DropStatsIoContext: public IoContext {
public:
    DropStatsIoContext(int msg_len, char *msg, uint32_t seqno, 
                       AgentStatsSandeshContext *ctx, IoContext::IoContextWorkQId id) 
        : IoContext(msg, msg_len, seqno, ctx, id) {}
    void Handler();
    void ErrorHandler(int err);
};

#endif //vnsw_agent_stats_h
