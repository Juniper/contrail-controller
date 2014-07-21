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
struct RouteFlowKey;
struct RouteFlowInfo;
struct RouteFlowKeyCmp;
class Inet4RouteUpdate;
class FlowEntry;
class FlowTable;
class FlowTableKSyncEntry;
class NhListener;
class NhState;
typedef boost::intrusive_ptr<FlowEntry> FlowEntryPtr;
typedef boost::intrusive_ptr<const NhState> NhStatePtr;

struct RouteFlowKey {
    RouteFlowKey() : vrf(0), plen(0) { ip.ipv4 = 0; }
    RouteFlowKey(uint32_t v, uint32_t ipv4, uint8_t prefix) : 
        vrf(v), plen(prefix) { 
        ip.ipv4 = GetPrefix(ipv4, prefix);
    }
    ~RouteFlowKey() {}
    static int32_t GetPrefix(uint32_t ip, uint8_t plen) {
        //Mask prefix
        uint8_t host = 32;
        uint32_t mask = (0xFFFFFFFF << (host - plen));
        return (ip & mask);
    }

    uint32_t vrf;
    union {
        uint32_t ipv4;
    } ip;
    uint8_t plen;
};

struct RouteFlowKeyCmp {
    bool operator()(const RouteFlowKey &lhs, const RouteFlowKey &rhs) {
        if (lhs.vrf != rhs.vrf) {
            return lhs.vrf < rhs.vrf;
        }
        if (lhs.ip.ipv4 != rhs.ip.ipv4) {
            return lhs.ip.ipv4 < rhs.ip.ipv4;
        }
        return lhs.plen < rhs.plen;
    }
};

struct FlowKey {
    FlowKey() :
        nh(0), src_port(0), dst_port(0), protocol(0) {
        src.ipv4 = 0;
        dst.ipv4 = 0;
    }

    FlowKey(uint32_t nh_p, uint32_t sip_p, uint32_t dip_p, uint8_t proto_p,
            uint16_t sport_p, uint16_t dport_p) 
        : nh(nh_p), src_port(sport_p), dst_port(dport_p), protocol(proto_p) {
        src.ipv4 = sip_p;
        dst.ipv4 = dip_p;
    }

    FlowKey(const FlowKey &key) : 
        nh(key.nh), src_port(key.src_port), dst_port(key.dst_port),
        protocol(key.protocol) {
        src.ipv4 = key.src.ipv4;
        dst.ipv4 = key.dst.ipv4;
    }

    uint32_t nh;
    union {
        uint32_t ipv4;
    } src;
    union {
        uint32_t ipv4;
    } dst;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol;
    bool CompareKey(const FlowKey &key) {
        return (key.nh == nh &&
                key.src.ipv4 == src.ipv4 &&
                key.dst.ipv4 == dst.ipv4 &&
                key.src_port == src_port &&
                key.dst_port == dst_port &&
                key.protocol == protocol);
    }
    void Reset() {
        nh = -1;
        src.ipv4 = -1;
        dst.ipv4 = -1;
        src_port = -1;
        dst_port = -1;
        protocol = -1;
    }
};

struct FlowKeyCmp {
    bool operator()(const FlowKey &lhs, const FlowKey &rhs) {

        if (lhs.nh != rhs.nh) {
            return lhs.nh < rhs.nh;
        }

        if (lhs.src.ipv4 != rhs.src.ipv4) {
            return lhs.src.ipv4 < rhs.src.ipv4;
        }

        if (lhs.dst.ipv4 != rhs.dst.ipv4) {
            return lhs.dst.ipv4 < rhs.dst.ipv4;
        }

        if (lhs.protocol != rhs.protocol) {
            return lhs.protocol < rhs.protocol;
        }

        if (lhs.src_port != rhs.src_port) {
            return lhs.src_port < rhs.src_port;
        }
        return lhs.dst_port < rhs.dst_port;
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
        source_vn(""), dest_vn(""), source_sg_id_l(), dest_sg_id_l(),
        flow_source_vrf(VrfEntry::kInvalidIndex),
        flow_dest_vrf(VrfEntry::kInvalidIndex), match_p(), vn_entry(NULL),
        intf_entry(NULL), in_vm_entry(NULL), out_vm_entry(NULL),
        vrf(VrfEntry::kInvalidIndex),
        mirror_vrf(VrfEntry::kInvalidIndex), dest_vrf(),
        component_nh_idx((uint32_t)CompositeNH::kInvalidComponentNHIdx),
        nh_state_(NULL), source_plen(0), dest_plen(0),
        vrf_assign_evaluated(false) {}

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
    bool vrf_assign_evaluated;
};

class FlowEntry {
    public:
    enum FlowPolicyState {
        NOT_EVALUATED,
        IMPLICIT_ALLOW, /* Due to No Acl rules */
        IMPLICIT_DENY,
        DEFAULT_GW_ICMP_OR_DNS, /* DNS/ICMP pkt to/from default gateway */
        LINKLOCAL_FLOW, /* No policy applied for linklocal flow */
        MULTICAST_FLOW /* No policy applied for multicast flow */
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
    void UpdateKSync();
    int GetRefCount() { return refcount_; }
    void MakeShortFlow();
    const FlowStats &stats() const { return stats_;}
    const FlowKey &key() const { return key_;}
    FlowData &data() { return data_;}
    const FlowData &data() const { return data_;}
    const uuid &flow_uuid() const { return flow_uuid_; }
    const uuid &egress_uuid() const { return egress_uuid_; }
    uint32_t flow_handle() const { return flow_handle_; }
    void set_flow_handle(uint32_t flow_handle) { flow_handle_ = flow_handle; }
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
    bool FlowSrcMatch(const RouteFlowKey &rkey) const;
    bool FlowDestMatch(const RouteFlowKey &rkey) const;
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
            const PktControlInfo *ctrl, const PktControlInfo *rev_ctrl);
    void InitRevFlow(const PktFlowInfo *info,
            const PktControlInfo *ctrl, const PktControlInfo *rev_ctrl);
    void InitAuditFlow(uint32_t flow_idx);
    void set_source_sg_id_l(SecurityGroupList &sg_l) { data_.source_sg_id_l = sg_l; }
    void set_dest_sg_id_l(SecurityGroupList &sg_l) { data_.dest_sg_id_l = sg_l; }
    int linklocal_src_port() const { return linklocal_src_port_; }
    int linklocal_src_port_fd() const { return linklocal_src_port_fd_; }
    const std::string& acl_assigned_vrf() const;
    uint32_t acl_assigned_vrf_index() const;
    uint32_t reverse_flow_fip() const;
    uint32_t reverse_flow_vmport_id() const;
    void UpdateFipStatsInfo(uint32_t fip, uint32_t id);
    const std::string &sg_rule_uuid() const { return sg_rule_uuid_; }
    const std::string &nw_ace_uuid() const { return nw_ace_uuid_; }
private:
    friend class FlowTable;
    friend class FlowStatsCollector;
    friend void intrusive_ptr_add_ref(FlowEntry *fe);
    friend void intrusive_ptr_release(FlowEntry *fe);
    bool SetRpfNH(const Inet4UnicastRouteEntry *rt);
    bool InitFlowCmn(const PktFlowInfo *info, const PktControlInfo *ctrl,
                     const PktControlInfo *rev_ctrl);

    FlowKey key_;
    FlowData data_;
    FlowStats stats_;
    uuid flow_uuid_;
    //egress_uuid is used only during flow-export and applicable only for local-flows
    uuid egress_uuid_;
    uint32_t flow_handle_;
    FlowEntryPtr reverse_flow_entry_;
    FlowTableKSyncEntry *ksync_entry_;
    static tbb::atomic<int> alloc_count_;
    bool deleted_;
    uint32_t flags_;
    // linklocal port - used as nat src port, agent locally binds to this port
    uint16_t linklocal_src_port_;
    // fd of the socket used to locally bind in case of linklocal
    int linklocal_src_port_fd_;
    std::string sg_rule_uuid_;
    std::string nw_ace_uuid_;
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

class FlowTable {
public:
    static const int MaxResponses = 100;
    typedef std::map<FlowKey, FlowEntry *, FlowKeyCmp> FlowEntryMap;

    typedef std::map<int, int> AceIdFlowCntMap;
    typedef std::set<FlowEntryPtr, FlowEntryCmp> FlowEntryTree;
    typedef std::map<const AclDBEntry *, AclFlowInfo *> AclFlowTree;
    typedef std::pair<const AclDBEntry *, AclFlowInfo *> AclFlowPair;

    typedef std::map<const VnEntry *, VnFlowInfo *> VnFlowTree;
    typedef std::pair<const VnEntry *, VnFlowInfo *> VnFlowPair;

    typedef std::map<const Interface *, IntfFlowInfo *> IntfFlowTree;
    typedef std::pair<const Interface *, IntfFlowInfo *> IntfFlowPair;
    static boost::uuids::random_generator rand_gen_;

    typedef std::map<const VmEntry *, VmFlowInfo *> VmFlowTree;
    typedef std::pair<const VmEntry *, VmFlowInfo *> VmFlowPair;

    typedef std::map<RouteFlowKey, RouteFlowInfo *, RouteFlowKeyCmp> RouteFlowTree;
    typedef std::pair<RouteFlowKey, RouteFlowInfo *> RouteFlowPair;

    struct VnFlowHandlerState : public DBState {
        AclDBEntryConstRef acl_;
        AclDBEntryConstRef macl_;
        AclDBEntryConstRef mcacl_;
        VnFlowHandlerState(const AclDBEntry *acl, 
                           const AclDBEntry *macl,
                           const AclDBEntry *mcacl) :
           acl_(acl), macl_(macl), mcacl_(mcacl) { }
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
        Inet4RouteUpdate *inet4_unicast_update_;
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
    Inet4UnicastRouteEntry * GetUcRoute(const VrfEntry *entry, const Ip4Address &addr);

    friend class FlowStatsCollector;
    friend class PktSandeshFlow;
    friend class FetchFlowRecord;
    friend class Inet4RouteUpdate;
    friend class NhState;
    friend void intrusive_ptr_release(FlowEntry *fe);
private:
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

    Inet4UnicastRouteEntry route_key_;

    void AclNotify(DBTablePartBase *part, DBEntryBase *e);
    void IntfNotify(DBTablePartBase *part, DBEntryBase *e);
    void VnNotify(DBTablePartBase *part, DBEntryBase *e);
    void VrfNotify(DBTablePartBase *part, DBEntryBase *e);
    std::string GetAceSandeshDataKey(const AclDBEntry *acl, int ace_id);
    std::string GetAclFlowSandeshDataKey(const AclDBEntry *acl, const int last_count);

    void IncrVnFlowCounter(VnFlowInfo *vn_flow_info, const FlowEntry *fe);
    void DecrVnFlowCounter(VnFlowInfo *vn_flow_info, const FlowEntry *fe);
    void ResyncVnFlows(const VnEntry *vn);
    void ResyncRouteFlows(RouteFlowKey &key, SecurityGroupList &sg_l);
    void ResyncAFlow(FlowEntry *fe);
    void ResyncVmPortFlows(const VmInterface *intf);
    void ResyncRpfNH(const RouteFlowKey &key, const Inet4UnicastRouteEntry *rt);
    void DeleteRouteFlows(const RouteFlowKey &key);

    void DeleteFlowInfo(FlowEntry *fe);
    void DeleteVnFlowInfo(FlowEntry *fe);
    void DeleteVmFlowInfo(FlowEntry *fe);
    void DeleteVmFlowInfo(FlowEntry *fe, const VmEntry *vm);
    void DeleteIntfFlowInfo(FlowEntry *fe);
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
    void AddRouteFlowInfo(FlowEntry *fe);

    void DeleteAclFlows(const AclDBEntry *acl);
    void DeleteInternal(FlowEntryMap::iterator &it);
    bool Delete(FlowEntryMap::iterator &it, bool rev_flow);

    void UpdateReverseFlow(FlowEntry *flow, FlowEntry *rflow);

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

class Inet4RouteUpdate {
public:
    struct State : DBState {
        SecurityGroupList sg_l_;
        const NextHop* active_nh_;
        const NextHop* local_nh_;
    };

    Inet4RouteUpdate(Inet4UnicastAgentRouteTable *rt_table);
    Inet4RouteUpdate();
    ~Inet4RouteUpdate();
    void ManagedDelete();
    static Inet4RouteUpdate *UnicastInit(Inet4UnicastAgentRouteTable *table);
    void Unregister();
    bool DeleteState(DBTablePartBase *partition, DBEntryBase *entry);
    static void WalkDone(DBTableBase *partition, Inet4RouteUpdate *rt);
private:
    DBTableBase::ListenerId id_;
    Inet4UnicastAgentRouteTable *rt_table_;
    bool marked_delete_;
    void UnicastNotify(DBTablePartBase *partition, DBEntryBase *e);
    LifetimeRef<Inet4RouteUpdate> table_delete_ref_;
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
    FlowTable::FlowEntryTree fet;
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
    FlowTable::FlowEntryTree fet;
    uint32_t ingress_flow_count;
    uint32_t egress_flow_count;
};

struct IntfFlowInfo {
    IntfFlowInfo() {}
    ~IntfFlowInfo() {}

    InterfaceConstRef intf_entry;
    FlowTable::FlowEntryTree fet;
};

struct VmFlowInfo {
    VmFlowInfo() : linklocal_flow_count() {}
    ~VmFlowInfo() {}

    VmEntryConstRef vm_entry;
    FlowTable::FlowEntryTree fet;
    uint32_t linklocal_flow_count;
};

struct RouteFlowInfo {
    RouteFlowInfo() {}
    ~RouteFlowInfo() {}
    FlowTable::FlowEntryTree fet;
};

extern SandeshTraceBufferPtr FlowTraceBuf;
extern void SetActionStr(const FlowAction &, std::vector<ActionStr> &);
extern void GetFlowSandeshActionParams(const FlowAction &, std::string &);

#define FLOW_TRACE(obj, ...)\
do {\
    Flow##obj::TraceMsg(FlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\

#endif
