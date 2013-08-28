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
#include <sandesh/sandesh_trace.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface.h>
#include <oper/nexthop.h>
#include <oper/inet4_ucroute.h>

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
typedef boost::intrusive_ptr<FlowEntry> FlowEntryPtr;

struct RouteFlowKey {
    RouteFlowKey() : vrf(0) { ip.ipv4 = 0;};
    RouteFlowKey(uint32_t v, uint32_t ipv4) : vrf(v) { ip.ipv4 = ipv4;};
    ~RouteFlowKey() {};
    uint32_t vrf;
    union {
        uint32_t ipv4;
    } ip;
};

struct RouteFlowKeyCmp {
    bool operator()(const RouteFlowKey &lhs, const RouteFlowKey &rhs) {
        if (lhs.vrf != rhs.vrf) {
            return lhs.vrf < rhs.vrf;
        }
        return lhs.ip.ipv4 < rhs.ip.ipv4;
    }
};

struct PktControlInfo {
    PktControlInfo() : 
        vrf_(NULL), intf_(NULL), rt_(NULL), vn_(NULL), vm_(NULL), 
        vlan_nh_(false), vlan_tag_(0) { };
    virtual ~PktControlInfo() { };

    const VrfEntry *vrf_;
    const Interface *intf_;
    const Inet4UcRoute *rt_;
    const VnEntry *vn_;
    const VmEntry *vm_;
    bool  vlan_nh_;
    uint16_t vlan_tag_;
};

class PktFlowInfo {
public:
    PktFlowInfo(PktInfo *info): 
        pkt(info), source_vn(NULL), dest_vn(NULL), flow_source_vrf(-1),
        flow_dest_vrf(-1), source_sg_id_l(NULL), dest_sg_id_l(NULL),
        dest_intf(NULL), nat_done(false), nat_ip_saddr(0),
        nat_ip_daddr(0), nat_sport(0), nat_dport(0), nat_vrf(0),
        nat_dest_vrf(0), dest_vrf(0), acl(NULL), ingress(false),
        short_flow(false), local_flow(false), mdata_flow(false), ecmp(false),
        in_component_nh_idx(-1), out_component_nh_idx(-1) {
    }

    static bool ComputeDirection(const Interface *intf);
    void MdataServiceFromVm(const PktInfo *pkt, PktControlInfo *in,
                            PktControlInfo *out);
    void MdataServiceFromHost(const PktInfo *pkt, PktControlInfo *in,
                              PktControlInfo *out);
    void MdataServiceTranslate(const PktInfo *pkt, PktControlInfo *in,
                               PktControlInfo *out);
    void FloatingIpSNat(const PktInfo *pkt, PktControlInfo *in,
                        PktControlInfo *out);
    void FloatingIpDNat(const PktInfo *pkt, PktControlInfo *in,
                        PktControlInfo *out);
    void IngressProcess(const PktInfo *pkt, PktControlInfo *in,
                        PktControlInfo *out);
    void EgressProcess(const PktInfo *pkt, PktControlInfo *in,
                       PktControlInfo *out);
    void Add(const PktInfo *pkt, PktControlInfo *in,
             PktControlInfo *out);
    bool Process(const PktInfo *pkt, PktControlInfo *in, PktControlInfo *out);
    void SetEcmpFlowInfo(const PktInfo *pkt, const PktControlInfo *in,
                         const PktControlInfo *out);
    static bool GetIngressNwPolicyAclList(const Interface *intf,
                                          const VnEntry *vn,
                                          MatchPolicy *m_policy);
    bool InitFlowCmn(FlowEntry *flow, PktControlInfo *ctrl);
    void InitFwdFlow(FlowEntry *flow, const PktInfo *pkt, PktControlInfo *ctrl);
    void InitRevFlow(FlowEntry *flow, const PktInfo *pkt, PktControlInfo *ctrl);
    void RewritePktInfo(uint32_t index);
public:
    PktInfo             *pkt;

    const std::string   *source_vn;
    const std::string   *dest_vn;
    uint32_t            flow_source_vrf;
    uint32_t            flow_dest_vrf;
    const SecurityGroupList *source_sg_id_l;
    const SecurityGroupList *dest_sg_id_l;
    const Interface     *dest_intf;

    // NAT addresses
    bool                nat_done;
    uint32_t            nat_ip_saddr;
    uint32_t            nat_ip_daddr;
    uint32_t            nat_sport;
    uint32_t            nat_dport;
    // VRF for matching the NAT flow
    uint16_t            nat_vrf;
    // Modified VRF for the NAT flow
    // After flow processing, packet is assigned this VRF
    uint16_t            nat_dest_vrf;

    // Modified VRF for the forward flow
    // After flow processing, packet is assigned this VRF
    uint16_t            dest_vrf;

    // Intermediate fields used in creating flows
    const AclDBEntry    *acl;

    // Ingress flow or egress flow
    bool                ingress;
    bool                short_flow;
    bool                local_flow;
    bool                mdata_flow;

    bool                ecmp;
    uint32_t            in_component_nh_idx;
    uint32_t            out_component_nh_idx;
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

struct FlowData {
    FlowData() : 
        source_vn(""), dest_vn(""), source_sg_id_l(), dest_sg_id_l(),
        flow_source_vrf(VrfEntry::kInvalidIndex),
        flow_dest_vrf(VrfEntry::kInvalidIndex), match_p(), vn_entry(NULL),
        intf_entry(NULL), vm_entry(NULL), mirror_vrf(VrfEntry::kInvalidIndex),
        reverse_flow(), ecmp(false),
        component_nh_idx((uint32_t)CompositeNH::kInvalidComponentNHIdx),
        bytes(0), packets(0) {};

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

    FlowEntryPtr reverse_flow;
    uint16_t dest_vrf;

    // Flow direction (ingress or egress)
    bool ingress;

    bool ecmp;
    uint32_t component_nh_idx;

    // Stats
    uint64_t bytes;
    uint64_t packets;
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
    FlowEntry() :
        key(), data(), intf_in(0), flow_handle(kInvalidFlowHandle), nat(false),
        local_flow(false), short_flow(false), mdata_flow(false), 
        is_reverse_flow(false), setup_time(0), teardown_time(0),
        last_modified_time(0) {
        flow_uuid = nil_uuid(); 
        egress_uuid = nil_uuid(); 
        refcount_ = 0;
        alloc_count_.fetch_and_increment();
    };
    FlowEntry(const FlowKey &k) : 
        key(k), data(), intf_in(0), flow_handle(kInvalidFlowHandle), nat(false),
        local_flow(false), short_flow(false), mdata_flow(false),
        is_reverse_flow(false), setup_time(0), teardown_time(0),
        last_modified_time(0) {
        flow_uuid = nil_uuid(); 
        egress_uuid = nil_uuid(); 
        refcount_ = 0;
        alloc_count_.fetch_and_increment();
    };
    virtual ~FlowEntry() {
        alloc_count_.fetch_and_decrement();
    };

    FlowKey key;
    FlowData data;
    uuid flow_uuid;
    //egress_uuid is used only during flow-export and applicable only for local-flows
    uuid egress_uuid;
    uint32_t intf_in;
    uint32_t flow_handle;

    // Flow flags
    bool nat;
    bool local_flow;
    bool short_flow;
    bool mdata_flow;
    bool is_reverse_flow;

    uint64_t setup_time;
    uint64_t teardown_time;
    uint64_t last_modified_time; //used for aging

    bool ActionRecompute(MatchPolicy *policy);
    void CompareAndModify(const MatchPolicy &m_policy, bool create);
    void UpdateKSync(FlowTableKSyncEntry *entry, bool create);
    int GetRefCount() { return refcount_; }
    bool ShortFlow() const {return short_flow;};
    void MakeShortFlow();
    bool ImplicitDenyFlow() const { 
        return ((data.match_p.action_info.action & 
                 (1 << TrafficAction::IMPLICIT_DENY)) ? true : false);
    }
    void FillFlowInfo(FlowInfo &info);
    void GetPort(uint8_t &proto, uint16_t &sport, uint16_t &dport) const {
        proto = key.protocol;
        sport = key.src_port;
        dport = key.dst_port;
    }
    void SetEgressUuid();
    bool GetPolicyInfo(MatchPolicy *policy);

    void GetPolicy(const VnEntry *vn, MatchPolicy *policy);
    void GetSgList(const Interface *intf, MatchPolicy *policy);
    bool DoPolicy(const PacketHeader &hdr, MatchPolicy *policy, bool ingress);
    uint32_t MatchAcl(const PacketHeader &hdr, MatchPolicy *policy,
                      std::list<MatchAclParams> &acl);
private:
    friend class FlowTable;
    friend void intrusive_ptr_add_ref(FlowEntry *fe);
    friend void intrusive_ptr_release(FlowEntry *fe);
    static tbb::atomic<int> alloc_count_;
    // atomic refcount
    tbb::atomic<int> refcount_;
};
 
inline void intrusive_ptr_add_ref(FlowEntry *fe) {
    fe->refcount_.fetch_and_increment();
}
inline void intrusive_ptr_release(FlowEntry *fe) {
    int prev = fe->refcount_.fetch_and_decrement();
    if (prev == 1) {
        delete fe;
    }
}

struct FlowEntryCmp {
    bool operator()(const FlowEntryPtr &l, const FlowEntryPtr &r) {
        FlowKey lhs = l.get()->key;
        FlowKey rhs = r.get()->key;

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
        SgList sg_l_;
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
        vn_listener_id_(), vm_listener_id_(), vrf_listener_id_() {};
    virtual ~FlowTable();
    
    static void Init();
    static void Shutdown();
    static FlowTable *GetFlowTableObject() { return singleton_;};

    FlowEntry *Allocate(const FlowKey &key);
    void Add(FlowEntry *flow, FlowEntry *rflow);
    FlowEntry *Find(const FlowKey &key);

    bool DeleteNatFlow(FlowKey &key, bool del_nat_flow);
    bool DeleteRevFlow(FlowKey &key, bool del_reverse_flow);

    size_t Size() {return flow_entry_map_.size();};
    size_t VnFlowSize(const VnEntry *vn);

    // Test code only used method
    void DeleteFlow(const AclDBEntry *acl, const FlowKey &key, AclEntryIDList &id_list);
    void ResyncAclFlows(const AclDBEntry *acl);
    void DeleteAll();

    void SetAclFlowSandeshData(const AclDBEntry *acl, AclFlowResp &data, 
                               const FlowKey &key);
    void SetAceSandeshData(const AclDBEntry *acl, AclFlowCountResp &data, 
                           int ace_id);
    friend class FlowStatsCollector;
    friend class PktSandeshFlow;
    friend class FetchFlowRecord;
    friend class Inet4RouteUpdate;
private:
    static FlowTable* singleton_;
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

    void AclNotify(DBTablePartBase *part, DBEntryBase *e);
    void IntfNotify(DBTablePartBase *part, DBEntryBase *e);
    void VnNotify(DBTablePartBase *part, DBEntryBase *e);
    void VrfNotify(DBTablePartBase *part, DBEntryBase *e);
    std::string GetAceSandeshDataKey(const AclDBEntry *acl, int ace_id);
    std::string GetAclFlowSandeshDataKey(const AclDBEntry *acl, const FlowKey &key);

    void ResyncVnFlows(const VnEntry *vn);
    void ResyncRouteFlows(RouteFlowKey &key, SecurityGroupList &sg_l);
    void ResyncAFlow(FlowEntry *fe, MatchPolicy &policy, bool create);
    void ResyncVmPortFlows(const VmPortInterface *intf);
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
    };

    Inet4RouteUpdate(Inet4UcRouteTable *rt_table);
    Inet4RouteUpdate();
    ~Inet4RouteUpdate();
    void ManagedDelete();
    static Inet4RouteUpdate *UnicastInit(Inet4UcRouteTable *table);
    void Unregister();
    bool DeleteState(DBTablePartBase *partition, DBEntryBase *entry);
    static void WalkDone(DBTableBase *partition, Inet4RouteUpdate *rt);
private:
    DBTableBase::ListenerId id_;
    Inet4UcRouteTable *rt_table_;
    bool marked_delete_;
    void UnicastNotify(DBTablePartBase *partition, DBEntryBase *e);
    LifetimeRef<Inet4RouteUpdate> table_delete_ref_;
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
    VnFlowInfo() {};
    ~VnFlowInfo() {};

    VnEntryConstRef vn_entry;
    FlowTable::FlowEntryTree fet;
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

#define FLOW_TRACE(obj, ...)\
do {\
    Flow##obj::TraceMsg(FlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\

#endif
