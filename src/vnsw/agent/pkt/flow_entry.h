/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_PKT_FLOW_ENTRY_H__
#define __AGENT_PKT_FLOW_ENTRY_H__

#include <boost/uuid/uuid_io.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/intrusive/list.hpp>
#include <tbb/atomic.h>
#include <tbb/mutex.h>
#include <tbb/recursive_mutex.h>
#include <base/util.h>
#include <base/address.h>
#include <db/db_table_walker.h>
#include <cmn/agent_cmn.h>
#include <oper/mirror_table.h>
#include <filter/traffic_action.h>
#include <filter/acl_entry.h>
#include <filter/acl.h>
#include <pkt/pkt_types.h>
#include <pkt/pkt_handler.h>
#include <pkt/pkt_init.h>
#include <pkt/pkt_flow_info.h>
#include <pkt/flow_token.h>
#include <sandesh/sandesh_trace.h>
#include <oper/global_vrouter.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/route_common.h>
#include <oper/sg.h>
#include <oper/vrf.h>
#include <filter/acl.h>
#include <sandesh/common/flow_types.h>

class FlowTableKSyncEntry;
class FlowEntry;
class FlowExportInfo;
class FlowStatsCollector;
class Token;
class FlowMgmtRequest;
class FlowEntryInfo;
struct FlowUveFwPolicyInfo;
struct FlowUveVnAcePolicyInfo;
typedef std::auto_ptr<FlowEntryInfo> FlowMgmtEntryInfoPtr;

////////////////////////////////////////////////////////////////////////////
// This is helper struct to carry parameters of reverse-flow. When flow is
// being deleted, the relationship between forward and reverse flows are
// broken. However, some info of reverse flow is needed during export of flows
// for FlowStatsCollector. This information of reverse flow is carried in the
// following struct.
////////////////////////////////////////////////////////////////////////////
struct RevFlowDepParams {
    boost::uuids::uuid rev_uuid_;
    boost::uuids::uuid rev_egress_uuid_;
    IpAddress sip_;
    std::string vmi_uuid_;
    std::string sg_uuid_;
    std::string vm_cfg_name_;
    uint16_t drop_reason_;
    std::string nw_ace_uuid_;
    FlowAction action_info_;

    RevFlowDepParams() : rev_uuid_(), rev_egress_uuid_(), sip_(), vmi_uuid_(),
                         sg_uuid_(), vm_cfg_name_(), drop_reason_(),
                         nw_ace_uuid_(), action_info_() {
    }

    RevFlowDepParams(const boost::uuids::uuid &rev_uuid,
                     const boost::uuids::uuid &rev_egress_uuid,
                     IpAddress sip,
                     const std::string &vmi_uuid,
                     const std::string &sg_uuid,
                     const std::string &vm_cfg_name,
                     uint16_t &drop_reason,
                     std::string &nw_ace_uuid,
                     FlowAction &action_info) : rev_uuid_(rev_uuid),
        rev_egress_uuid_(rev_egress_uuid), sip_(sip), vmi_uuid_(vmi_uuid),
        sg_uuid_(sg_uuid), vm_cfg_name_(vm_cfg_name),
        drop_reason_(drop_reason), nw_ace_uuid_(nw_ace_uuid),
        action_info_(action_info) {
    }
};

////////////////////////////////////////////////////////////////////////////
// Helper class to manage following,
// 1. VM referred by the flow
// 2. Per VM flow counters to apply per-vm flow limits
//    - Number of flows for a VM
//    - Number of linklocal flows for a VM
// 3. socket opened for linklocal flows
////////////////////////////////////////////////////////////////////////////
class VmFlowRef {
public:
    static const int kInvalidFd=-1;
    VmFlowRef();
    VmFlowRef(const VmFlowRef &rhs);
    ~VmFlowRef();

    void Init(FlowEntry *flow);
    void operator=(const VmFlowRef &rhs);
    void Reset(bool reset_flow);
    void FreeRef();
    void FreeFd();
    void SetVm(const VmEntry *vm);
    bool AllocateFd(Agent *agent, uint8_t l3_proto);
    void Move(VmFlowRef *rhs);

    int fd() const { return fd_; }
    uint16_t port() const { return port_; }
    const VmEntry *vm() const { return vm_.get(); }
private:
    // IMPORTANT: Keep this structure assignable. Assignment operator is used in
    // FlowEntry::Copy() on this structure
    VmEntryConstRef vm_;
    int fd_;
    uint16_t port_;
    FlowEntry *flow_;
};

typedef boost::intrusive_ptr<FlowEntry> FlowEntryPtr;

struct FlowKey {
    FlowKey() :
        family(Address::UNSPEC), nh(0), src_addr(Ip4Address(0)),
        dst_addr(Ip4Address(0)), protocol(0),
        src_port(0), dst_port(0){
    }

    FlowKey(uint32_t nh_p, const Ip4Address &sip_p, const Ip4Address &dip_p,
            uint8_t proto_p, uint16_t sport_p, uint16_t dport_p)
        : family(Address::INET), nh(nh_p), src_addr(sip_p), dst_addr(dip_p),
        protocol(proto_p), src_port(sport_p), dst_port(dport_p) {
    }

    FlowKey(uint32_t nh_p, const IpAddress &sip_p, const IpAddress &dip_p,
            uint8_t proto_p, uint16_t sport_p, uint16_t dport_p)
        : family(sip_p.is_v4() ? Address::INET : Address::INET6), nh(nh_p),
        src_addr(sip_p), dst_addr(dip_p), protocol(proto_p), src_port(sport_p),
        dst_port(dport_p) {
    }

    FlowKey(const FlowKey &key) :
        family(key.family), nh(key.nh), src_addr(key.src_addr),
        dst_addr(key.dst_addr), protocol(key.protocol), src_port(key.src_port),
        dst_port(key.dst_port) {
    }

    // Comparator for the flow-entry
    bool IsLess(const FlowKey &key) const {
        if (family != key.family)
            return family < key.family;

        if (nh != key.nh)
            return nh < key.nh;

        if (src_addr != key.src_addr)
            return src_addr < key.src_addr;

        if (dst_addr != key.dst_addr)
            return dst_addr < key.dst_addr;

        if (protocol != key.protocol)
            return protocol < key.protocol;

        if (src_port != key.src_port)
            return src_port < key.src_port;

        return dst_port < key.dst_port;
    }

    bool IsEqual(const FlowKey &key) const {
        if (family != key.family)
            return false;

        if (nh != key.nh)
            return false;

        if (src_addr != key.src_addr)
            return false;

        if (dst_addr != key.dst_addr)
            return false;

        if (protocol != key.protocol)
            return false;

        if (src_port != key.src_port)
            return false;

        if (dst_port != key.dst_port)
            return false;

        return true;
    }

    void Reset() {
        family = Address::UNSPEC;
        nh = -1;
        src_addr = Ip4Address(0);
        dst_addr = Ip4Address(0);
        protocol = -1;
        src_port = -1;
        dst_port = -1;
    }

    Address::Family family;
    uint32_t nh;
    IpAddress src_addr;
    IpAddress dst_addr;
    uint8_t protocol;
    uint16_t src_port;
    uint16_t dst_port;
};

typedef std::list<MatchAclParams> MatchAclParamsList;

struct SessionPolicy {
    void Reset();
    void ResetAction();
    void ResetPolicy();
    void ResetRuleMatchInfo();

    MatchAclParamsList m_acl_l;
    bool rule_present;
    uint32_t action;

    MatchAclParamsList m_out_acl_l;
    bool out_rule_present;
    uint32_t out_action;

    MatchAclParamsList m_reverse_acl_l;
    bool reverse_rule_present;
    uint32_t reverse_action;

    MatchAclParamsList m_reverse_out_acl_l;
    bool reverse_out_rule_present;
    uint32_t reverse_out_action;

    std::string rule_uuid_;
    std::string acl_name_;
    uint32_t action_summary;
};

// IMPORTANT: Keep this structure assignable. Assignment operator is used in
// FlowEntry::Copy() on this structure
struct MatchPolicy {
    MatchPolicy();
    ~MatchPolicy();

    void Reset();

    // IMPORTANT: Keep this structure assignable.
    MatchAclParamsList m_acl_l;
    uint32_t policy_action;

    MatchAclParamsList m_out_acl_l;
    uint32_t out_policy_action;

    SessionPolicy sg_policy;
    SessionPolicy aps_policy;
    SessionPolicy fwaas_policy;

    MatchAclParamsList m_mirror_acl_l;
    uint32_t mirror_action;

    MatchAclParamsList m_out_mirror_acl_l;
    uint32_t out_mirror_action;

    MatchAclParamsList m_vrf_assign_acl_l;
    uint32_t vrf_assign_acl_action;

    FlowAction action_info;
};

// IMPORTANT: Keep this structure assignable. Assignment operator is used in
// FlowEntry::Copy() on this structure
struct FlowData {
    FlowData();
    ~FlowData();

    void Reset();
    std::vector<std::string> SourceVnList() const;
    std::vector<std::string> DestinationVnList() const;
    std::vector<std::string> OriginVnSrcList() const;
    std::vector<std::string> OriginVnDstList() const;

    MacAddress smac;
    MacAddress dmac;
    std::string source_vn_match;
    std::string dest_vn_match;
    std::string origin_vn_src;
    std::string origin_vn_dst;
    VnListType source_vn_list;
    VnListType dest_vn_list;
    VnListType origin_vn_src_list;
    VnListType origin_vn_dst_list;
    SecurityGroupList source_sg_id_l;
    SecurityGroupList dest_sg_id_l;
    TagList source_tag_id_l;
    TagList dest_tag_id_l;
    uint32_t flow_source_vrf;
    uint32_t flow_dest_vrf;

    MatchPolicy match_p;
    VnEntryConstRef vn_entry;
    InterfaceConstRef intf_entry;
    VmFlowRef  in_vm_entry;
    VmFlowRef  out_vm_entry;
    NextHopConstRef src_ip_nh;
    uint32_t vrf;
    uint32_t mirror_vrf;
    uint32_t dest_vrf;
    uint32_t component_nh_idx;
    uint32_t bgp_as_a_service_sport;
    uint32_t bgp_as_a_service_dport;
    boost::uuids::uuid bgp_health_check_uuid;
    uint32_t ttl;
    // In case of policy on fabric, the forwarding happens in
    // agent_->fabric_vrf(), but policy processing must happen in
    // agent_->fabric_policy_vrf(). Storing the route infor for
    // fabric_policy_vrf() for tracking purpose
    uint32_t src_policy_vrf;
    uint32_t src_policy_plen;
    uint32_t dst_policy_vrf;
    uint32_t dst_policy_plen;

    // Stats
    uint8_t source_plen;
    uint8_t dest_plen;
    uint16_t drop_reason;
    bool vrf_assign_evaluated;
    uint32_t            if_index_info;
    TunnelInfo          tunnel_info;
    // map for references to the routes which were ignored due to more specific
    // route this will be used to trigger flow re-compute to use more specific
    // on route add. key for the map is vrf and data is prefix length
    FlowRouteRefMap     flow_source_plen_map;
    FlowRouteRefMap     flow_dest_plen_map;

    // RPF related
    bool enable_rpf;
    // RPF NH for the flow
    NextHopConstRef rpf_nh;
    // When RPF is derived from a INET route, flow-management uses VRF and plen
    // below to track the route for any NH change
    // rpf_vrf will be VrfEntry::kInvalidIndex if flow uses l2-route for RPF
    uint32_t rpf_vrf;
    uint8_t rpf_plen;

    bool disable_validation; // ignore RPF on specific flows (like BFD health check)

    std::string vm_cfg_name;
    uint32_t acl_assigned_vrf_index_;
    uint32_t qos_config_idx;
    uint16_t allocated_port_;
    // IMPORTANT: Keep this structure assignable. Assignment operator is used in
    // FlowEntry::Copy() on this structure
};

struct FlowEventLog {
    enum Event {
        FLOW_ADD,
        FLOW_UPDATE,
        FLOW_DELETE,
        FLOW_EVICT,
        FLOW_HANDLE_ASSIGN,
        FLOW_MSG_SKIP_EVICTED,
        EVENT_MAXIMUM
    };

    FlowEventLog();
    ~FlowEventLog();

    uint64_t time_;
    Event event_;
    uint32_t flow_handle_;
    uint8_t flow_gen_id_;
    FlowTableKSyncEntry *ksync_entry_;
    uint32_t hash_id_;
    uint8_t gen_id_;
    uint32_t vrouter_flow_handle_;
    uint8_t vrouter_gen_id_;
};

// There are 4 actions supported,
// Flow recomputation goes thru 2 stages of processing,
//
// - recompute_dbentry_ : In this stage, flow is enqueued to flow-update-queue
//                        as a result of db-entry add/delete/change.
// - recompute_         : In this stage, flow is enqueued to flow-event-queue
//                        for recomputation of flow
// - delete_            : Specifies that delete action is pending on flow.
// - recompute_         : Specifies that flow is enqueued into flow-event-queue
//                        for recomputation.
//
// The actions have a priority, the higher priorty action overrides lower
// priority actions. The priority in decreasing order is,
// - delete_
// - recompute_
// - recompute_dbentry_
// - revaluate_
//
// The flags are also used for state-compression of objects. The state
// compression is acheived with,
//
// - Before Event Enqueue :
//   Before enqueuing an event, the FlowEvent module checks if the
//   corresponding action or higher priority action is pending. If so, the
//   event is ignored.
//   Note, if the lower priority event is pending, the higher priority event
//   is still enqueued. The lower priority event is ignored later as given below
//
// - On Event dequeue :
//   After dequeuing an event, FlowEvent module checks if a higher priority
//   event is pending. If so, the current event is ignored.
//
// - Post Event processing:
//   Once the event is processed, the corresponding action is cleared for both
//   forward and reverse flows. Clearing an action also clears lower priority
//   actions
class FlowPendingAction {
public:
    FlowPendingAction();
    ~FlowPendingAction();

    void Reset();

    bool CanDelete();
    bool SetDelete();
    void ResetDelete();

    bool CanRecompute();
    bool SetRecompute();
    void ResetRecompute();

    bool CanRecomputeDBEntry();
    bool SetRecomputeDBEntry();
    void ResetRecomputeDBEntry();

    bool CanRevaluate();
    bool SetRevaluate();
    void ResetRevaluate();
private:
    // delete pending
    bool delete_;
    // Flow pending complete recompute
    bool recompute_;
    // Flow pending recompute-dbentry
    bool recompute_dbentry_;
    // Flow pending revaluation due to change in interface, vn, acl and nh
    bool revaluate_;
};

class FlowEntry {
    public:
    enum FlowShortReason {
        /* Please update FlowEntry::FlowDropReasonStr whenever entries are added
         * to the below enum */
        SHORT_UNKNOWN = 0,
        SHORT_UNAVIALABLE_INTERFACE,
        SHORT_IPV4_FWD_DIS,
        SHORT_UNAVIALABLE_VRF,
        SHORT_NO_SRC_ROUTE,
        SHORT_NO_DST_ROUTE,
        SHORT_AUDIT_ENTRY,
        SHORT_VRF_CHANGE,
        SHORT_NO_REVERSE_FLOW,
        SHORT_REVERSE_FLOW_CHANGE,
        SHORT_NAT_CHANGE,
        SHORT_FLOW_LIMIT,
        SHORT_LINKLOCAL_SRC_NAT,
        SHORT_FAILED_VROUTER_INSTALL,
        SHORT_INVALID_L2_FLOW,
        SHORT_FLOW_ON_TSN,
        SHORT_NO_MIRROR_ENTRY,
        SHORT_SAME_FLOW_RFLOW_KEY,
        SHORT_PORT_MAP_DROP,
        SHORT_NO_SRC_ROUTE_L2RPF,
        SHORT_FAT_FLOW_NAT_CONFLICT,
        SHORT_MAX
    };

    enum FlowDropReason {
        /* Please update FlowEntry::FlowDropReasonStr whenever entries are added
         * to the below enum */
        DROP_UNKNOWN = 0,
        DROP_POLICY = SHORT_MAX,
        DROP_OUT_POLICY,
        DROP_SG,
        DROP_OUT_SG,
        DROP_REVERSE_SG,
        DROP_REVERSE_OUT_SG,
        DROP_FIREWALL_POLICY,
        DROP_OUT_FIREWALL_POLICY,
        DROP_REVERSE_FIREWALL_POLICY,
        DROP_REVERSE_OUT_FIREWALL_POLICY,
        DROP_FWAAS_POLICY,
        DROP_FWAAS_OUT_POLICY,
        DROP_FWAAS_REVERSE_POLICY,
        DROP_FWAAS_REVERSE_OUT_POLICY,
    };

    enum FlowPolicyState {
        NOT_EVALUATED,
        IMPLICIT_ALLOW, /* Due to No Acl rules */
        IMPLICIT_DENY,
        DEFAULT_GW_ICMP_OR_DNS, /* DNS/ICMP pkt to/from default gateway */
        LINKLOCAL_FLOW, /* No policy applied for linklocal flow */
        MULTICAST_FLOW, /* No policy applied for multicast flow */
        BGPROUTERSERVICE_FLOW, /* No policy applied for bgp router service flow */
        NON_IP_FLOW,    /* Flow due to bridging */
    };

    static const uint32_t kInvalidFlowHandle=0xFFFFFFFF;
    static const uint8_t kMaxMirrorsPerFlow=0x2;
    static const std::map<FlowPolicyState, const char*> FlowPolicyStateStr;
    static const std::map<uint16_t, const char*> FlowDropReasonStr;
    static const uint32_t kFlowRetryAttempts = 5;
    // Don't go beyond PCAP_END, pcap type is one byte
    enum PcapType {
        PCAP_CAPTURE_HOST = 1,
        PCAP_FLAGS = 2,
        PCAP_SOURCE_VN = 3,
        PCAP_DEST_VN = 4,
        PCAP_TLV_END = 255
    };

    enum FlowEntryFlags {
        NatFlow         = 1 << 0,
        LocalFlow       = 1 << 1,
        ShortFlow       = 1 << 2,
        LinkLocalFlow   = 1 << 3,
        ReverseFlow     = 1 << 4,
        EcmpFlow        = 1 << 5,
        IngressDir      = 1 << 6,
        Trap            = 1 << 7,
        Multicast       = 1 << 8,
        // a local port bind is done (used as as src port for linklocal nat)
        LinkLocalBindLocalSrcPort = 1 << 9,
        TcpAckFlow                = 1 << 10,
        UnknownUnicastFlood       = 1 << 11,
        BgpRouterService          = 1 << 12,
        AliasIpFlow               = 1 << 13,
        FabricControlFlow         = 1 << 14,
        FabricFlow                = 1 << 15,
        HbfFlow                   = 1 << 16
    };

    enum HbsInterface {
        HBS_INTERFACE_INVALID  = 0,
        HBS_INTERFACE_LEFT     = 1,
        HBS_INTERFACE_RIGHT    = 2
    };

    FlowEntry(FlowTable *flow_table);
    virtual ~FlowEntry();

    void Reset(const FlowKey &k);
    void Reset();

    // Copy data fields from rhs
    void Copy(FlowEntry *rhs, bool update);

    void InitFwdFlow(const PktFlowInfo *info, const PktInfo *pkt,
                     const PktControlInfo *ctrl,
                     const PktControlInfo *rev_ctrl, FlowEntry *rflow,
                     Agent *agent);
    void InitRevFlow(const PktFlowInfo *info, const PktInfo *pkt,
                     const PktControlInfo *ctrl,
                     const PktControlInfo *rev_ctrl, FlowEntry *rflow,
                     Agent *agent);
    void InitAuditFlow(uint32_t flow_idx, uint8_t gen_id);
    static void Init();

    static AgentRoute *GetL2Route(const VrfEntry *entry, const MacAddress &mac);
    static AgentRoute *GetUcRoute(const VrfEntry *entry, const IpAddress &addr);
    static AgentRoute *GetEvpnRoute(const VrfEntry *entry, const MacAddress &mac,
                                    const IpAddress &addr, uint32_t ethernet_tag);
    static const SecurityGroupList &default_sg_list() {
        return default_sg_list_;
    }
    static FlowEntry *Allocate(const FlowKey &key, FlowTable *flow_table);
    static bool ShouldDrop(uint32_t action);

    // Flow accessor routines
    int GetRefCount() { return refcount_; }
    const FlowKey &key() const { return key_;}
    FlowData &data() { return data_;}
    const FlowData &data() const { return data_;}
    FlowTable *flow_table() const { return flow_table_; }
    bool l3_flow() const { return l3_flow_; }
    uint8_t gen_id() const { return gen_id_; }
    uint32_t flow_handle() const { return flow_handle_; }
    void set_flow_handle(uint32_t flow_handle, uint8_t gen_id);
    FlowEntry *reverse_flow_entry() { return reverse_flow_entry_.get(); }
    uint32_t flags() const { return flags_; }
    const FlowEntry *reverse_flow_entry() const {
        return reverse_flow_entry_.get();
    }
    void set_reverse_flow_entry(FlowEntry *reverse_flow_entry) {
        reverse_flow_entry_ = reverse_flow_entry;
    }
    bool is_flags_set(const FlowEntryFlags &flags) const {
        return (flags_ & flags);
    }
    void set_flags(const FlowEntryFlags &flags) { flags_ |= flags; }
    void reset_flags(const FlowEntryFlags &flags) { flags_ &= ~flags; }
    void set_source_sg_id_l(const SecurityGroupList &sg_l) {
        data_.source_sg_id_l = sg_l;
    }
    void set_dest_sg_id_l(const SecurityGroupList &sg_l) {
        data_.dest_sg_id_l = sg_l;
    }
    int linklocal_src_port() const { return data_.in_vm_entry.port(); }
    int linklocal_src_port_fd() const { return data_.in_vm_entry.fd(); }
    const std::string& acl_assigned_vrf() const;
    void set_acl_assigned_vrf_index();
    uint32_t acl_assigned_vrf_index() const;
    uint32_t fip() const { return fip_; }
    VmInterfaceKey fip_vmi() const { return fip_vmi_; }
    uint32_t reverse_flow_fip() const;
    VmInterfaceKey reverse_flow_vmi() const;
    void UpdateFipStatsInfo(uint32_t fip, uint32_t id, Agent *agent);
    const boost::uuids::uuid &uuid() const { return uuid_; }
    const boost::uuids::uuid &egress_uuid() const { return egress_uuid_;}
    const std::string &sg_rule_uuid() const {
        return data_.match_p.sg_policy.rule_uuid_;
    }
    const std::string &nw_ace_uuid() const { return nw_ace_uuid_; }
    const std::string fw_policy_name_uuid() const;
    const std::string fw_policy_uuid() const;
    const std::string RemotePrefix() const;
    const TagList &remote_tagset() const;
    const TagList &local_tagset() const;
    const std::string &peer_vrouter() const { return peer_vrouter_; }
    TunnelType tunnel_type() const { return tunnel_type_; }

    uint16_t short_flow_reason() const { return short_flow_reason_; }
    const MacAddress &smac() const { return data_.smac; }
    const MacAddress &dmac() const { return data_.dmac; }
    bool on_tree() const { return on_tree_; }
    void set_on_tree() { on_tree_ = true; }
    tbb::mutex &mutex() { return mutex_; }

    const Interface *intf_entry() const { return data_.intf_entry.get(); }
    const VnEntry *vn_entry() const { return data_.vn_entry.get(); }
    VmFlowRef *in_vm_flow_ref() { return &(data_.in_vm_entry); }
    const VmEntry *in_vm_entry() const { return data_.in_vm_entry.vm(); }
    const VmEntry *out_vm_entry() const { return data_.out_vm_entry.vm(); }
    const NextHop *src_ip_nh() const { return data_.src_ip_nh.get(); }
    const NextHop *rpf_nh() const { return data_.rpf_nh.get(); }
    uint32_t GetEcmpIndex() const { return data_.component_nh_idx; }
    const uint32_t bgp_as_a_service_sport() const {
        if (is_flags_set(FlowEntry::BgpRouterService))
            return data_.bgp_as_a_service_sport;
        return 0;
    }
    const uint32_t bgp_as_a_service_dport() const {
        if (is_flags_set(FlowEntry::BgpRouterService))
            return data_.bgp_as_a_service_dport;
        return 0;
    }
    const MatchPolicy &match_p() const { return data_.match_p; }

    bool ActionSet(TrafficAction::Action action) const {
        return ((data_.match_p.action_info.action &
                 (1 << action)) ? true : false);
    }
    bool ImplicitDenyFlow() const {
        return ((data_.match_p.action_info.action &
                 (1 << TrafficAction::IMPLICIT_DENY)) ? true : false);
    }
    bool deleted() { return deleted_; }

    bool IsShortFlow() const { return is_flags_set(FlowEntry::ShortFlow); }
    bool IsEcmpFlow() const { return is_flags_set(FlowEntry::EcmpFlow); }
    bool IsNatFlow() const { return is_flags_set(FlowEntry::NatFlow); }
    bool IsIngressFlow() const { return is_flags_set(FlowEntry::IngressDir); }
    // Flow action routines
    void ResyncFlow();
    void RpfUpdate();
    bool ActionRecompute();
    bool DoPolicy();
    void MakeShortFlow(FlowShortReason reason);
    void SetMirrorVrfFromAction();
    void SetHbsInfofromAction();
    void SetVrfAssignEntry();
    void ComputeReflexiveAction();
    uint32_t MatchAcl(const PacketHeader &hdr,
                      MatchAclParamsList &acl, bool add_implicit_deny,
                      bool add_implicit_allow, FlowPolicyInfo *info);
    void ResetPolicy();

    void FillFlowInfo(FlowInfo &info) const;
    void GetPolicyInfo(const VnEntry *vn, const FlowEntry *rflow);
    void GetPolicyInfo(const FlowEntry *rflow);
    void GetPolicyInfo(const VnEntry *vn);
    void GetPolicyInfo();
    void UpdateL2RouteInfo();
    void GetVrfAssignAcl();
    void SetMirrorVrf(const uint32_t id) {data_.mirror_vrf = id;}

    void GetPolicy(const VnEntry *vn, const FlowEntry *rflow);
    void GetNonLocalFlowSgList(const VmInterface *vm_port);
    void GetLocalFlowSgList(const VmInterface *vm_port,
                            const VmInterface *reverse_vm_port);
    void GetSgList(const Interface *intf);
    void GetApplicationPolicySet(const Interface *intf,
                                 const FlowEntry *rflow);
    void SetPacketHeader(PacketHeader *hdr);
    void SetOutPacketHeader(PacketHeader *hdr);
    void set_deleted(bool deleted) { deleted_ = deleted; }
    void SetAclAction(std::vector<AclAction> &acl_action_l) const;
    void UpdateReflexiveAction();
    bool IsFabricControlFlow() const;
    void SetAclFlowSandeshData(const AclDBEntry *acl,
                               FlowSandeshData &fe_sandesh_data,
                               Agent *agent) const;
    uint32_t InterfaceKeyToId(Agent *agent, const VmInterfaceKey &key);
    FlowTableKSyncEntry *ksync_entry() { return ksync_entry_; }
    FlowStatsCollector* fsc() const {
        return fsc_;
    }

    void set_fsc(FlowStatsCollector *fsc) {
        fsc_ = fsc;
    }
    static std::string DropReasonStr(uint16_t reason);
    std::string KeyString() const;
    void SetEventSandeshData(SandeshFlowIndexInfo *info);
    void LogFlow(FlowEventLog::Event event, FlowTableKSyncEntry* ksync,
                 uint32_t flow_handle, uint8_t gen_id);
    void RevFlowDepInfo(RevFlowDepParams *params);
    uint32_t last_event() const { return last_event_; }
    void set_last_event(uint32_t event) {
        last_event_ = event;
        e_history_.UpdateEvtHistory(event);
    }
    uint8_t GetMaxRetryAttempts() { return flow_retry_attempts_; }
    void  IncrementRetrycount() { flow_retry_attempts_++;}
    void ResetRetryCount(){ flow_retry_attempts_ = 0; }
    bool IsOnUnresolvedList(){ return is_flow_on_unresolved_list;}
    void SetUnResolvedList(bool added){ is_flow_on_unresolved_list = added;}
    FlowPendingAction *GetPendingAction() { return &pending_actions_; }
    bool trace() const { return trace_; }
    void set_trace(bool val) { trace_ = val; }

    FlowMgmtRequest *flow_mgmt_request() const { return flow_mgmt_request_; }
    void set_flow_mgmt_request(FlowMgmtRequest *req) {
        flow_mgmt_request_ = req;
    }

    FlowEntryInfo *flow_mgmt_info() const { return flow_mgmt_info_.get(); }
    void set_flow_mgmt_info(FlowEntryInfo *info);
    void FillUveFwStatsInfo(FlowUveFwPolicyInfo *info, bool added) const;
    void FillUveVnAceInfo(FlowUveVnAcePolicyInfo *info) const;
    bool IsClientFlow();
    bool IsServerFlow();
    uint16_t allocated_port() {
        return data_.allocated_port_;
    }
    void IncrementTransactionId() { transaction_id_++;}
    uint32_t GetTransactionId() {return transaction_id_;}
    void SetHbsInterface (HbsInterface intf) { hbs_intf_ = intf; }
    HbsInterface GetHbsInterface() { return hbs_intf_; }
private:
    friend class FlowTable;
    friend class FlowEntryFreeList;
    friend class FlowStatsCollector;
    friend class KSyncFlowIndexManager;

    friend void intrusive_ptr_add_ref(FlowEntry *fe);
    friend void intrusive_ptr_release(FlowEntry *fe);

    void FillUveLocalRevFlowStatsInfo(FlowUveFwPolicyInfo *info, bool added)
        const;
    void FillUveFwdFlowStatsInfo(FlowUveFwPolicyInfo *info, bool added) const;
    void RpfInit(const AgentRoute *rt, const IpAddress &sip);
    void RpfSetRpfNhFields(const NextHop *rpf_nh);
    void RpfSetRpfNhFields(const AgentRoute *rt, const NextHop *rpf_nh);
    void RpfSetSrcIpNhFields(const AgentRoute *rt, const NextHop *src_ip_nh);
    bool RpfFromSrcIpNh() const;
    void RpfComputeEgress();
    void RpfComputeIngress();

    bool InitFlowCmn(const PktFlowInfo *info, const PktControlInfo *ctrl,
                     const PktControlInfo *rev_ctrl, FlowEntry *rflow);
    VmInterfaceKey InterfaceIdToKey(Agent *agent, uint32_t id);
    void GetSourceRouteInfo(const AgentRoute *rt);
    void GetDestRouteInfo(const AgentRoute *rt);
    const std::string InterfaceIdToVmCfgName(Agent *agent, uint32_t id);
    const VrfEntry *GetDestinationVrf() const;
    bool SetQosConfigIndex();
    void SetAclInfo(SessionPolicy *sp, SessionPolicy *rsp,
                    const FlowPolicyInfo &fwd_flow_info,
                    const FlowPolicyInfo &rev_flow_info, bool tcp_rev,
                    bool is_sg);
    void SessionMatch(SessionPolicy *sp, SessionPolicy *rsp, bool is_sg);
    void UpdateReflexiveAction(SessionPolicy *sp, SessionPolicy *rsp);
    const std::string BuildRemotePrefix(const FlowRouteRefMap &rt_list,
                                        uint32_t vr, const IpAddress &ip) const;

    FlowKey key_;
    FlowTable *flow_table_;
    FlowData data_;
    bool l3_flow_;
    uint8_t gen_id_;
    uint32_t flow_handle_;
    FlowEntryPtr reverse_flow_entry_;
    static tbb::atomic<int> alloc_count_;
    bool deleted_;
    uint32_t flags_;
    uint16_t short_flow_reason_;
    boost::uuids::uuid uuid_;
    boost::uuids::uuid egress_uuid_;
    std::string sg_rule_uuid_;
    std::string nw_ace_uuid_;
    //IP address of the src vrouter for egress flows and dst vrouter for
    //ingress flows. Used only during flow-export
    std::string peer_vrouter_;
    //Underlay IP protocol type. Used only during flow-export
    TunnelType tunnel_type_;
    // Is flow-entry on the tree
    bool on_tree_;
    // Following fields are required for FIP stats accounting
    uint32_t fip_;
    VmInterfaceKey fip_vmi_;
    // Ksync entry for the flow
    FlowTableKSyncEntry *ksync_entry_;
    // atomic refcount
    tbb::atomic<int> refcount_;
    tbb::mutex mutex_;
    boost::intrusive::list_member_hook<> free_list_node_;
    FlowStatsCollector *fsc_;
    uint32_t last_event_;
    bool trace_;
    boost::scoped_array<FlowEventLog> event_logs_;
    uint16_t event_log_index_;
    FlowPendingAction pending_actions_;
    static SecurityGroupList default_sg_list_;
    uint8_t flow_retry_attempts_;
    bool is_flow_on_unresolved_list;
    // flow_mgmt_request used for compressing events to flow-mgmt queue.
    // flow_mgmt_request_ is set when flow is enqueued to flow-mgmt queue. No
    // subsequent enqueues are done till this field is set. The request can be
    // updated with new values to reflect latest state
    FlowMgmtRequest *flow_mgmt_request_;

    // Field used by flow-mgmt module. Its stored here to optimize flow-mgmt
    // and avoid lookups
    FlowMgmtEntryInfoPtr flow_mgmt_info_;
    const std::string fw_policy_;
    // Transaction id is used to detect old/stale vrouter add-ack response for
    // reverse flow handle allocation requests. It can happen if flow are
    // evicted from vrouter just after add-ack response sent to agent
    // and same flows are created before add-ack response gets processed
    // in agent.
    // transaction id should not be copied, it is incremented when flow entry
    // is reused.
    uint32_t transaction_id_;
    class FlowEntryEventHistory {
        public:
            FlowEntryEventHistory() {
                idx_ = 0;
                for (uint32_t i = 0; i < size_; i++) {
                      last_events_[i] = 0;
                }
            }

            void UpdateEvtHistory( uint32_t event ) {
                last_events_[idx_] = event;
                idx_ = (idx_+1) % size_;
            }

        private:
            static const uint32_t size_ = 5;
            uint32_t last_events_[size_];
            uint32_t idx_;
    };
    // Not modifying on Reset and Copy to retain the history
    FlowEntryEventHistory e_history_;
    HbsInterface          hbs_intf_;
    // IMPORTANT: Remember to update Reset() routine if new fields are added
    // IMPORTANT: Remember to update Copy() routine if new fields are added
};

void intrusive_ptr_add_ref(FlowEntry *fe);
void intrusive_ptr_release(FlowEntry *fe);

//A bound source port could be reused across different
//destination IP address, Port class holds a list of
//destination IP which have used this particular source
//port
class Port {
public:
    Port() :
        port_(0) {}
    Port(uint16_t port) : port_(port) {}
    ~Port() {}

    uint16_t port() const {
        return port_;
    }

    virtual uint16_t Bind() = 0;
protected:
    uint16_t port_;
};

class TcpPort : public Port {
public:
    TcpPort(boost::asio::io_service &io, uint16_t port):
        Port(port), socket_(io) {}
    ~TcpPort();

    virtual uint16_t Bind();
private:
    boost::asio::ip::tcp::socket socket_;
};

class UdpPort : public Port {
public:
    UdpPort(boost::asio::io_service &io, uint16_t port):
        Port(port), socket_(io) {}
    ~UdpPort();

    virtual uint16_t Bind();
private:
    boost::asio::ip::udp::socket socket_;
};

//Given a flow key gives the port used
//This would fail on first attempt or if the
//flow has been idle for long time
class PortCacheEntry {
public:
    PortCacheEntry(const FlowKey &key,
                   const uint16_t port):
        key_(key), port_(port), stale_(false) {}

    const FlowKey& key() const {
        return key_;
    }

    uint16_t port() const {
        return port_;
    }

    void set_stale(bool stale) const {
        stale_ = stale;
    }

    void MarkDelete() const;
    bool operator<(const PortCacheEntry &rhs) const;
    bool CanBeAged(uint64_t current_time, uint64_t timeout) const;
private:
    FlowKey  key_;
    uint16_t port_;
    mutable bool stale_;
    mutable uint64_t delete_time_;
};

class PortTable;

//Maintain a cache table which would be used to check
//if a flow prexisted and reuse the port if yes
class PortCacheTable {
public:
    static const uint64_t kCacheAging = 1000;
    static const uint64_t kAgingTimeout = 1000 * 1000 * 600; //10 minutes

    typedef std::set<PortCacheEntry> PortCacheEntryList;
    typedef std::map<uint16_t , PortCacheEntryList> PortCacheTree;

    PortCacheTable(PortTable *table);
    ~PortCacheTable();

    void Add(const PortCacheEntry &cache_entry);
    void Delete(const PortCacheEntry &cache_entry);
    const PortCacheEntry* Find(const FlowKey &key) const;
    void MarkDelete(const PortCacheEntry &cache_entry);

    void set_timeout(uint64_t timeout) {
        timeout_ = timeout;
    }

private:
    void StartTimer();
    void StopTimer();
    bool Age();

    PortCacheTree tree_;
    PortTable *port_table_;
    Timer* timer_;
    uint16_t hash_;
    uint64_t timeout_;
};

//Per protocol table to manage port allocation
class PortTable {
public:
    const static uint8_t kInvalidPort = 0;

    typedef boost::shared_ptr<Port> PortPtr;
    typedef IndexVector<PortPtr> PortList;

    typedef std::map<uint16_t, uint16_t> PortToBitIndexMap;
    typedef std::pair<uint16_t, uint16_t> PortToBitIndexPair;

    typedef IndexVector<FlowKey> PortBitMap;
    typedef boost::shared_ptr<PortBitMap> PortBitMapPtr;
    typedef std::vector<PortBitMapPtr> PortHashTable;

    PortTable(Agent *agent, uint32_t bucket_size, uint8_t protocol);
    ~PortTable();

    uint16_t Allocate(const FlowKey &key);
    void Free(const FlowKey &key, uint16_t port, bool release);
    uint16_t HashFlowKey(const FlowKey &key);

    Agent *agent() {
        return agent_;
    }

    uint16_t port_count() const {
        return port_config_.port_count;
    }

    void set_timeout(uint64_t timeout) {
        cache_.set_timeout(timeout);
    }

    tbb::recursive_mutex& mutex() {
        return mutex_;
    }

    void UpdatePortConfig(const PortConfig *port_config);

    uint16_t GetPortIndex(uint16_t port) const;
    const PortConfig* port_config() const {
        return &port_config_;
    }

    std::vector<uint16_t> GetPortList() const {
        tbb::recursive_mutex::scoped_lock lock(mutex_);
        std::vector<uint16_t> port_list;
        PortToBitIndexMap::const_iterator it = port_to_bit_index_.begin();
        for(; it != port_to_bit_index_.end(); it++) {
            port_list.push_back(it->first);
        }
        return port_list;
    }

    void GetFlowKeyList(uint16_t port, std::vector<FlowKey> &key) const;
private:
    Agent *agent_;
    PortPtr CreatePortEntry(uint16_t port_no);
    //Create a port with given port_no
    void AddPort(uint16_t port_no);
    void DeletePort(uint16_t port_no);
    void Relocate(uint16_t port_no);
    bool IsValidPort(uint16_t port, uint16_t count);
    void DeleteAllFlow(uint16_t port, uint16_t index);
    bool HandlePortConfig(const PortConfig &pc);

    uint8_t protocol_;
    //Holds freed bit entry in table for while so that
    //flow could be re-established after aging
    PortCacheTable cache_;

    //Max no of hash entries, higher the number
    //lesser the chance of clash. Hash would be derived based on
    //destination IP and port
    uint16_t hash_table_size_;

    //A Given a hash holds a list of used port numbers
    //Free Bit index is to be used in Port tree to get actual port value
    PortHashTable hash_table_;

    //Mapping from bit vector offset to actual port number
    PortList port_list_;

    //Mapping from port to bit index for easier auditing on config
    //change
    PortToBitIndexMap port_to_bit_index_;

    //Number of port that agent can bind on
    PortConfig port_config_;
    mutable tbb::recursive_mutex mutex_;
    std::auto_ptr<TaskTrigger> task_trigger_;
};

class PortTableManager {
public:
    typedef boost::shared_ptr<PortTable> PortTablePtr;
    PortTableManager(Agent *agent, uint16_t hash_table_size);
    ~PortTableManager();

    uint16_t Allocate(const FlowKey &key);
    void Free(const FlowKey &key, uint16_t port, bool release);

    void UpdatePortConfig(uint8_t protocol, const PortConfig *config);
    static void PortConfigHandler(Agent *agent, uint8_t protocol,
                                  const PortConfig *pc);
    const PortTable* GetPortTable(uint8_t proto) {
        return port_table_list_[proto].get();
    }
private:
    Agent *agent_;
    PortTablePtr port_table_list_[IPPROTO_MAX];
};
#endif //  __AGENT_PKT_FLOW_ENTRY_H__
