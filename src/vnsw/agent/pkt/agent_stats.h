/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 *
 * Global vnswad statistics
 */

#ifndef vnsw_agent_stats_hpp
#define vnsw_agent_stats_hpp

class AgentStats {
public:
    AgentStats(Agent *agent)
        : agent_(agent), xmpp_reconnect_(), xmpp_in_msgs_(), xmpp_out_msgs_(),
        sandesh_reconnects_(0U), sandesh_in_msgs_(0U), sandesh_out_msgs_(0U),
        sandesh_http_sessions_(0U), nh_count_(0U), pkt_exceptions_(0U),
        pkt_invalid_agent_hdr_(0U), pkt_invalid_interface_(0U), 
        pkt_no_handler_(0U), pkt_dropped_(0U), flow_created_(0U),
        flow_aged_(0U), flow_active_(0U), flow_drop_due_to_max_limit_(0),
        flow_drop_due_to_linklocal_limit_(0), ipc_in_msgs_(0U),
        ipc_out_msgs_(0U), in_tpkts_(0U), in_bytes_(0U), out_tpkts_(0U),
        out_bytes_(0U) {
        assert(singleton_ == NULL);
        singleton_ = this;
    }

    virtual ~AgentStats() {singleton_ = NULL;}

    static AgentStats *GetInstance() {return singleton_;}
    void Shutdown() { }

    void Reset();
    void incr_xmpp_reconnects(uint8_t idx) {xmpp_reconnect_[idx]++;}
    uint16_t xmpp_reconnects(uint8_t idx) const {
        return xmpp_reconnect_[idx];
    }

    void incr_xmpp_in_msgs(uint8_t idx) {xmpp_in_msgs_[idx]++;}
    uint64_t xmpp_in_msgs(uint8_t idx) const {return xmpp_in_msgs_[idx];}

    void incr_xmpp_out_msgs(uint8_t idx) {xmpp_out_msgs_[idx]++;}
    uint64_t xmpp_out_msgs(uint8_t idx) const {return xmpp_out_msgs_[idx];}

    void incr_sandesh_reconnects() {sandesh_reconnects_++;}
    uint16_t sandesh_reconnects() const {return sandesh_reconnects_;}

    void incr_sandesh_in_msgs() {sandesh_in_msgs_++;}
    uint64_t sandesh_in_msgs() const {return sandesh_in_msgs_;}

    void incr_sandesh_out_msgs() {sandesh_out_msgs_++;}
    uint64_t sandesh_out_msgs() const {return sandesh_out_msgs_;}

    void incr_sandesh_http_sessions() {sandesh_http_sessions_++;}
    uint16_t sandesh_http_sessions() const {return sandesh_http_sessions_;}

    void incr_flow_created() {flow_created_++;}
    uint64_t flow_created() const {return flow_created_;}

    void incr_flow_aged() {flow_aged_++;}
    uint64_t flow_aged() const {return flow_aged_;}

    void incr_flow_drop_due_to_max_limit() {flow_drop_due_to_max_limit_++;}
    uint64_t flow_drop_due_to_max_limit() const {
        return flow_drop_due_to_max_limit_;
    }
    void incr_flow_drop_due_to_linklocal_limit() {
        flow_drop_due_to_linklocal_limit_++;
    }
    uint64_t flow_drop_due_to_linklocal_limit() const {
        return flow_drop_due_to_linklocal_limit_;
    }

    void incr_pkt_exceptions() {pkt_exceptions_++;}
    uint64_t pkt_exceptions() const {return pkt_exceptions_;}

    void incr_pkt_invalid_agent_hdr() {pkt_invalid_agent_hdr_++;}
    uint64_t pkt_invalid_agent_hdr() const {return pkt_invalid_agent_hdr_;}

    void incr_pkt_invalid_interface() {pkt_invalid_interface_++;}
    uint64_t pkt_invalid_interface() const {return pkt_invalid_interface_;}

    void incr_pkt_no_handler() {pkt_no_handler_++;}
    uint64_t pkt_no_handler() const {return pkt_no_handler_;}

    void incr_pkt_dropped() {pkt_dropped_++;}
    uint64_t pkt_dropped() const {return pkt_dropped_;}

    void incr_ipc_in_msgs() {ipc_in_msgs_++;}
    uint64_t ipc_in_msgs() const {return ipc_out_msgs_;}

    void incr_ipc_out_msgs() {ipc_out_msgs_++;}
    uint64_t ipc_out_msgs() const {return ipc_out_msgs_;}

    void incr_in_pkts(uint64_t count) {in_tpkts_ += count;}
    uint64_t in_pkts() const {return in_tpkts_;}

    void incr_in_bytes(uint64_t count) {in_bytes_ += count;}
    uint64_t in_bytes() const {return in_bytes_;}

    void incr_out_pkts(uint64_t count) {out_tpkts_ += count;}
    uint64_t out_pkts() const {return out_tpkts_;}

    void incr_out_bytes(uint64_t count) {out_bytes_ += count;}
    uint64_t out_bytes() const {return out_bytes_;}
private:
    Agent *agent_;
    uint16_t xmpp_reconnect_[MAX_XMPP_SERVERS];
    uint64_t xmpp_in_msgs_[MAX_XMPP_SERVERS];
    uint64_t xmpp_out_msgs_[MAX_XMPP_SERVERS];

    uint16_t sandesh_reconnects_;
    uint64_t sandesh_in_msgs_;
    uint64_t sandesh_out_msgs_;
    uint16_t sandesh_http_sessions_;

    // Number of NH created
    uint32_t nh_count_;

    // Exception packet stats
    uint64_t pkt_exceptions_;
    uint64_t pkt_invalid_agent_hdr_;
    uint64_t pkt_invalid_interface_;
    uint64_t pkt_no_handler_;
    uint64_t pkt_dropped_;

    // Flow stats
    uint64_t flow_created_;
    uint64_t flow_aged_;
    uint64_t flow_active_;
    uint64_t flow_drop_due_to_max_limit_;
    uint64_t flow_drop_due_to_linklocal_limit_;

    // Kernel IPC
    uint64_t ipc_in_msgs_;
    uint64_t ipc_out_msgs_;
    uint64_t in_tpkts_;
    uint64_t in_bytes_;
    uint64_t out_tpkts_;
    uint64_t out_bytes_;

    static AgentStats *singleton_;
};

#endif // vnsw_agent_stats_hpp
