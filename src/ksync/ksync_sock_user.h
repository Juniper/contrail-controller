/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ksync_sock_user_h 
#define ctrlplane_ksync_sock_user_h 

#include <queue>

#include <tbb/mutex.h>
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>

#include <boost/unordered_map.hpp>
#include "ksync_sock.h"

#include "vr_types.h"
#include "vr_flow.h"

using boost::asio::ip::udp;

class MockDumpHandlerBase;

class vrouter_ops_test : public vrouter_ops {
private:
    virtual void Process(SandeshContext *context);
};

class KSyncUserSockContext : public AgentSandeshContext {
public:
    KSyncUserSockContext(bool value, uint32_t num) : 
            response_reqd_(value), seqno_(num) {}
    virtual ~KSyncUserSockContext() {}
    void SetResponseReqd(bool value) {
        response_reqd_ = value;
    }
    bool IsResponseReqd() { return response_reqd_; }
    uint32_t GetSeqNum() { return seqno_; }
    virtual void IfMsgHandler(vr_interface_req *req);
    virtual void NHMsgHandler(vr_nexthop_req *req);
    virtual void RouteMsgHandler(vr_route_req *req);
    virtual void MplsMsgHandler(vr_mpls_req *req);
    virtual void MirrorMsgHandler(vr_mirror_req *req);
    virtual int VrResponseMsgHandler(vr_response *req) {return 0;};
    virtual void FlowMsgHandler(vr_flow_req *req);
    virtual void VrfAssignMsgHandler(vr_vrf_assign_req *req);
    virtual void VrfStatsMsgHandler(vr_vrf_stats_req *req);
    virtual void DropStatsMsgHandler(vr_drop_stats_req *req);
    virtual void VxLanMsgHandler(vr_vxlan_req *req);
    virtual void Process() {}
private:
    bool response_reqd_; 
    uint32_t seqno_;
};

struct TestRouteCmp {
    bool operator()(const vr_route_req &lhs, const vr_route_req &rhs) {
        if (lhs.get_rtr_vrf_id() != rhs.get_rtr_vrf_id()) {
            return lhs.get_rtr_vrf_id() < rhs.get_rtr_vrf_id();
        }
        if (lhs.get_rtr_prefix() != rhs.get_rtr_prefix()) {
            return lhs.get_rtr_prefix() < rhs.get_rtr_prefix();
        }
        return lhs.get_rtr_prefix_len() < rhs.get_rtr_prefix_len();
    }
};

struct TestVrfAssignCmp {
    bool operator() (const vr_vrf_assign_req &lhs,
                     const vr_vrf_assign_req &rhs){
        if (lhs.get_var_vif_index() != rhs.get_var_vif_index()) {
            return lhs.get_var_vif_index() < rhs.get_var_vif_index();
        }

        return lhs.get_var_vlan_id() < rhs.get_var_vlan_id();
    }
};

//this class stores all netlink sandesh messages in a map
//used for unit testing or userspace datapath integration
class KSyncSockTypeMap : public KSyncSock {
public:
    enum KSyncSockEntryType {
        KSYNC_FLOW_ENTRY_TYPE = 0,
        KSYNC_MAX_ENTRY_TYPE
    };

    KSyncSockTypeMap(boost::asio::io_service &ios) : KSyncSock(), sock_(ios) {
        block_msg_processing_ = false;
    }
    ~KSyncSockTypeMap() {
        assert(nh_map.size() == 0);
        assert(flow_map.size() == 0);
        assert(if_map.size() == 0);
        assert(rt_tree.size() == 0);
        assert(mpls_map.size() == 0);
        assert(mirror_map.size() == 0);
        assert(vrf_assign_tree.size() == 0);
        assert(vxlan_map.size() == 0);
        singleton_ = NULL;
    }

    typedef std::map<int, vr_nexthop_req> ksync_map_nh;
    ksync_map_nh nh_map; 
    typedef boost::unordered_map<int, vr_flow_req> ksync_map_flow;
    ksync_map_flow flow_map; 
    typedef std::map<int, vr_interface_req> ksync_map_if;
    ksync_map_if if_map; 
    typedef std::set<vr_route_req, TestRouteCmp> ksync_rt_tree;
    ksync_rt_tree rt_tree; 
    typedef std::map<int, vr_mpls_req> ksync_map_mpls;
    ksync_map_mpls mpls_map; 
    typedef std::map<int, vr_mirror_req> ksync_map_mirror;
    ksync_map_mirror mirror_map; 
    typedef std::set<vr_vrf_assign_req, TestVrfAssignCmp> ksync_vrf_assign_tree;
    ksync_vrf_assign_tree vrf_assign_tree; 
    typedef std::map<int, vr_vrf_stats_req> ksync_map_vrf_stats;
    ksync_map_vrf_stats vrf_stats_map; 
    vr_drop_stats_req drop_stats;
    typedef std::map<int, vr_vxlan_req> ksync_map_vxlan;
    ksync_map_vxlan vxlan_map; 

    typedef std::queue<KSyncUserSockContext *> ksync_map_ctx_queue;
    ksync_map_ctx_queue ctx_queue_;
    tbb::mutex  ctx_queue_lock_;

    virtual uint32_t GetSeqno(char *data);
    virtual bool IsMoreData(char *data);
    virtual void Decoder(char *data, SandeshContext *ctxt);
    virtual bool Validate(char *data);
    virtual void AsyncReceive(boost::asio::mutable_buffers_1, HandlerCb);
    virtual void AsyncSendTo(IoContext *, boost::asio::mutable_buffers_1,
                             HandlerCb);
    virtual std::size_t SendTo(boost::asio::const_buffers_1, uint32_t);
    virtual void Receive(boost::asio::mutable_buffers_1);

    static void set_error_code(int code) { error_code_ = code; }
    static int error_code() { return error_code_; }
    static void ProcessSandesh(const uint8_t *, std::size_t, KSyncUserSockContext *);
    static void SimulateResponse(uint32_t, int, int);
    static void SendNetlinkDoneMsg(int seq_num);
    static void IfDumpResponse(uint32_t);
    static void IfNetlinkMsgSend(uint32_t seq_num, ksync_map_if::const_iterator it);
    static void IfStatsUpdate(int, int, int, int, int, int, int);
    static void IfStatsSet(int, int, int, int, int, int, int);
    static void InterfaceAdd(int id, int flags = 0, int mac_size = 6);
    static void InterfaceDelete(int id);
    static void NHAdd(int id, int flags = 0);
    static void NHDelete(int id);
    static void MplsAdd(int id);
    static void MplsDelete(int id);
    static void MirrorAdd(int id);
    static void MirrorDelete(int id);
    static void RouteAdd(vr_route_req &req);
    static void RouteDelete(vr_route_req &req);
    static void VrfAssignAdd(vr_vrf_assign_req &req);
    static void VrfAssignDelete(vr_vrf_assign_req &req);
    static void VrfStatsAdd(int vrf_id);
    static void VrfStatsUpdate(int vrf_id, uint64_t discards, uint64_t resolves, 
                               uint64_t receives, uint64_t udp_tunnels, 
                               uint64_t udp_mpls_tunnels, 
                               uint64_t udp_gre_tunnels, 
                               int64_t ecmp_composites, 
                               int64_t fabric_composites,
                               int64_t l2_mcast_composites,
                               int64_t l3_mcast_composites,
                               int64_t multi_proto_composites,
                               uint64_t encaps, uint64_t l2_encaps);
    static void VrfStatsDelete(int vrf_id);
    static void VxlanAdd(int id);
    static void VxlanDelete(int id);

    static void SetDropStats(const vr_drop_stats_req &req);
    static int IfCount();
    static int NHCount();
    static int MplsCount();
    static int RouteCount();
    static int VxLanCount();
    static KSyncSockTypeMap *GetKSyncSockTypeMap() { return singleton_; };
    static void Init(boost::asio::io_service &ios, int count);
    static void Shutdown();
    static vr_flow_entry *FlowMmapAlloc(int size);
    static void FlowMmapFree();
    static vr_flow_entry *GetFlowEntry(int idx);
    static void SetFlowEntry(vr_flow_req *req, bool set);
    static void IncrFlowStats(int idx, int pkts, int bytes);
    static void SetOFlowStats(int idx, uint8_t pkts, uint16_t bytes);
    static void FlowNatResponse(uint32_t seq_num, vr_flow_req *req);
    friend class MockDumpHandlerBase;
    friend class RouteDumpHandler;
    friend class VrfAssignDumpHandler;
    void SetBlockMsgProcessing(bool enable);
    bool IsBlockMsgProcessing() {
        tbb::mutex::scoped_lock lock(ctx_queue_lock_);
        return block_msg_processing_;
    }

    void SetKSyncError(KSyncSockEntryType type, int ksync_error) {
        ksync_error_[type] = ksync_error;
    }

    int GetKSyncError(KSyncSockEntryType type) {
        return ksync_error_[type];
    }

private:
    void PurgeBlockedMsg();
    udp::socket sock_;
    udp::endpoint local_ep_;
    int ksync_error_[KSYNC_MAX_ENTRY_TYPE];
    bool block_msg_processing_;
    static KSyncSockTypeMap *singleton_;
    static vr_flow_entry *flow_table_;
    static int error_code_;
    DISALLOW_COPY_AND_ASSIGN(KSyncSockTypeMap);
};

class MockDumpHandlerBase {
public:
    MockDumpHandlerBase() {}
    virtual ~MockDumpHandlerBase() {}
    void SendDumpResponse(uint32_t seq_num, Sandesh *);
    void SendGetResponse(uint32_t seq_num, int idx);
    virtual Sandesh* GetFirst(Sandesh *) = 0;
    virtual Sandesh* GetNext(Sandesh *) = 0;
    virtual Sandesh* Get(int idx) = 0;
};

class IfDumpHandler : public MockDumpHandlerBase {
public:
    IfDumpHandler() : MockDumpHandlerBase() {}
    virtual Sandesh* GetFirst(Sandesh *);
    virtual Sandesh* GetNext(Sandesh *);
    virtual Sandesh* Get(int idx);
};

class NHDumpHandler : public MockDumpHandlerBase {
public:
    NHDumpHandler() : MockDumpHandlerBase() {}
    virtual Sandesh* GetFirst(Sandesh *);
    virtual Sandesh* GetNext(Sandesh *);
    virtual Sandesh* Get(int idx);
};

class MplsDumpHandler : public MockDumpHandlerBase {
public:
    MplsDumpHandler() : MockDumpHandlerBase() {}
    virtual Sandesh* GetFirst(Sandesh *);
    virtual Sandesh* GetNext(Sandesh *);
    virtual Sandesh* Get(int idx);
};

class MirrorDumpHandler : public MockDumpHandlerBase {
public:
    MirrorDumpHandler() : MockDumpHandlerBase() {}
    virtual Sandesh* GetFirst(Sandesh *);
    virtual Sandesh* GetNext(Sandesh *);
    virtual Sandesh* Get(int idx);
};

class RouteDumpHandler : public MockDumpHandlerBase {
public:
    RouteDumpHandler() : MockDumpHandlerBase() {}
    virtual Sandesh* GetFirst(Sandesh *);
    virtual Sandesh* GetNext(Sandesh *);
    /* GET operation is not supported for vrf assign */
    virtual Sandesh* Get(int idx) { 
        return NULL;
    }
};

class VrfAssignDumpHandler : public MockDumpHandlerBase {
public:
    VrfAssignDumpHandler() : MockDumpHandlerBase() {}
    virtual Sandesh* GetFirst(Sandesh *);
    virtual Sandesh* GetNext(Sandesh *);
    /* GET operation is not supported for route */
    virtual Sandesh* Get(int idx) { 
        return NULL;
    }
};

class VrfStatsDumpHandler : public MockDumpHandlerBase {
public:
    VrfStatsDumpHandler() : MockDumpHandlerBase() {}
    virtual Sandesh* GetFirst(Sandesh *);
    virtual Sandesh* GetNext(Sandesh *);
    virtual Sandesh* Get(int idx);
};

class VxLanDumpHandler : public MockDumpHandlerBase {
public:
    VxLanDumpHandler() : MockDumpHandlerBase() {}
    virtual Sandesh* GetFirst(Sandesh *);
    virtual Sandesh* GetNext(Sandesh *);
    virtual Sandesh* Get(int idx);
};
 
class DropStatsDumpHandler : public MockDumpHandlerBase {
public:
    DropStatsDumpHandler() : MockDumpHandlerBase() {}
    virtual Sandesh* GetFirst(Sandesh *) { return NULL; }
    virtual Sandesh* GetNext(Sandesh *) { return NULL; }
    virtual Sandesh* Get(int idx) {
        KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
        return &sock->drop_stats;
    }
};

class KSyncUserSockIfContext : public KSyncUserSockContext {
public:
    KSyncUserSockIfContext(uint32_t seq_num, vr_interface_req *req) : KSyncUserSockContext(false, seq_num) {
        if (req) {
            req_ = new vr_interface_req(*req);
        } else {
            req_ = NULL;
        }
    }
    ~KSyncUserSockIfContext() {
        if (req_) {
            delete req_;
            req_ = NULL;
        }
    }

    virtual void Process(); 
private:
    vr_interface_req *req_;
};

class KSyncUserSockNHContext : public KSyncUserSockContext {
public:
    KSyncUserSockNHContext(uint32_t seq_num, vr_nexthop_req *req) : KSyncUserSockContext(false, seq_num) {
        if (req) {
            req_ = new vr_nexthop_req(*req);
        } else {
            req_ = NULL;
        }
    }
    ~KSyncUserSockNHContext() {
        if (req_) {
            delete req_;
            req_ = NULL;
        }
    }

    virtual void Process(); 
private:
    vr_nexthop_req *req_;
};

class KSyncUserSockMplsContext : public KSyncUserSockContext {
public:
    KSyncUserSockMplsContext(uint32_t seq_num, vr_mpls_req *req) : KSyncUserSockContext(false, seq_num) {
        if (req) {
            req_ = new vr_mpls_req(*req);
        } else {
            req_ = NULL;
        }
    }
    ~KSyncUserSockMplsContext() {
        if (req_) {
            delete req_;
            req_ = NULL;
        }
    }

    virtual void Process(); 
private:
    vr_mpls_req *req_;
};

class KSyncUserSockFlowContext : public KSyncUserSockContext {
public:
    KSyncUserSockFlowContext(uint32_t seq_num, vr_flow_req *req) : KSyncUserSockContext(false, seq_num) {
        if (req) {
            req_ = new vr_flow_req(*req);
        } else {
            req_ = NULL;
        }
    }
    ~KSyncUserSockFlowContext() {
        if (req_) {
            delete req_;
            req_ = NULL;
        }
    }

    virtual void Process(); 
private:
    vr_flow_req *req_;
};

class KSyncUserSockRouteContext : public KSyncUserSockContext {
public:
    KSyncUserSockRouteContext(uint32_t seq_num, vr_route_req *req) : KSyncUserSockContext(false, seq_num) {
        if (req) {
            req_ = new vr_route_req(*req);
        } else {
            req_ = NULL;
        }
    }
    ~KSyncUserSockRouteContext() {
        if (req_) {
            delete req_;
            req_ = NULL;
        }
    }

    virtual void Process(); 
private:
    vr_route_req *req_;
};

class KSyncUserSockVrfAssignContext : public KSyncUserSockContext {
public:
    KSyncUserSockVrfAssignContext(uint32_t seq_num, vr_vrf_assign_req *req) : 
        KSyncUserSockContext(false, seq_num) {
        if (req) {
            req_ = new vr_vrf_assign_req(*req);
        } else {
            req_ = NULL;
        }
    }
    ~KSyncUserSockVrfAssignContext() {
        if (req_) {
            delete req_;
            req_ = NULL;
        }
    }

    virtual void Process(); 
private:
    vr_vrf_assign_req *req_;
};

class KSyncUserSockVrfStatsContext : public KSyncUserSockContext {
public:
    KSyncUserSockVrfStatsContext(uint32_t seq_num, vr_vrf_stats_req *req) : KSyncUserSockContext(false, seq_num) {
        if (req) {
            req_ = new vr_vrf_stats_req(*req);
        } else {
            req_ = NULL;
        }
    }
    ~KSyncUserSockVrfStatsContext() {
        if (req_) {
            delete req_;
            req_ = NULL;
        }
    }

    virtual void Process(); 
private:
    vr_vrf_stats_req *req_;
};

class KSyncUserSockVxLanContext : public KSyncUserSockContext {
public:
    KSyncUserSockVxLanContext(uint32_t seq_num, vr_vxlan_req *req) : KSyncUserSockContext(false, seq_num) {
        if (req) {
            req_ = new vr_vxlan_req(*req);
        } else {
            req_ = NULL;
        }
    }
    ~KSyncUserSockVxLanContext() {
        if (req_) {
            delete req_;
            req_ = NULL;
        }
    }

    virtual void Process(); 
private:
    vr_vxlan_req *req_;
};
 
class KSyncUserSockDropStatsContext : public KSyncUserSockContext {
public:
    KSyncUserSockDropStatsContext(uint32_t seq_num, vr_drop_stats_req *req) : KSyncUserSockContext(false, seq_num) {
        if (req) {
            req_ = new vr_drop_stats_req(*req);
        } else {
            req_ = NULL;
        }
    }
    ~KSyncUserSockDropStatsContext() {
        if (req_) {
            delete req_;
            req_ = NULL;
        }
    }

    virtual void Process(); 
private:
    vr_drop_stats_req *req_;
};

#endif // ctrlplane_ksync_sock_user_h
