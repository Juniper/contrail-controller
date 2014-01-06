/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_FLOW_TABLE_H__
#define __AGENT_FLOW_TABLE_H__

#include <map>
#include <boost/uuid/random_generator.hpp>
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
#include <oper/agent_route.h>

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
    RouteFlowKey() : vrf(0), plen(0) { ip.ipv4 = 0;};
    RouteFlowKey(uint32_t v, uint32_t ipv4, uint8_t prefix) : 
        vrf(v), plen(prefix) { 
        ip.ipv4 = GetPrefix(ipv4, prefix);
    };
    ~RouteFlowKey() {};
    static int32_t GetPrefix(uint32_t ip, uint8_t plen) {
        //Mask prefix
        uint8_t host = 32;
        uint32_t mask = (0xFFFFFFFF << (host - plen));
        return (ip & mask);
    };

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
        vrf(0), src_port(0), dst_port(0), protocol(0) {
        src.ipv4 = 0;
        dst.ipv4 = 0;
    };

    FlowKey(uint32_t vrf_p, uint32_t sip_p, uint32_t dip_p, uint8_t proto_p,
            uint16_t sport_p, uint16_t dport_p) 
        : vrf(vrf_p), src_port(sport_p), dst_port(dport_p), protocol(proto_p) {
        src.ipv4 = sip_p;
        dst.ipv4 = dip_p;
    }

    FlowKey(const FlowKey &key) : 
        vrf(key.vrf), src_port(key.src_port), dst_port(key.dst_port),
        protocol(key.protocol) {
        src.ipv4 = key.src.ipv4;
        dst.ipv4 = key.dst.ipv4;
    };

    uint32_t vrf;
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
        return (key.vrf == vrf &&
                key.src.ipv4 == src.ipv4 &&
                key.dst.ipv4 == dst.ipv4 &&
                key.src_port == src_port &&
                key.dst_port == dst_port &&
                key.protocol == protocol);
    }
    void Reset() {
        vrf = -1;
        src.ipv4 = -1;
        dst.ipv4 = -1;
        src_port = -1;
        dst_port = -1;
        protocol = -1;
    }
};

struct FlowKeyCmp {
    bool operator()(const FlowKey &lhs, const FlowKey &rhs) {

        if (lhs.vrf != rhs.vrf) {
            return lhs.vrf < rhs.vrf;
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
        bytes(0), packets(0), intf_in(0), exported(false) {};

    uint64_t setup_time;
    uint64_t teardown_time;
    uint64_t last_modified_time; //used for aging
    uint64_t bytes;
    uint64_t packets;
    uint32_t intf_in;
    bool exported;
};

struct FlowData {
    FlowData() : 
        source_vn(""), dest_vn(""), source_sg_id_l(), dest_sg_id_l(),
        flow_source_vrf(VrfEntry::kInvalidIndex),
        flow_dest_vrf(VrfEntry::kInvalidIndex), match_p(), vn_entry(NULL),
        intf_entry(NULL), vm_entry(NULL), mirror_vrf(VrfEntry::kInvalidIndex),
        dest_vrf(),
        component_nh_idx((uint32_t)CompositeNH::kInvalidComponentNHIdx),
        nh_state_(NULL), source_plen(0), dest_plen(0) {};

    std::string source_vn;
    std::string dest_vn;
    SecurityGroupList source_sg_id_l;
    SecurityGroupList dest_sg_id_l;
    uint32_t flow_source_vrf;
    uint32_t flow_dest_vrf;

    MatchPolicy match_p;
    VnEntryConstRef vn_entry;
    InterfaceConstRef intf_entry;
    VmEntryConstRef vm_entry;
    uint32_t mirror_vrf;

    uint16_t dest_vrf;

    uint32_t component_nh_idx;

    // Stats
    NhStatePtr nh_state_;
    uint8_t source_plen;
    uint8_t dest_plen;
};

class FlowEntry {
  public:
    static const uint32_t kInvalidFlowHandle=0xFFFFFFFF;
    static const uint8_t kMaxMirrorsPerFlow=0x2;
    // Don't go beyond PCAP_END, pcap type is one byte
    enum PcapType {
        PCAP_CAPTURE_HOST = 1,
        PCAP_FLAGS = 2,
        PCAP_SOURCE_VN = 3,
        PCAP_DEST_VN = 4,
        PCAP_TLV_END = 255
    };
    FlowEntry();
    FlowEntry(const FlowKey &k);
    virtual ~FlowEntry() {
        alloc_count_.fetch_and_decrement();
    };

    FlowData data;

    bool ActionRecompute(MatchPolicy *policy);
    void CompareAndModify(const MatchPolicy &m_policy, bool create);
    void UpdateKSync(FlowTableKSyncEntry *entry, bool create);
    int GetRefCount() { return refcount_; }
    void MakeShortFlow();
    FlowStats &stats() { return stats_;};
    const FlowStats &stats() const { return stats_;};
    const FlowKey &key() const { return key_;};
    const uuid &flow_uuid() const { return flow_uuid_; };
    const uuid &egress_uuid() const { return egress_uuid_; };
    uint32_t flow_handle() const { return flow_handle_; };
    void set_flow_handle(uint32_t flow_handle) { flow_handle_ = flow_handle; };
    FlowEntry * reverse_flow_entry() { return reverse_flow_entry_.get(); };
    const FlowEntry * reverse_flow_entry() const { return reverse_flow_entry_.get(); };
    void set_reverse_flow_entry(FlowEntry *reverse_flow_entry) {
        reverse_flow_entry_ = reverse_flow_entry;
    };
    bool nat_flow() const { return (flags_ & NatFlow); };
    void set_nat_flow(const bool &nat) {
        if (nat) {
            flags_ |= NatFlow;
        } else {
            flags_ &= ~NatFlow;
        }
    };
    bool local_flow() const { return (flags_ & LocalFlow); };
    void set_local_flow(const bool &local) {
        if (local) {
            flags_ |= LocalFlow;
        } else {
            flags_ &= ~LocalFlow;
        }
    };
    bool short_flow() const { return (flags_ & ShortFlow); };
    void set_short_flow(const bool &short_flow) {
        if (short_flow) {
            flags_ |= ShortFlow;
        } else {
            flags_ &= ~ShortFlow;
        }
    };
    bool linklocal_flow() const { return (flags_ & LinkLocalFlow); };
    void set_linklocal_flow(const bool &linklocal_flow) {
        if (linklocal_flow) {
            flags_ |= LinkLocalFlow;
        } else {
            flags_ &= ~LinkLocalFlow;
        }
    };
    bool reverse_flow() const { return (flags_ & ReverseFlow); };
    void set_reverse_flow(const bool &reverse_flow) {
        if (reverse_flow) {
            flags_ |= ReverseFlow;
        } else {
            flags_ &= ~ReverseFlow;
        }
    };
    bool ecmp() const { return (flags_ & EcmpFlow); };
    void set_ecmp(const bool &ecmp) {
        if (ecmp) {
            flags_ |= EcmpFlow;
        } else {
            flags_ &= ~EcmpFlow;
        }
    };
    bool ingress() const { return (flags_ & IngressDir); };
    void set_ingress(const bool &ingress) {
        if (ingress) {
            flags_ |= IngressDir;
        } else {
            flags_ &= ~IngressDir;
        }
    };
    bool trap() const { return (flags_ & Trap); };
    void set_trap(const bool &trap) {
        if (trap) {
            flags_ |= Trap;
        } else {
            flags_ &= ~Trap;
        }
    };
    bool ImplicitDenyFlow() const { 
        return ((data.match_p.action_info.action & 
                 (1 << TrafficAction::IMPLICIT_DENY)) ? true : false);
    }
    void FillFlowInfo(FlowInfo &info);
    void GetPort(uint8_t &proto, uint16_t &sport, uint16_t &dport) const {
        proto = key_.protocol;
        sport = key_.src_port;
        dport = key_.dst_port;
    }
    void GetPolicyInfo(MatchPolicy *policy);

    void GetPolicy(const VnEntry *vn, MatchPolicy *policy);
    void GetSgList(const Interface *intf, MatchPolicy *policy);
    bool DoPolicy(const PacketHeader &hdr, MatchPolicy *policy, bool ingress);
    uint32_t MatchAcl(const PacketHeader &hdr, MatchPolicy *policy,
                      std::list<MatchAclParams> &acl, bool add_implicit_deny);
    void set_deleted(bool deleted) { deleted_ = deleted; }
    bool deleted() { return deleted_; }
    bool FlowSrcMatch(const RouteFlowKey &rkey) const;
    bool FlowDestMatch(const RouteFlowKey &rkey) const;
    void SetAclAction(std::vector<AclAction> &acl_action_l) const;
    uint32_t IngressReflexiveAction() const;
    uint32_t EgressReflexiveAction() const;
    const Interface *intf_entry() const { return data.intf_entry.get();};
    const VnEntry *vn_entry() const { return data.vn_entry.get();};
    const VmEntry *vm_entry() const { return data.vm_entry.get();};
private:
    friend class FlowTable;
    friend void intrusive_ptr_add_ref(FlowEntry *fe);
    friend void intrusive_ptr_release(FlowEntry *fe);
    enum FlowEntryFlags {
        NatFlow         = 1 << 0,
        LocalFlow       = 1 << 1,
        ShortFlow       = 1 << 2,
        LinkLocalFlow   = 1 << 3,
        ReverseFlow     = 1 << 4,
        EcmpFlow        = 1 << 5,
        IngressDir      = 1 << 6,
        Trap            = 1 << 7
    };
    FlowKey key_;
    FlowStats stats_;
    uuid flow_uuid_;
    //egress_uuid is used only during flow-export and applicable only for local-flows
    uuid egress_uuid_;
    uint32_t flow_handle_;
    FlowEntryPtr reverse_flow_entry_;
    static tbb::atomic<int> alloc_count_;
    bool deleted_;
    uint32_t flags_;
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
           acl_(acl), macl_(macl), mcacl_(mcacl) { };
        virtual ~VnFlowHandlerState() { };
    };
    struct VmIntfFlowHandlerState : public DBState {
        VmIntfFlowHandlerState(const VnEntry *vn) : vn_(vn) { };
        virtual ~VmIntfFlowHandlerState() { };

        VnEntryConstRef vn_;
        bool policy_;
        VmInterface::SecurityGroupEntryList sg_l_;
    };

    struct VrfFlowHandlerState : public DBState {
        VrfFlowHandlerState() {};
        virtual ~VrfFlowHandlerState() {};
        Inet4RouteUpdate *inet4_unicast_update_;
    };
    struct RouteFlowHandlerState : public DBState {
        RouteFlowHandlerState(SecurityGroupList &sg_l) : sg_l_(sg_l) { };
        virtual ~RouteFlowHandlerState() { };
        SecurityGroupList sg_l_;
    };

    FlowTable() : 
        flow_entry_map_(), acl_flow_tree_(), acl_listener_id_(), intf_listener_id_(),
        vn_listener_id_(), vm_listener_id_(), vrf_listener_id_(), 
        nh_listener_(NULL) {};
    virtual ~FlowTable();
    
    void Init();
    void Shutdown();

    FlowEntry *Allocate(const FlowKey &key);
    void Add(FlowEntry *flow, FlowEntry *rflow);
    FlowEntry *Find(const FlowKey &key);
    bool Delete(const FlowKey &key, bool del_reverse_flow);

    size_t Size() {return flow_entry_map_.size();};
    void VnFlowCounters(const VnEntry *vn, uint32_t *in_count, 
                        uint32_t *out_count);

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
    friend class FlowStatsCollector;
    friend class PktSandeshFlow;
    friend class FetchFlowRecord;
    friend class Inet4RouteUpdate;
    friend class NhState;
    friend void intrusive_ptr_release(FlowEntry *fe);
private:
    FlowEntryMap flow_entry_map_;

    AclFlowTree acl_flow_tree_;
    VnFlowTree vn_flow_tree_;
    IntfFlowTree intf_flow_tree_;
    VmFlowTree vm_flow_tree_;
    RouteFlowTree route_flow_tree_;

    DBTableBase::ListenerId acl_listener_id_;
    DBTableBase::ListenerId intf_listener_id_;
    DBTableBase::ListenerId vn_listener_id_;
    DBTableBase::ListenerId vm_listener_id_;
    DBTableBase::ListenerId vrf_listener_id_;
    NhListener *nh_listener_;

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
    void ResyncAFlow(FlowEntry *fe, MatchPolicy &policy, bool create);
    void ResyncVmPortFlows(const VmInterface *intf);
    void ResyncRpfNH(const RouteFlowKey &key, const Inet4UnicastRouteEntry *rt);
    void DeleteRouteFlows(const RouteFlowKey &key);

    void DeleteFlowInfo(FlowEntry *fe);
    void DeleteVnFlowInfo(FlowEntry *fe);
    void DeleteVmFlowInfo(FlowEntry *fe);
    void DeleteIntfFlowInfo(FlowEntry *fe);
    void DeleteRouteFlowInfo(FlowEntry *fe);
    void DeleteAclFlowInfo(const AclDBEntry *acl, FlowEntry* flow, AclEntryIDList &id_list);

    void DeleteVnFlows(const VnEntry *vn);
    void DeleteVmIntfFlows(const Interface *intf);
    void DeleteVmFlows(const VmEntry *vm);

    void AddFlowInfo(FlowEntry *fe);
    void AddAclFlowInfo(FlowEntry *fe);
    void UpdateAclFlow(const AclDBEntry *acl, FlowEntry* flow, AclEntryIDList &id_list);
    void AddIntfFlowInfo(FlowEntry *fe);
    void AddVnFlowInfo(FlowEntry *fe);
    void AddVmFlowInfo(FlowEntry *fe);
    void AddRouteFlowInfo(FlowEntry *fe);

    void DeleteAclFlows(const AclDBEntry *acl);
    void DeleteInternal(FlowEntryMap::iterator &it);
    bool Delete(FlowEntryMap::iterator &it, bool rev_flow);

    void UpdateReverseFlow(FlowEntry *flow, FlowEntry *rflow);

    DISALLOW_COPY_AND_ASSIGN(FlowTable);
};

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
    NhState(NextHop *nh):refcount_(), nh_(nh){ };
    ~NhState() {};
    const NextHop* nh() const { return nh_; }
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
        id_ = Agent::GetInstance()->GetNextHopTable()->
              Register(boost::bind(&NhListener::Notify, this, _1, _2));
    }
    ~NhListener() {
        Agent::GetInstance()->GetNextHopTable()->Unregister(id_);
    }
    void Notify(DBTablePartBase *part, DBEntryBase *e);
    DBTableBase::ListenerId id() {
        return id_;
    }
private:
    DBTableBase::ListenerId id_;
};

struct AclFlowInfo {
    AclFlowInfo() : flow_count(0), flow_miss(0) { };
    ~AclFlowInfo() { };
    FlowTable::FlowEntryTree fet;
    FlowTable::AceIdFlowCntMap aceid_cnt_map;
    void AddAclEntryIDFlowCnt(AclEntryIDList &idlist);
    void RemoveAclEntryIDFlowCnt(AclEntryIDList &idlist);
    int32_t flow_count;
    int32_t flow_miss;
    AclDBEntryConstRef acl_entry;
};

struct VnFlowInfo {
    VnFlowInfo() : ingress_flow_count(0), egress_flow_count(0) {};
    ~VnFlowInfo() {};

    VnEntryConstRef vn_entry;
    FlowTable::FlowEntryTree fet;
    uint32_t ingress_flow_count;
    uint32_t egress_flow_count;
};

struct IntfFlowInfo {
    IntfFlowInfo() {};
    ~IntfFlowInfo() {};

    InterfaceConstRef intf_entry;
    FlowTable::FlowEntryTree fet;
};

struct VmFlowInfo {
    VmFlowInfo() {};
    ~VmFlowInfo() {};

    VmEntryConstRef vm_entry;
    FlowTable::FlowEntryTree fet;
};

struct RouteFlowInfo {
    RouteFlowInfo() {};
    ~RouteFlowInfo() {};
    FlowTable::FlowEntryTree fet;
};

extern SandeshTraceBufferPtr FlowTraceBuf;
extern void SetActionStr(const FlowAction &, std::vector<ActionStr> &);

#define FLOW_TRACE(obj, ...)\
do {\
    Flow##obj::TraceMsg(FlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\

#endif
