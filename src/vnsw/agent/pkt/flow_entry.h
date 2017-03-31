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
#include <base/util.h>
#include <net/address.h>
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
struct FlowExportInfo;
class FlowStatsCollector;
class Token;
class FlowMgmtRequest;
class FlowEntryInfo;
typedef std::auto_ptr<FlowEntryInfo> FlowMgmtEntryInfoPtr;

////////////////////////////////////////////////////////////////////////////
// This is helper struct to carry parameters of reverse-flow. When flow is
// being deleted, the relationship between forward and reverse flows are
// broken. However, some info of reverse flow is needed during export of flows
// for FlowStatsCollector. This information of reverse flow is carried in the
// following struct.
////////////////////////////////////////////////////////////////////////////
struct RevFlowDepParams {
    uuid rev_uuid_;
    uuid rev_egress_uuid_;
    IpAddress sip_;
    std::string vmi_uuid_;
    std::string sg_uuid_;
    std::string vm_cfg_name_;

    RevFlowDepParams() : rev_uuid_(), rev_egress_uuid_(), sip_(), vmi_uuid_(),
                         sg_uuid_(), vm_cfg_name_() {
    }

    RevFlowDepParams(const uuid &rev_uuid, const uuid &rev_egress_uuid,
                     IpAddress sip,
                     const std::string &vmi_uuid,
                     const std::string &sg_uuid,
                     const std::string &vm_cfg_name) : rev_uuid_(rev_uuid),
        rev_egress_uuid_(rev_egress_uuid), sip_(sip), vmi_uuid_(vmi_uuid),
        sg_uuid_(sg_uuid), vm_cfg_name_(vm_cfg_name) {
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

    MatchAclParamsList m_out_sg_acl_l;
    bool out_sg_rule_present;
    uint32_t out_sg_action;

    MatchAclParamsList m_sg_acl_l;
    bool sg_rule_present;
    uint32_t sg_action;

    MatchAclParamsList m_reverse_sg_acl_l;
    bool reverse_sg_rule_present;
    uint32_t reverse_sg_action;

    MatchAclParamsList m_reverse_out_sg_acl_l;
    bool reverse_out_sg_rule_present;
    uint32_t reverse_out_sg_action;

    MatchAclParamsList m_mirror_acl_l;
    uint32_t mirror_action;

    MatchAclParamsList m_out_mirror_acl_l;
    uint32_t out_mirror_action;

    MatchAclParamsList m_vrf_assign_acl_l;
    uint32_t vrf_assign_acl_action;

    // Summary of SG actions
    uint32_t sg_action_summary;
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

    MacAddress smac;
    MacAddress dmac;
    std::string source_vn_match;
    std::string dest_vn_match;
    VnListType source_vn_list;
    VnListType dest_vn_list;
    SecurityGroupList source_sg_id_l;
    SecurityGroupList dest_sg_id_l;
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
    uint32_t bgp_as_a_service_port;
    uint32_t ttl;

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

    std::string vm_cfg_name;
    uint32_t acl_assigned_vrf_index_;
    uint32_t qos_config_idx;
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
        EVENT_MAX
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
        DROP_REVERSE_OUT_SG
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
        TcpAckFlow      = 1 << 10,
        UnknownUnicastFlood = 1 << 11,
        BgpRouterService   = 1 << 12,
        AliasIpFlow     = 1 << 13
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
    const std::string &sg_rule_uuid() const { return sg_rule_uuid_; }
    const std::string &nw_ace_uuid() const { return nw_ace_uuid_; }
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
    const uint32_t bgp_as_a_service_port() const {
        if (is_flags_set(FlowEntry::BgpRouterService))
            return data_.bgp_as_a_service_port;
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
    void SetPacketHeader(PacketHeader *hdr);
    void SetOutPacketHeader(PacketHeader *hdr);
    void set_deleted(bool deleted) { deleted_ = deleted; }
    void SetAclAction(std::vector<AclAction> &acl_action_l) const;
    void UpdateReflexiveAction();
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
    void set_last_event(uint32_t event) { last_event_ = event; }
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
    void set_flow_mgmt_info(FlowEntryInfo *info) {
        flow_mgmt_info_.reset(info);
    }
private:
    friend class FlowTable;
    friend class FlowEntryFreeList;
    friend class FlowStatsCollector;
    friend class KSyncFlowIndexManager;

    friend void intrusive_ptr_add_ref(FlowEntry *fe);
    friend void intrusive_ptr_release(FlowEntry *fe);

    void RpfInit(const AgentRoute *rt);
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
    void SetSgAclInfo(const FlowPolicyInfo &fwd_flow_info,
                      const FlowPolicyInfo &rev_flow_info, bool tcp_rev_sg);
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
    // IMPORTANT: Remember to update Reset() routine if new fields are added
    // IMPORTANT: Remember to update Copy() routine if new fields are added
};
 
void intrusive_ptr_add_ref(FlowEntry *fe);
void intrusive_ptr_release(FlowEntry *fe);

#endif //  __AGENT_PKT_FLOW_ENTRY_H__
