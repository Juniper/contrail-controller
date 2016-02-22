/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _ROOT_STATS_MANAGER_H_
#define _ROOT_STATS_MANAGER_H_

#include <cmn/agent_cmn.h>
#include <oper/vrf.h>
#include <oper/interface.h>
#include <vrouter_types.h>
#include <string>
#include <map>
#include <utility>
#include <uve/flow_ace_stats_request.h>

// The container class for storing stats queried from vrouter
// Defines routines for storing and managing (add, delete and query)
// interface, vrf and drop statistics
class StatsManager {
 public:
    struct InterfaceStats {
        InterfaceStats();
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
        uint64_t prev_in_pkts;  /* Required for sending diff stats */
        uint64_t prev_out_pkts; /* Required for sending diff stats */
        uint64_t prev_5min_in_bytes;
        uint64_t prev_5min_out_bytes;
        uint64_t prev_10min_in_bytes;
        uint64_t prev_10min_out_bytes;
        uint64_t stats_time;
    };
    struct VrfStats {
        VrfStats();
        std::string name;
        uint64_t discards;
        uint64_t resolves;
        uint64_t receives;
        uint64_t udp_tunnels;
        uint64_t udp_mpls_tunnels;
        uint64_t gre_mpls_tunnels;
        uint64_t ecmp_composites;
        uint64_t l2_mcast_composites;
        uint64_t fabric_composites;
        uint64_t encaps;
        uint64_t l2_encaps;
        uint64_t gros;
        uint64_t diags;
        uint64_t encap_composites;
        uint64_t evpn_composites;
        uint64_t vrf_translates;
        uint64_t vxlan_tunnels;
        uint64_t arp_virtual_proxy;
        uint64_t arp_virtual_stitch;
        uint64_t arp_virtual_flood;
        uint64_t arp_physical_stitch;
        uint64_t arp_tor_proxy;
        uint64_t arp_physical_flood;
        uint64_t l2_receives;
        uint64_t uuc_floods;

        uint64_t prev_discards;
        uint64_t prev_resolves;
        uint64_t prev_receives;
        uint64_t prev_udp_tunnels;
        uint64_t prev_udp_mpls_tunnels;
        uint64_t prev_gre_mpls_tunnels;
        uint64_t prev_ecmp_composites;
        uint64_t prev_l2_mcast_composites;
        uint64_t prev_fabric_composites;
        uint64_t prev_encaps;
        uint64_t prev_l2_encaps;
        uint64_t prev_gros;
        uint64_t prev_diags;
        uint64_t prev_encap_composites;
        uint64_t prev_evpn_composites;
        uint64_t prev_vrf_translates;
        uint64_t prev_vxlan_tunnels;
        uint64_t prev_arp_virtual_proxy;
        uint64_t prev_arp_virtual_stitch;
        uint64_t prev_arp_virtual_flood;
        uint64_t prev_arp_physical_stitch;
        uint64_t prev_arp_tor_proxy;
        uint64_t prev_arp_physical_flood;
        uint64_t prev_l2_receives;
        uint64_t prev_uuc_floods;

        uint64_t k_discards;
        uint64_t k_resolves;
        uint64_t k_receives;
        uint64_t k_udp_tunnels;
        uint64_t k_udp_mpls_tunnels;
        uint64_t k_gre_mpls_tunnels;
        uint64_t k_ecmp_composites;
        uint64_t k_l2_mcast_composites;
        uint64_t k_fabric_composites;
        uint64_t k_encaps;
        uint64_t k_l2_encaps;
        uint64_t k_gros;
        uint64_t k_diags;
        uint64_t k_encap_composites;
        uint64_t k_evpn_composites;
        uint64_t k_vrf_translates;
        uint64_t k_vxlan_tunnels;
        uint64_t k_arp_virtual_proxy;
        uint64_t k_arp_virtual_stitch;
        uint64_t k_arp_virtual_flood;
        uint64_t k_arp_physical_stitch;
        uint64_t k_arp_tor_proxy;
        uint64_t k_arp_physical_flood;
        uint64_t k_l2_receives;
        uint64_t k_uuc_floods;
    };

    typedef std::map<const Interface *, InterfaceStats> InterfaceStatsTree;
    typedef std::pair<const Interface *, InterfaceStats> InterfaceStatsPair;
    typedef std::map<int, VrfStats> VrfIdToVrfStatsTree;
    typedef std::pair<int, VrfStats> VrfStatsPair;

    struct FlowRuleMatchInfo {
        std::string interface;
        std::string sg_rule_uuid;
        std::string vn;
        std::string nw_ace_uuid;
        FlowRuleMatchInfo(const std::string &itf, const std::string &sg_rule,
                          const std::string &net, const std::string &nw_ace)
            : interface(itf), sg_rule_uuid(sg_rule), vn(net),
              nw_ace_uuid(nw_ace) {
        }
    };

    typedef std::map<const boost::uuids::uuid, FlowRuleMatchInfo> FlowAceTree;
    typedef std::pair<const boost::uuids::uuid, FlowRuleMatchInfo> FlowAcePair;

    explicit StatsManager(Agent *agent);
    virtual ~StatsManager();

    AgentDropStats drop_stats() const { return drop_stats_; }
    void set_drop_stats(const AgentDropStats &req) { drop_stats_ = req; }
    InterfaceStats* GetInterfaceStats(const Interface *intf);
    VrfStats* GetVrfStats(int vrf_id);
    std::string GetNamelessVrf() { return "__untitled__"; }
    int GetNamelessVrfId() { return -1; }
    void Shutdown(void);
    void RegisterDBClients();
    bool RequestHandler(boost::shared_ptr<FlowAceStatsRequest> req);
    void EnqueueEvent(const boost::shared_ptr<FlowAceStatsRequest> &req);
    friend class AgentStatsCollectorTest;

 private:
    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    void InterfaceNotify(DBTablePartBase *part, DBEntryBase *e);
    void AddNamelessVrfStatsEntry();
    void AddInterfaceStatsEntry(const Interface *intf);
    void DelInterfaceStatsEntry(const Interface *intf);
    void AddUpdateVrfStatsEntry(const VrfEntry *intf);
    void DelVrfStatsEntry(const VrfEntry *intf);
    void AddFlow(const FlowAceStatsRequest *req);
    void DeleteFlow(const FlowAceStatsRequest *req);

    VrfIdToVrfStatsTree vrf_stats_tree_;
    InterfaceStatsTree if_stats_tree_;
    FlowAceTree flow_ace_tree_;
    AgentDropStats drop_stats_;
    DBTableBase::ListenerId vrf_listener_id_;
    DBTableBase::ListenerId intf_listener_id_;
    Agent *agent_;
    WorkQueue<boost::shared_ptr<FlowAceStatsRequest> > request_queue_;
    DISALLOW_COPY_AND_ASSIGN(StatsManager);
};
#endif  // _ROOT_STATS_MANAGER_H_
