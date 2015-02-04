/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_FLOW_TABLE_H__
#define __AGENT_FLOW_TABLE_H__

#include <map>
#if defined(__GNUC__)
#include "base/compiler.h"
#if __GNUC_PREREQ(4, 5)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
#endif
#endif
#include <boost/uuid/random_generator.hpp>
#if defined(__GNUC__) && __GNUC_PREREQ(4, 6)
#pragma GCC diagnostic pop
#endif

#include <boost/uuid/uuid_io.hpp>
#include <boost/intrusive_ptr.hpp>
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
#include <sandesh/common/flow_types.h>

class FlowStatsCollector;
class PktSandeshFlow;
class FetchFlowRecord;
struct AclFlowInfo;
struct VnFlowInfo;
struct IntfFlowInfo;
struct VmFlowInfo;
class RouteFlowUpdate;
class InetRouteFlowUpdate;
class BridgeEntryFlowUpdate;
class FlowEntry;
class FlowTable;
class FlowTableKSyncEntry;
class NhListener;
class NhState;
typedef boost::intrusive_ptr<FlowEntry> FlowEntryPtr;
typedef boost::intrusive_ptr<const NhState> NhStatePtr;

struct FlowTaskMsg : public InterTaskMsg {
    FlowTaskMsg(FlowEntry * fe) : InterTaskMsg(0), fe_ptr(fe) {}
    ~FlowTaskMsg() {}

    FlowEntryPtr fe_ptr;
};

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

struct Inet4FlowKeyCmp {
    bool operator()(const FlowKey &lhs, const FlowKey &rhs) {
        const FlowKey &lhs_base = static_cast<const FlowKey &>(lhs);
        return lhs_base.IsLess(rhs);
    }
};

struct FlowStats {
    FlowStats() : setup_time(0), teardown_time(0), last_modified_time(0),
        bytes(0), packets(0), intf_in(0), exported(false), fip(0),
        fip_vm_port_id(Interface::kInvalidIndex) {}

    uint64_t setup_time;
    uint64_t teardown_time;
    uint64_t last_modified_time; //used for aging
    uint64_t bytes;
    uint64_t packets;
    uint32_t intf_in;
    bool exported;
    // Following fields are required for FIP stats accounting
    uint32_t fip;
    uint32_t fip_vm_port_id;
};

typedef std::list<MatchAclParams> MatchAclParamsList;
struct MatchPolicy {
    MatchPolicy():
        m_acl_l(), policy_action(0), m_out_acl_l(), out_policy_action(0), 
        m_out_sg_acl_l(), out_sg_rule_present(false), out_sg_action(0), 
        m_sg_acl_l(), sg_rule_present(false), sg_action(0),
        m_reverse_sg_acl_l(), reverse_sg_rule_present(false),
        reverse_sg_action(0), m_reverse_out_sg_acl_l(),
        reverse_out_sg_rule_present(false), reverse_out_sg_action(0),
        m_mirror_acl_l(), mirror_action(0), m_out_mirror_acl_l(),
        out_mirror_action(0), m_vrf_assign_acl_l(), vrf_assign_acl_action(0),
        sg_action_summary(0), action_info() {
    }

    ~MatchPolicy() {}

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

struct FlowData {
    FlowData() :
        smac(), dmac(), source_vn(""), dest_vn(""), source_sg_id_l(),
        dest_sg_id_l(), flow_source_vrf(VrfEntry::kInvalidIndex),
        flow_dest_vrf(VrfEntry::kInvalidIndex), match_p(), vn_entry(NULL),
        intf_entry(NULL), in_vm_entry(NULL), out_vm_entry(NULL),
        vrf(VrfEntry::kInvalidIndex),
        mirror_vrf(VrfEntry::kInvalidIndex), dest_vrf(),
        component_nh_idx((uint32_t)CompositeNH::kInvalidComponentNHIdx),
        nh_state_(NULL), source_plen(0), dest_plen(0), drop_reason(0),
        vrf_assign_evaluated(false), pending_recompute(false), enable_rpf(true) {}

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
    uint32_t vrf;
    uint32_t mirror_vrf;

    uint16_t dest_vrf;

    uint32_t component_nh_idx;

    // Stats
    NhStatePtr nh_state_;
    uint8_t source_plen;
    uint8_t dest_plen;
    uint16_t drop_reason;
    bool vrf_assign_evaluated;
    bool pending_recompute;
    uint16_t            if_index_info;
    TunnelInfo          tunnel_info;
    // map for references to the routes which were ignored due to more specific
    // route this will be used to trigger flow re-compute to use more specific
    // on route add. key for the map is vrf and data is prefix length
    FlowRouteRefMap     flow_source_plen_map;
    FlowRouteRefMap     flow_dest_plen_map;
    bool enable_rpf;
};

class FlowEntry {
    public:
    enum FlowShortReason {
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
        SHORT_MAX
    };

    enum FlowDropReason {
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
        TcpAckFlow      = 1 << 10
    };
    FlowEntry(const FlowKey &k);
    virtual ~FlowEntry() {
        if (linklocal_src_port_fd_ != PktFlowInfo::kLinkLocalInvalidFd) {
            close(linklocal_src_port_fd_);
        }
        alloc_count_.fetch_and_decrement();
    };

    bool ActionRecompute();
    void UpdateKSync(FlowTable* table);
    int GetRefCount() { return refcount_; }
    void MakeShortFlow(FlowShortReason reason);
    const FlowStats &stats() const { return stats_;}
    const FlowKey &key() const { return key_;}
    FlowData &data() { return data_;}
    const FlowData &data() const { return data_;}
    const uuid &flow_uuid() const { return flow_uuid_; }
    const uuid &egress_uuid() const { return egress_uuid_; }
    bool l3_flow() const { return l3_flow_; }
    uint32_t flow_handle() const { return flow_handle_; }
    void set_flow_handle(uint32_t flow_handle, FlowTable* table);
    FlowEntry * reverse_flow_entry() { return reverse_flow_entry_.get(); }
    const FlowEntry * reverse_flow_entry() const { return reverse_flow_entry_.get(); }
    void set_reverse_flow_entry(FlowEntry *reverse_flow_entry) {
        reverse_flow_entry_ = reverse_flow_entry;
    }
    bool is_flags_set(const FlowEntryFlags &flags) const { return (flags_ & flags); }
    void set_flags(const FlowEntryFlags &flags) { flags_ |= flags; }
    void reset_flags(const FlowEntryFlags &flags) { flags_ &= ~flags; }

    bool ImplicitDenyFlow() const { 
        return ((data_.match_p.action_info.action & 
                 (1 << TrafficAction::IMPLICIT_DENY)) ? true : false);
    }
    void FillFlowInfo(FlowInfo &info);
    void GetPolicyInfo();
    void GetPolicyInfo(const VnEntry *vn);
    void SetMirrorVrf(const uint32_t id) {data_.mirror_vrf = id;}
    void SetMirrorVrfFromAction();
    void SetVrfAssignEntry();

    void GetPolicy(const VnEntry *vn);
    void GetNonLocalFlowSgList(const VmInterface *vm_port);
    void GetLocalFlowSgList(const VmInterface *vm_port,
                            const VmInterface *reverse_vm_port);
    void GetSgList(const Interface *intf);
    void SetPacketHeader(PacketHeader *hdr);
    void SetOutPacketHeader(PacketHeader *hdr);
    void ComputeReflexiveAction();
    bool DoPolicy();
    void GetVrfAssignAcl();
    uint32_t MatchAcl(const PacketHeader &hdr,
                      MatchAclParamsList &acl, bool add_implicit_deny,
                      bool add_implicit_allow, FlowPolicyInfo *info);
    void ResetPolicy();
    void ResetStats();
    void set_deleted(bool deleted) { deleted_ = deleted; }
    bool deleted() { return deleted_; }
    void SetAclAction(std::vector<AclAction> &acl_action_l) const;
    void UpdateReflexiveAction();
    const Interface *intf_entry() const { return data_.intf_entry.get();}
    const VnEntry *vn_entry() const { return data_.vn_entry.get();}
    const VmEntry *in_vm_entry() const { return data_.in_vm_entry.get();}
    const VmEntry *out_vm_entry() const { return data_.out_vm_entry.get();}
    const MatchPolicy &match_p() const { return data_.match_p; }
    void SetAclFlowSandeshData(const AclDBEntry *acl,
            FlowSandeshData &fe_sandesh_data) const;
    void InitFwdFlow(const PktFlowInfo *info, const PktInfo *pkt,
                     const PktControlInfo *ctrl,
                     const PktControlInfo *rev_ctrl);
    void InitRevFlow(const PktFlowInfo *info, const PktInfo *pkt,
                     const PktControlInfo *ctrl,
                     const PktControlInfo *rev_ctrl);
    void InitAuditFlow(uint32_t flow_idx);
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
    uint32_t reverse_flow_fip() const;
    uint32_t reverse_flow_vmport_id() const;
    void UpdateFipStatsInfo(uint32_t fip, uint32_t id);
    const std::string &sg_rule_uuid() const { return sg_rule_uuid_; }
    const std::string &nw_ace_uuid() const { return nw_ace_uuid_; }
    const std::string &peer_vrouter() const { return peer_vrouter_; }
    TunnelType tunnel_type() const { return tunnel_type_; }
    uint16_t underlay_source_port() const { return underlay_source_port_; }
    void set_underlay_source_port(uint16_t port) {
        underlay_source_port_ = port;
    }
    uint16_t short_flow_reason() const { return short_flow_reason_; }
    bool set_pending_recompute(bool value);
    const MacAddress &smac() const { return data_.smac; }
    const MacAddress &dmac() const { return data_.dmac; }
private:
    friend class FlowTable;
    friend class FlowStatsCollector;
    friend void intrusive_ptr_add_ref(FlowEntry *fe);
    friend void intrusive_ptr_release(FlowEntry *fe);
    bool SetRpfNH(const AgentRoute *rt);
    bool InitFlowCmn(const PktFlowInfo *info, const PktControlInfo *ctrl,
                     const PktControlInfo *rev_ctrl);
    void GetSourceRouteInfo(const AgentRoute *rt);
    void GetDestRouteInfo(const AgentRoute *rt);
    void UpdateRpf();

    FlowKey key_;
    FlowData data_;
    FlowStats stats_;
    uuid flow_uuid_;
    //egress_uuid is used only during flow-export and applicable only for local-flows
    uuid egress_uuid_;
    bool l3_flow_;
    uint32_t flow_handle_;
    FlowEntryPtr reverse_flow_entry_;
    FlowTableKSyncEntry *ksync_entry_;
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
    //Underlay source port. 0 for local flows. Used during flow-export
    uint16_t underlay_source_port_;
    // atomic refcount
    tbb::atomic<int> refcount_;
};
 
struct FlowEntryCmp {
    bool operator()(const FlowEntryPtr &l, const FlowEntryPtr &r) {
        FlowEntry *lhs = l.get();
        FlowEntry *rhs = r.get();

        return (lhs < rhs);
    }
};

typedef std::set<FlowEntryPtr, FlowEntryCmp> FlowEntryTree;

struct RouteFlowKey {
    RouteFlowKey() : vrf(-1), family(Address::INET), plen(0) {}

    RouteFlowKey(uint32_t v, const Ip4Address &ipv4, uint8_t p) :
        vrf(v), family(Address::INET), plen(p) {
        ip = Address::GetIp4SubnetAddress(ipv4, plen);
    }

    RouteFlowKey(uint32_t v, const Ip6Address &ipv6, uint8_t p) :
        vrf(v), family(Address::INET6), plen(p) {
        assert(0);
    }

    RouteFlowKey(uint32_t v, const IpAddress &ip_p, uint8_t p) :
        vrf(v), plen(p) {
        if (ip_p.is_v4()) {
            family = Address::INET;
            ip = Address::GetIp4SubnetAddress(ip_p.to_v4(), plen);
        } else if (ip_p.is_v6()) {
            family = Address::INET6;
            ip = Address::GetIp6SubnetAddress(ip_p.to_v6(), plen);
        } else {
            assert(0);
        }
    }

    RouteFlowKey(uint32_t v, const MacAddress &addr) :
        vrf(v), family(Address::ENET), ip(), plen(48) {
    }
    virtual ~RouteFlowKey() {}

    bool FlowSrcMatch(const FlowEntry *key) const;
    bool FlowDestMatch(const FlowEntry *key) const;

    uint32_t vrf;
    Address::Family family; // address family
    IpAddress ip;
    MacAddress mac;
    uint8_t plen;
};

struct RouteFlowInfo {
    RouteFlowInfo(const RouteFlowKey &r_key) : key(r_key) {}
    RouteFlowInfo(uint32_t v, const IpAddress &ip_p, uint8_t p) :
        key(v, ip_p, p) {}
    ~RouteFlowInfo() {}

    class KeyCmp {
    public:
        static std::size_t BitLength(const RouteFlowInfo *route_info) {
            return (((sizeof(route_info->key.vrf) +
                      sizeof(route_info->key.family)) << 3)
                    + route_info->key.plen);
        }

        static char ByteValue(const RouteFlowInfo *route_info, std::size_t idx) {
            const char *ch;
            std::size_t i = idx;
            if (i < sizeof(route_info->key.vrf)) {
                ch = (const char *)&route_info->key.vrf;
                return ch[sizeof(route_info->key.vrf) - i - 1];
            }
            i -= sizeof(route_info->key.vrf);
            if (i < sizeof(route_info->key.family)) {
                ch = (const char *)&route_info->key.family;
                return ch[sizeof(route_info->key.family) - i - 1];
            }
            i -= sizeof(route_info->key.family);
            if (route_info->key.family == Address::INET) {
                return route_info->key.ip.to_v4().to_bytes()[i];
            } else if (route_info->key.family == Address::INET6) {
                return route_info->key.ip.to_v6().to_bytes()[i];
            } else if (route_info->key.family == Address::ENET) {
                return (route_info->key.mac.GetData())[i];
            } else {
                assert(0);
            }
        }
    };

    RouteFlowKey key;
    FlowEntryTree fet;
    Patricia::Node node;
};

class FlowTable {
public:
    static const int MaxResponses = 100;
    typedef std::map<FlowKey, FlowEntry *, Inet4FlowKeyCmp> FlowEntryMap;

    typedef std::map<int, int> AceIdFlowCntMap;
    typedef std::map<const AclDBEntry *, AclFlowInfo *> AclFlowTree;
    typedef std::pair<const AclDBEntry *, AclFlowInfo *> AclFlowPair;

    typedef std::map<const VnEntry *, VnFlowInfo *> VnFlowTree;
    typedef std::pair<const VnEntry *, VnFlowInfo *> VnFlowPair;

    typedef std::map<const Interface *, IntfFlowInfo *> IntfFlowTree;
    typedef std::pair<const Interface *, IntfFlowInfo *> IntfFlowPair;
    static boost::uuids::random_generator rand_gen_;

    typedef std::map<const VmEntry *, VmFlowInfo *> VmFlowTree;
    typedef std::pair<const VmEntry *, VmFlowInfo *> VmFlowPair;

    typedef Patricia::Tree<RouteFlowInfo, &RouteFlowInfo::node, RouteFlowInfo::KeyCmp> RouteFlowTree;
    typedef boost::function<bool(FlowEntry *flow)> FlowEntryCb;

    struct VnFlowHandlerState : public DBState {
        AclDBEntryConstRef acl_;
        AclDBEntryConstRef macl_;
        AclDBEntryConstRef mcacl_;
        bool enable_rpf_;
        VnFlowHandlerState(const AclDBEntry *acl, 
                           const AclDBEntry *macl,
                           const AclDBEntry *mcacl, bool enable_rpf) :
           acl_(acl), macl_(macl), mcacl_(mcacl), enable_rpf_(enable_rpf) { }
        virtual ~VnFlowHandlerState() { }
    };
    struct VmIntfFlowHandlerState : public DBState {
        VmIntfFlowHandlerState(const VnEntry *vn) : vn_(vn) { }
        virtual ~VmIntfFlowHandlerState() { }

        VnEntryConstRef vn_;
        bool policy_;
        VmInterface::SecurityGroupEntryList sg_l_;
    };

    struct VrfFlowHandlerState : public DBState {
        VrfFlowHandlerState() {}
        virtual ~VrfFlowHandlerState() {}

        // Register to all the route tables of intrest
        void Register(VrfEntry *vrf);
        // Unregister from the route tables
        void Unregister(VrfEntry *vrf);

        InetRouteFlowUpdate *inet4_unicast_update_;
        BridgeEntryFlowUpdate *bridge_update_;
    };
    struct RouteFlowHandlerState : public DBState {
        RouteFlowHandlerState(SecurityGroupList &sg_l) : sg_l_(sg_l) { }
        virtual ~RouteFlowHandlerState() { }
        SecurityGroupList sg_l_;
    };

    FlowTable(Agent *agent);
    virtual ~FlowTable();
    
    void Init();
    void InitDone();
    void Shutdown();

    FlowEntry *Allocate(const FlowKey &key);
    void Add(FlowEntry *flow, FlowEntry *rflow);
    FlowEntry *Find(const FlowKey &key);
    bool Delete(const FlowKey &key, bool del_reverse_flow);

    size_t Size() { return flow_entry_map_.size(); }
    void VnFlowCounters(const VnEntry *vn, uint32_t *in_count, 
                        uint32_t *out_count);
    uint32_t VmFlowCount(const VmEntry *vm);
    uint32_t VmLinkLocalFlowCount(const VmEntry *vm);
    uint32_t max_vm_flows() const { return max_vm_flows_; }
    void set_max_vm_flows(uint32_t num_flows) { max_vm_flows_ = num_flows; }
    uint32_t linklocal_flow_count() const { return linklocal_flow_count_; }
    Agent *agent() const { return agent_; }

    // Test code only used method
    RouteFlowInfo *RouteFlowInfoFind(RouteFlowKey &key);
    void DeleteFlow(const AclDBEntry *acl, const FlowKey &key, AclEntryIDList &id_list);
    void ResyncAclFlows(const AclDBEntry *acl);
    void DeleteAll();

    void SetAclFlowSandeshData(const AclDBEntry *acl, AclFlowResp &data, 
                               const int last_count);
    void SetAceSandeshData(const AclDBEntry *acl, AclFlowCountResp &data, 
                           int ace_id);
   
    FlowTable::FlowEntryMap::iterator begin() {
        return flow_entry_map_.begin();
    }

    FlowTable::FlowEntryMap::iterator end() {
        return flow_entry_map_.end(); 
    }

    DBTableBase::ListenerId nh_listener_id();
    AgentRoute *GetL2Route(const VrfEntry *entry, const MacAddress &mac,
                           const IpAddress &ip_addr);
    AgentRoute *GetUcRoute(const VrfEntry *entry, const IpAddress &addr);
    static const SecurityGroupList &default_sg_list() {return default_sg_list_;}
    bool ValidFlowMove(const FlowEntry *new_flow,
                       const FlowEntry *old_flow) const;
    void FlowExport(FlowEntry *flow, uint64_t diff_bytes, uint64_t diff_pkts);
    virtual void DispatchFlowMsg(SandeshLevel::type level, FlowDataIpv4 &flow);
    void IterateFlowInfoEntries(const RouteFlowKey &key, FlowEntryCb cb);
    RouteFlowInfo *FindRouteFlowInfo(RouteFlowInfo *key);
    void FlowRecompute(RouteFlowInfo *rt_info);

    // Update flow port bucket information
    void NewFlow(const FlowEntry *flow);
    void DeleteFlow(const FlowEntry *flow);
    friend class FlowStatsCollector;
    friend class PktSandeshFlow;
    friend class FetchFlowRecord;
    friend class RouteFlowUpdate;
    friend class InetRouteFlowUpdate;
    friend class BridgeEntryFlowUpdate;
    friend class NhState;
    friend class PktFlowInfo;
    friend void intrusive_ptr_release(FlowEntry *fe);
private:
    static SecurityGroupList default_sg_list_;

    Agent *agent_;
    FlowEntryMap flow_entry_map_;

    AclFlowTree acl_flow_tree_;
    VnFlowTree vn_flow_tree_;
    IntfFlowTree intf_flow_tree_;
    VmFlowTree vm_flow_tree_;
    RouteFlowTree route_flow_tree_;

    uint32_t max_vm_flows_;     // maximum flow count allowed per vm
    uint32_t linklocal_flow_count_;  // total linklocal flows in the agent

    DBTableBase::ListenerId acl_listener_id_;
    DBTableBase::ListenerId intf_listener_id_;
    DBTableBase::ListenerId vn_listener_id_;
    DBTableBase::ListenerId vm_listener_id_;
    DBTableBase::ListenerId vrf_listener_id_;
    NhListener *nh_listener_;

    InetUnicastRouteEntry inet4_route_key_;
    InetUnicastRouteEntry inet6_route_key_;

    void AclNotify(DBTablePartBase *part, DBEntryBase *e);
    void IntfNotify(DBTablePartBase *part, DBEntryBase *e);
    void VnNotify(DBTablePartBase *part, DBEntryBase *e);
    void VrfNotify(DBTablePartBase *part, DBEntryBase *e);
    std::string GetAceSandeshDataKey(const AclDBEntry *acl, int ace_id);
    std::string GetAclFlowSandeshDataKey(const AclDBEntry *acl, const int last_count);

    void IncrVnFlowCounter(VnFlowInfo *vn_flow_info, const FlowEntry *fe);
    void DecrVnFlowCounter(VnFlowInfo *vn_flow_info, const FlowEntry *fe);
    void ResyncVnFlows(const VnEntry *vn);
    void ResyncAFlow(FlowEntry *fe);
    void ResyncVmPortFlows(const VmInterface *intf);
    void ResyncRpfNH(const RouteFlowKey &key, const AgentRoute *rt);

    void DeleteFlowInfo(FlowEntry *fe);
    void DeleteVnFlowInfo(FlowEntry *fe);
    void DeleteVmFlowInfo(FlowEntry *fe);
    void DeleteVmFlowInfo(FlowEntry *fe, const VmEntry *vm);
    void DeleteIntfFlowInfo(FlowEntry *fe);
    void DeleteInetRouteFlowInfoInternal(FlowEntry *fe, RouteFlowKey &key);
    void DeleteInetRouteFlowInfo(FlowEntry *fe);
    void DeleteL2RouteFlowInfo(FlowEntry *fe);
    void DeleteRouteFlowInfo(FlowEntry *fe);
    void DeleteAclFlowInfo(const AclDBEntry *acl, FlowEntry* flow, const AclEntryIDList &id_list);

    void DeleteVnFlows(const VnEntry *vn);
    void DeleteVmIntfFlows(const Interface *intf);
    void DeleteVmFlows(const VmEntry *vm);

    void AddFlowInfo(FlowEntry *fe);
    void AddAclFlowInfo(FlowEntry *fe);
    void UpdateAclFlow(const AclDBEntry *acl, FlowEntry* flow, const AclEntryIDList &id_list);
    void AddIntfFlowInfo(FlowEntry *fe);
    void AddVnFlowInfo(FlowEntry *fe);
    void AddVmFlowInfo(FlowEntry *fe);
    void AddVmFlowInfo(FlowEntry *fe, const VmEntry *vm);
    void AddInetRouteFlowInfoInternal(FlowEntry *fe, RouteFlowKey &key);
    void AddInetRouteFlowInfo(FlowEntry *fe);
    void AddL2RouteFlowInfo(FlowEntry *fe);
    void AddRouteFlowInfo(FlowEntry *fe);

    void DeleteAclFlows(const AclDBEntry *acl);
    void DeleteInternal(FlowEntryMap::iterator &it);
    bool Delete(FlowEntryMap::iterator &it, bool rev_flow);

    void UpdateReverseFlow(FlowEntry *flow, FlowEntry *rflow);
    void SourceIpOverride(FlowEntry *flow, FlowDataIpv4 &s_flow);
    void SetUnderlayInfo(FlowEntry *flow, FlowDataIpv4 &s_flow);

    DISALLOW_COPY_AND_ASSIGN(FlowTable);
};

inline void intrusive_ptr_add_ref(FlowEntry *fe) {
    fe->refcount_.fetch_and_increment();
}
inline void intrusive_ptr_release(FlowEntry *fe) {
    int prev = fe->refcount_.fetch_and_decrement();
    if (prev == 1) {
        FlowTable *table = Agent::GetInstance()->pkt()->flow_table();
        FlowTable::FlowEntryMap::iterator it = table->flow_entry_map_.find(fe->key());
        assert(it != table->flow_entry_map_.end());
        table->flow_entry_map_.erase(it);
        delete fe;
    }
}

////////////////////////////////////////////////////////////////////////////
// RouteFlowUpdate class responsible to keep flow in-sync with route
// add/delete/change
//
// A RouteFlowUpdate entry is created for every Route table.
// RouteFlowUpdate registers to route notifications and do the following
//   - Tracks change of SG-List for a route entry
//   - Tracks change of NH for a route entry
// When a VRF entry is deleted, will start a DBTable walk and in the walk, 
// will delete DBState for all entries in the table. The RouteFlowUpdate entry
// itself is deleted at end of the walk
//
// Defines pure virtual methods for following
// - Handle add of a new route
// - Handle delte of a route
// - Handle change in SG-List
// - Handle change in NH for route
////////////////////////////////////////////////////////////////////////////
class RouteFlowUpdate {
public:
    // DBSTate to hold old values for SG-List and NH
    struct State : DBState {
        SecurityGroupList sg_l_;
        const NextHop* active_nh_;
        const NextHop* local_nh_;
    };

    RouteFlowUpdate(AgentRouteTable *table);
    virtual ~RouteFlowUpdate();

    void set_dblistener_id(DBTableBase::ListenerId id) { id_ = id; }
    DBTableBase::ListenerId dblistener_id() { return id_; }

    void set_walk_id(DBTableWalker::WalkId id) { walk_id_ = id; }
    DBTableWalker::WalkId walk_id() { return walk_id_; }
    AgentRouteTable *rt_table() const { return rt_table_; }

    void ManagedDelete();
    void Notify(DBTablePartBase *partition, DBEntryBase *e);

    static bool DeleteState(DBTablePartBase *partition, DBEntryBase *entry,
                            RouteFlowUpdate *info);
    static void WalkDone(DBTableBase *partition, RouteFlowUpdate *info);

    virtual void TraceMsg(AgentRoute *route, const AgentPath *path,
                          SecurityGroupList &sg_list) = 0;
    virtual void RouteDel(AgentRoute *entry) = 0;
    virtual void RouteAdd(AgentRoute *entry) = 0;
    virtual void SgChange(AgentRoute *entry, SecurityGroupList &sg_list)
        = 0;
    virtual void NhChange(AgentRoute *entry, const NextHop *active_nh,
                          const NextHop *local_nh) = 0;
protected:
    DBTableBase::ListenerId id_;
    AgentRouteTable *rt_table_;
    bool rt_table_deleted_;
    LifetimeRef<RouteFlowUpdate> table_delete_ref_;
    DBTableWalker::WalkId walk_id_;
private:
    DISALLOW_COPY_AND_ASSIGN(RouteFlowUpdate);
};

////////////////////////////////////////////////////////////////////////////
// RouteFlowUpdate implementation for InetUnicast route tables
//
// RouteDel : Triggers re-evaluation of the flows. Flows can potentially use
//            route with lower prefix-len
// RouteAdd : Triggers re-evaluation of the flows. Finds the covering route
//            (route with lower prefix). The new route can potentially change
//            the route for flows associated with covering route. The flow
//            re-evaluates all flows attached to lower prefix route.
// SgChange : When SG-List for a flow changes, it can potentially change
//            flow action. So, RESYNC's the flows
// NhChange : Change of NH can potentially change RPF check for flows.
//            Re-evaluates flows to re-compute RPF check.
////////////////////////////////////////////////////////////////////////////
class InetRouteFlowUpdate : public RouteFlowUpdate {
public:
    InetRouteFlowUpdate(AgentRouteTable *table) : RouteFlowUpdate(table) { }
    virtual ~InetRouteFlowUpdate() { }

    bool SgUpdate(FlowEntry *fe, FlowTable *table, RouteFlowKey &key,
                  const SecurityGroupList &sg_list);

    virtual void TraceMsg(AgentRoute *route, const AgentPath *path,
                          SecurityGroupList &sg_list);
    virtual void RouteDel(AgentRoute *entry);
    virtual void RouteAdd(AgentRoute *entry);
    virtual void SgChange(AgentRoute *entry, SecurityGroupList &sg_list);
    virtual void NhChange(AgentRoute *entry, const NextHop *active_nh,
                          const NextHop *local_nh);
private:
    DISALLOW_COPY_AND_ASSIGN(InetRouteFlowUpdate);
};

////////////////////////////////////////////////////////////////////////////
// BridgeEntryFlowUpdate implementation for bridge tables
//
// RouteDel : Deletes the flows. Unlike Inet routes, flow cannot match other
//            route to re-evaluate
// RouteAdd : No-op
// SgChange : When SG-List for a flow changes, it can potentially change
//            flow action. So, RESYNC's the flows
// NhChange : Change of NH can potentially change RPF check for flows.
//            Re-evaluates flows to re-compute RPF check.
////////////////////////////////////////////////////////////////////////////
class BridgeEntryFlowUpdate : public RouteFlowUpdate {
public:
    BridgeEntryFlowUpdate(AgentRouteTable *table) : RouteFlowUpdate(table) { }
    virtual ~BridgeEntryFlowUpdate() { }

    bool SgUpdate(FlowEntry *fe, FlowTable *table, RouteFlowKey &key,
                  const SecurityGroupList &sg_list);
    bool DelEntry(FlowEntry *fe, FlowTable *table, RouteFlowKey &key);

    virtual void TraceMsg(AgentRoute *route, const AgentPath *path,
                          SecurityGroupList &sg_list);
    virtual void RouteDel(AgentRoute *entry);
    virtual void RouteAdd(AgentRoute *entry);
    virtual void SgChange(AgentRoute *entry, SecurityGroupList &sg_list);
    virtual void NhChange(AgentRoute *entry, const NextHop *active_nh,
                          const NextHop *local_nh);
private:
    DISALLOW_COPY_AND_ASSIGN(BridgeEntryFlowUpdate);
};

class NhState : public DBState {
public:
    NhState(NextHop *nh):refcount_(), nh_(nh){ }
    ~NhState() {}
    NextHop* nh() const { return nh_; }
    uint32_t refcount() const { return refcount_; }
private:
    friend void intrusive_ptr_add_ref(const NhState *nh);
    friend void intrusive_ptr_release(const NhState *nh);
    mutable tbb::atomic<uint32_t> refcount_;
    NextHop *nh_;
};

inline void intrusive_ptr_add_ref(const NhState *nh_state) {
    nh_state->refcount_.fetch_and_increment();
}
inline void intrusive_ptr_release(const NhState *nh_state) {
    int prev = nh_state->refcount_.fetch_and_decrement();
    if (prev == 1 && nh_state->nh()->IsDeleted()) {
        AgentDBTable *table = 
            static_cast<AgentDBTable *>(nh_state->nh_->get_table());
        nh_state->nh_->ClearState(table, 
            Agent::GetInstance()->pkt()->flow_table()->nh_listener_id());
        delete nh_state; 
    }
}

class NhListener {
public:
    NhListener() {
        id_ = Agent::GetInstance()->nexthop_table()->
              Register(boost::bind(&NhListener::Notify, this, _1, _2));
    }
    ~NhListener() {
        Agent::GetInstance()->nexthop_table()->Unregister(id_);
    }
    void Notify(DBTablePartBase *part, DBEntryBase *e);
    DBTableBase::ListenerId id() {
        return id_;
    }
private:
    DBTableBase::ListenerId id_;
};

struct AclFlowInfo {
    AclFlowInfo() : flow_count(0), flow_miss(0) { }
    ~AclFlowInfo() { }
    FlowEntryTree fet;
    FlowTable::AceIdFlowCntMap aceid_cnt_map;
    void AddAclEntryIDFlowCnt(AclEntryIDList &idlist);
    void RemoveAclEntryIDFlowCnt(AclEntryIDList &idlist);
    int32_t flow_count;
    int32_t flow_miss;
    AclDBEntryConstRef acl_entry;
};

struct VnFlowInfo {
    VnFlowInfo() : ingress_flow_count(0), egress_flow_count(0) {}
    ~VnFlowInfo() {}

    VnEntryConstRef vn_entry;
    FlowEntryTree fet;
    uint32_t ingress_flow_count;
    uint32_t egress_flow_count;
};

struct IntfFlowInfo {
    IntfFlowInfo() {}
    ~IntfFlowInfo() {}

    InterfaceConstRef intf_entry;
    FlowEntryTree fet;
};

struct VmFlowInfo {
    VmFlowInfo() : linklocal_flow_count() {}
    ~VmFlowInfo() {}

    VmEntryConstRef vm_entry;
    FlowEntryTree fet;
    uint32_t linklocal_flow_count;
};

extern SandeshTraceBufferPtr FlowTraceBuf;
extern void SetActionStr(const FlowAction &, std::vector<ActionStr> &);
extern void GetFlowSandeshActionParams(const FlowAction &, std::string &);

#define FLOW_TRACE(obj, ...)\
do {\
    Flow##obj::TraceMsg(FlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\

#endif
