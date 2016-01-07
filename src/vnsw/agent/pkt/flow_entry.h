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
class KSyncFlowIndexEntry;
class FlowStatsCollector;

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

    MacAddress smac;
    MacAddress dmac;
    std::string source_vn;
    std::string dest_vn;
    SecurityGroupList source_sg_id_l;
    SecurityGroupList dest_sg_id_l;
    uint32_t flow_source_vrf;
    uint32_t flow_dest_vrf;

    MatchPolicy match_p;
    VnEntryConstRef vn_entry;
    InterfaceConstRef intf_entry;
    VmEntryConstRef in_vm_entry;
    VmEntryConstRef out_vm_entry;
    NextHopConstRef nh;
    uint32_t vrf;
    uint32_t mirror_vrf;
    uint32_t dest_vrf;
    uint32_t component_nh_idx;

    // Stats
    uint8_t source_plen;
    uint8_t dest_plen;
    uint16_t drop_reason;
    bool vrf_assign_evaluated;
    bool pending_recompute;
    uint32_t            if_index_info;
    TunnelInfo          tunnel_info;
    // map for references to the routes which were ignored due to more specific
    // route this will be used to trigger flow re-compute to use more specific
    // on route add. key for the map is vrf and data is prefix length
    FlowRouteRefMap     flow_source_plen_map;
    FlowRouteRefMap     flow_dest_plen_map;
    bool enable_rpf;
    uint8_t l2_rpf_plen;
    std::string vm_cfg_name;
    // IMPORTANT: Keep this structure assignable. Assignment operator is used in
    // FlowEntry::Copy() on this structure
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
        NON_IP_FLOW,    /* Flow due to bridging */
    };

    static const uint32_t kInvalidFlowHandle=0xFFFFFFFF;
    static const uint8_t kMaxMirrorsPerFlow=0x2;
    static const std::map<FlowPolicyState, const char*> FlowPolicyStateStr;
    static const std::map<uint16_t, const char*> FlowDropReasonStr;

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
        UnknownUnicastFlood = 1 << 11
    };

    FlowEntry(FlowTable *flow_table);
    virtual ~FlowEntry();

    void Reset(const FlowKey &k);
    void Reset();

    // Copy data fields from rhs
    void Copy(const FlowEntry *rhs);

    void InitFwdFlow(const PktFlowInfo *info, const PktInfo *pkt,
                     const PktControlInfo *ctrl,
                     const PktControlInfo *rev_ctrl, FlowEntry *rflow,
                     Agent *agent);
    void InitRevFlow(const PktFlowInfo *info, const PktInfo *pkt,
                     const PktControlInfo *ctrl,
                     const PktControlInfo *rev_ctrl, FlowEntry *rflow,
                     Agent *agent);
    void InitAuditFlow(uint32_t flow_idx);
    static void Init();

    static AgentRoute *GetL2Route(const VrfEntry *entry, const MacAddress &mac);
    static AgentRoute *GetUcRoute(const VrfEntry *entry, const IpAddress &addr);
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
    uint32_t flow_handle() const { return flow_handle_; }
    void set_flow_handle(uint32_t flow_handle);
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
    int linklocal_src_port() const { return linklocal_src_port_; }
    int linklocal_src_port_fd() const { return linklocal_src_port_fd_; }
    const std::string& acl_assigned_vrf() const;
    uint32_t acl_assigned_vrf_index() const;
    uint32_t fip() const { return fip_; }
    VmInterfaceKey fip_vmi() const { return fip_vmi_; }
    uint32_t reverse_flow_fip() const;
    VmInterfaceKey reverse_flow_vmi() const;
    void UpdateFipStatsInfo(uint32_t fip, uint32_t id, Agent *agent);
    const std::string &sg_rule_uuid() const { return sg_rule_uuid_; }
    const std::string &nw_ace_uuid() const { return nw_ace_uuid_; }
    const std::string &peer_vrouter() const { return peer_vrouter_; }
    TunnelType tunnel_type() const { return tunnel_type_; }

    uint16_t short_flow_reason() const { return short_flow_reason_; }
    bool set_pending_recompute(bool value);
    const MacAddress &smac() const { return data_.smac; }
    const MacAddress &dmac() const { return data_.dmac; }
    bool on_tree() const { return on_tree_; }
    void set_on_tree() { on_tree_ = true; }
    tbb::mutex &mutex() { return mutex_; }

    const Interface *intf_entry() const { return data_.intf_entry.get(); }
    const VnEntry *vn_entry() const { return data_.vn_entry.get(); }
    const VmEntry *in_vm_entry() const { return data_.in_vm_entry.get(); }
    const VmEntry *out_vm_entry() const { return data_.out_vm_entry.get(); }
    const NextHop *nh() const { return data_.nh.get(); }
    const MatchPolicy &match_p() const { return data_.match_p; }

    bool ImplicitDenyFlow() const { 
        return ((data_.match_p.action_info.action & 
                 (1 << TrafficAction::IMPLICIT_DENY)) ? true : false);
    }
    bool deleted() { return deleted_; }

    bool IsShortFlow() { return (flags_ & (1 << ShortFlow)); }
    // Flow action routines
    void ResyncFlow();
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

    void FillFlowInfo(FlowInfo &info);
    void GetPolicyInfo(const VnEntry *vn, const FlowEntry *rflow);
    void GetPolicyInfo(const FlowEntry *rflow);
    void GetPolicyInfo(const VnEntry *vn);
    void GetPolicyInfo();
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
    KSyncFlowIndexEntry *ksync_index_entry() { return ksync_index_entry_.get();}
    FlowStatsCollector* fsc() const {
        return fsc_;
    }

    void set_fsc(FlowStatsCollector *fsc) {
        fsc_ = fsc;
    }
    static std::string DropReasonStr(uint16_t reason);
private:
    friend class FlowTable;
    friend class FlowEntryFreeList;
    friend class FlowStatsCollector;
    friend void intrusive_ptr_add_ref(FlowEntry *fe);
    friend void intrusive_ptr_release(FlowEntry *fe);
    bool SetRpfNH(FlowTable *ft, const AgentRoute *rt);
    bool InitFlowCmn(const PktFlowInfo *info, const PktControlInfo *ctrl,
                     const PktControlInfo *rev_ctrl, FlowEntry *rflow);
    void GetSourceRouteInfo(const AgentRoute *rt);
    void GetDestRouteInfo(const AgentRoute *rt);
    void UpdateRpf();
    VmInterfaceKey InterfaceIdToKey(Agent *agent, uint32_t id);
    const std::string InterfaceIdToVmCfgName(Agent *agent, uint32_t id);

    FlowKey key_;
    FlowTable *flow_table_;
    FlowData data_;
    bool l3_flow_;
    uint32_t flow_handle_;
    FlowEntryPtr reverse_flow_entry_;
    static tbb::atomic<int> alloc_count_;
    bool deleted_;
    uint32_t flags_;
    uint16_t short_flow_reason_;
    // linklocal port - used as nat src port, agent locally binds to this port
    uint16_t linklocal_src_port_;
    // fd of the socket used to locally bind in case of linklocal
    int linklocal_src_port_fd_;
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
    // KSync state for the flow
    std::auto_ptr<KSyncFlowIndexEntry> ksync_index_entry_;
    // atomic refcount
    tbb::atomic<int> refcount_;
    tbb::mutex mutex_;
    boost::intrusive::list_member_hook<> free_list_node_;
    FlowStatsCollector *fsc_;
    // IMPORTANT: Remember to update Copy() routine if new fields are added

    static InetUnicastRouteEntry inet4_route_key_;
    static InetUnicastRouteEntry inet6_route_key_;
    static SecurityGroupList default_sg_list_;
};
 
void intrusive_ptr_add_ref(FlowEntry *fe);
void intrusive_ptr_release(FlowEntry *fe);

#endif //  __AGENT_PKT_FLOW_ENTRY_H__
