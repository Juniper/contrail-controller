/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _ROOT_STATS_MANAGER_H_
#define _ROOT_STATS_MANAGER_H_

#include <cmn/agent_cmn.h>
#include <cmn/agent_stats.h>
#include <oper/vrf.h>
#include <oper/interface.h>
#include <vrouter_types.h>
#include <string>
#include <map>
#include <utility>
#include <uve/flow_uve_stats_request.h>
#include <vr_types.h>
#include <uve/agent_uve.h>

struct FlowRateComputeInfo {
    uint64_t prev_time_;
    uint64_t prev_flow_created_;
    uint64_t prev_flow_aged_;

    FlowRateComputeInfo() : prev_time_(UTCTimestampUsec()),
        prev_flow_created_(0), prev_flow_aged_(0) {
    }
};

#define DEFAULT_FUVE_REQUEST_QUEUE_SIZE (4*1024*1024)

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
        void GetDiffStats(uint64_t *in_b, uint64_t *out_b) const;

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
        uint64_t stats_time;
        FlowRateComputeInfo flow_info;
        AgentStats::FlowCounters added;
        AgentStats::FlowCounters deleted;
        bool drop_stats_received;
        vr_drop_stats_req drop_stats;
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
        FlowUveFwPolicyInfo fw_policy_info;
        FlowUveVnAcePolicyInfo vn_ace_info;
        FlowRuleMatchInfo(const std::string &itf, const std::string &sg_rule,
                          const FlowUveFwPolicyInfo &fw_info,
                          const FlowUveVnAcePolicyInfo &nw_ace_info) :
            interface(itf), sg_rule_uuid(sg_rule), fw_policy_info(fw_info),
            vn_ace_info(nw_ace_info) {
        }
        bool IsFwPolicyInfoEqual(const FlowUveFwPolicyInfo &info) const {
            if (fw_policy_info.is_valid_ != info.is_valid_) {
                return false;
            }
            if (fw_policy_info.local_tagset_ != info.local_tagset_) {
                return false;
            }
            if (fw_policy_info.fw_policy_ != info.fw_policy_) {
                return false;
            }
            if (fw_policy_info.remote_tagset_ != info.remote_tagset_) {
                return false;
            }
            if (fw_policy_info.remote_prefix_ != info.remote_prefix_) {
                return false;
            }
            if (fw_policy_info.local_vn_ != info.local_vn_) {
                return false;
            }
            if (fw_policy_info.remote_vn_ != info.remote_vn_) {
                return false;
            }
            if (fw_policy_info.initiator_ != info.initiator_) {
                return false;
            }
            return true;
        }
        bool IsVnAceInfoEqual(const FlowUveVnAcePolicyInfo &info) const {
            if (vn_ace_info.vn_ != info.vn_) {
                return false;
            }
            if (vn_ace_info.nw_ace_uuid_ != info.nw_ace_uuid_) {
                return false;
            }
            return true;
        }
    };

    typedef std::map<const boost::uuids::uuid, FlowRuleMatchInfo> FlowAceTree;
    typedef std::pair<const boost::uuids::uuid, FlowRuleMatchInfo> FlowAcePair;

    explicit StatsManager(Agent *agent);
    virtual ~StatsManager();

    vr_drop_stats_req drop_stats() const { return drop_stats_; }
    void set_drop_stats(const vr_drop_stats_req &req) { drop_stats_ = req; }

    InterfaceStats* GetInterfaceStats(const Interface *intf);

    VrfStats* GetVrfStats(int vrf_id);
    std::string GetNamelessVrf() { return "__untitled__"; }
    int GetNamelessVrfId() { return -1; }
    void Shutdown(void);
    void RegisterDBClients();
    void InitDone();
    bool RequestHandler(boost::shared_ptr<FlowUveStatsRequest> req);
    void EnqueueEvent(const boost::shared_ptr<FlowUveStatsRequest> &req);
    bool BuildFlowRate(AgentStats::FlowCounters &created,
                       AgentStats::FlowCounters &aged,
                       FlowRateComputeInfo &flow_info,
                       VrouterFlowRate &flow_rate) const;
    void BuildDropStats(const vr_drop_stats_req &r,
                        AgentDropStats &ds) const;
    friend class AgentStatsCollectorTest;

 private:
    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    void InterfaceNotify(DBTablePartBase *part, DBEntryBase *e);
    void AddNamelessVrfStatsEntry();
    void AddInterfaceStatsEntry(const Interface *intf);
    void DelInterfaceStatsEntry(const Interface *intf);
    void AddUpdateVrfStatsEntry(const VrfEntry *intf);
    void DelVrfStatsEntry(const VrfEntry *intf);
    void AddFlow(const FlowUveStatsRequest *req);
    void DeleteFlow(const FlowUveStatsRequest *req);
    bool FlowStatsUpdate();

    VrfIdToVrfStatsTree vrf_stats_tree_;
    InterfaceStatsTree if_stats_tree_;
    FlowAceTree flow_ace_tree_;
    vr_drop_stats_req drop_stats_;
    DBTableBase::ListenerId vrf_listener_id_;
    DBTableBase::ListenerId intf_listener_id_;
    Agent *agent_;
    WorkQueue<boost::shared_ptr<FlowUveStatsRequest> > request_queue_;
    Timer *timer_;
    DISALLOW_COPY_AND_ASSIGN(StatsManager);
};
#endif  // _ROOT_STATS_MANAGER_H_
