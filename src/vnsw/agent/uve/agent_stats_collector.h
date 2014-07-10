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
#include <uve/agent_stats_sandesh_context.h>

//Defines the functionality to periodically poll interface, vrf and drop
//statistics from vrouter and updates its data-structures with this
//information. Stats collection request runs in the context of 
//"Agent::StatsCollector" which has exclusion with "db::DBTable", 
//"Agent::FlowHandler", "sandesh::RecvQueue", "bgp::Config" & "Agent::KSync"
//Stats collection response runs in the context of "Agent::Uve" which has 
//exclusion with "db::DBTable"
class AgentStatsCollector : public StatsCollector {
public:
    static const uint32_t AgentStatsInterval = (30 * 1000); // time in millisecs
    struct InterfaceStats {
        InterfaceStats()
            : name(""), speed(0), duplexity(0), in_pkts(0), in_bytes(0),
              out_pkts(0), out_bytes(0), prev_in_bytes(0),
              prev_out_bytes(0), prev_in_pkts(0), prev_out_pkts(0),
              prev_5min_in_bytes(0), prev_5min_out_bytes(0),
              prev_10min_in_bytes(0), prev_10min_out_bytes(10), stats_time(0) {
        }
        void UpdateStats(uint64_t in_b, uint64_t in_p, uint64_t out_b,
                         uint64_t out_p);
        void UpdatePrevStats();
        void GetDiffStats(uint64_t *in_b, uint64_t *in_p, uint64_t *out_b,
                          uint64_t *out_p);

        std::string name;
        int32_t  speed;
        int32_t  duplexity;
        uint64_t in_pkts;
        uint64_t in_bytes;
        uint64_t out_pkts;
        uint64_t out_bytes;
        uint64_t prev_in_bytes;
        uint64_t prev_out_bytes;
        uint64_t prev_in_pkts;  /* Required for sending diff stats to analytics */
        uint64_t prev_out_pkts; /* Required for sending diff stats to analytics */
        uint64_t prev_5min_in_bytes;
        uint64_t prev_5min_out_bytes;
        uint64_t prev_10min_in_bytes;
        uint64_t prev_10min_out_bytes;
        uint64_t stats_time;
    };
    struct VrfStats {
        VrfStats() : name(""), discards(0), resolves(0), receives(0), 
                     udp_tunnels(0), udp_mpls_tunnels(0), gre_mpls_tunnels(0),
                     ecmp_composites(0), l3_mcast_composites(0), 
                     l2_mcast_composites(0), fabric_composites(0),
                     multi_proto_composites(0), encaps(0), l2_encaps(0),
                     prev_discards(0), prev_resolves(0), prev_receives(0), 
                     prev_udp_tunnels(0), prev_udp_mpls_tunnels(0), 
                     prev_gre_mpls_tunnels(0), prev_encaps(0),
                     prev_ecmp_composites(0), prev_l3_mcast_composites(0), 
                     prev_l2_mcast_composites(0), prev_fabric_composites(0),
                     prev_multi_proto_composites(0), prev_l2_encaps(0),
                     k_discards(0), k_resolves(0), k_receives(0), 
                     k_gre_mpls_tunnels(0), k_encaps(0),
                     k_ecmp_composites(0), k_l3_mcast_composites(0), 
                     k_l2_mcast_composites(0), k_fabric_composites(0),
                     k_multi_proto_composites(0), k_l2_encaps(0) {};
        std::string name;
        uint64_t discards;
        uint64_t resolves;
        uint64_t receives;
        uint64_t udp_tunnels;
        uint64_t udp_mpls_tunnels;
        uint64_t gre_mpls_tunnels;
        uint64_t ecmp_composites;
        uint64_t l3_mcast_composites;
        uint64_t l2_mcast_composites;
        uint64_t fabric_composites;
        uint64_t multi_proto_composites;
        uint64_t encaps;
        uint64_t l2_encaps;
        uint64_t prev_discards;
        uint64_t prev_resolves;
        uint64_t prev_receives;
        uint64_t prev_udp_tunnels;
        uint64_t prev_udp_mpls_tunnels;
        uint64_t prev_gre_mpls_tunnels;
        uint64_t prev_encaps;
        uint64_t prev_ecmp_composites;
        uint64_t prev_l3_mcast_composites;
        uint64_t prev_l2_mcast_composites;
        uint64_t prev_fabric_composites;
        uint64_t prev_multi_proto_composites;
        uint64_t prev_l2_encaps;
        uint64_t k_discards;
        uint64_t k_resolves;
        uint64_t k_receives;
        uint64_t k_udp_tunnels;
        uint64_t k_udp_mpls_tunnels;
        uint64_t k_gre_mpls_tunnels;
        uint64_t k_encaps;
        uint64_t k_ecmp_composites;
        uint64_t k_l3_mcast_composites;
        uint64_t k_l2_mcast_composites;
        uint64_t k_fabric_composites;
        uint64_t k_multi_proto_composites;
        uint64_t k_l2_encaps;
    };

    enum StatsType {
        InterfaceStatsType,
        VrfStatsType,
        DropStatsType
    };
    typedef std::map<const Interface *, InterfaceStats> InterfaceStatsTree;
    typedef std::pair<const Interface *, InterfaceStats> InterfaceStatsPair;
    typedef std::map<int, VrfStats> VrfIdToVrfStatsTree;
    typedef std::pair<int, VrfStats> VrfStatsPair;

    AgentStatsCollector(boost::asio::io_service &io, Agent *agent);
    virtual ~AgentStatsCollector();
    Agent* agent() const { return agent_; }
    vr_drop_stats_req drop_stats() const { return drop_stats_; }
    void set_drop_stats(vr_drop_stats_req &req) { drop_stats_ = req; }

    void SendInterfaceBulkGet();
    void SendVrfStatsBulkGet();
    void SendDropStatsBulkGet();
    bool Run();
    void RegisterDBClients();
    void SendStats();
    InterfaceStats* GetInterfaceStats(const Interface *intf);
    VrfStats* GetVrfStats(int vrf_id);
    std::string GetNamelessVrf() { return "__untitled__"; }
    int GetNamelessVrfId() { return -1; }
    void Shutdown(void);
    virtual IoContext *AllocateIoContext(char* buf, uint32_t buf_len,
                                         StatsType type, uint32_t seq);
protected:
    boost::scoped_ptr<AgentStatsSandeshContext> intf_stats_sandesh_ctx_;
    boost::scoped_ptr<AgentStatsSandeshContext> vrf_stats_sandesh_ctx_;
    boost::scoped_ptr<AgentStatsSandeshContext> drop_stats_sandesh_ctx_;
private:
    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    void InterfaceNotify(DBTablePartBase *part, DBEntryBase *e);
    void AddNamelessVrfStatsEntry();
    void SendAsync(char* buf, uint32_t buf_len, StatsType type);
    bool SendRequest(Sandesh &encoder, StatsType type);
    void AddUpdateVrfStatsEntry(const VrfEntry *intf);
    void DelVrfStatsEntry(const VrfEntry *intf);
    void AddInterfaceStatsEntry(const Interface *intf);
    void DelInterfaceStatsEntry(const Interface *intf);

    InterfaceStatsTree if_stats_tree_;
    VrfIdToVrfStatsTree vrf_stats_tree_;
    vr_drop_stats_req drop_stats_;
    DBTableBase::ListenerId vrf_listener_id_;
    DBTableBase::ListenerId intf_listener_id_;
    Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(AgentStatsCollector);
};

#endif //vnsw_agent_stats_collector_h
