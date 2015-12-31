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
#include <pkt/flow_entry.h>
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

class FlowStatsCollector;
class PktSandeshFlow;
class FetchFlowRecord;
struct VmFlowInfo;
class FlowEntry;
class FlowTable;
class FlowTableKSyncEntry;
class FlowTableKSyncObject;
class FlowEvent;

/////////////////////////////////////////////////////////////////////////////
// Class to manage free-list of flow-entries
// Flow allocation can happen from multiple threads. In scaled scenarios
// allocation of flow-entries in multi-thread environment adds overheads.
// The FlowEntryFreeList helps to maintain a per task free-list. Alloc/Free
// can happen withou lock.
//
// Alloc and Free happens in a chunk. Alloc/Free are done based on thresholds
// in task context of the corresponding flow-table
/////////////////////////////////////////////////////////////////////////////
class FlowEntryFreeList {
public:
    static const uint32_t kInitCount = (25 * 1000);
    static const uint32_t kTestInitCount = (5 * 1000);
    static const uint32_t kGrowSize = (1 * 1000);
    static const uint32_t kMinThreshold = (4 * 1000);

    typedef boost::intrusive::member_hook<FlowEntry,
            boost::intrusive::list_member_hook<>,
            &FlowEntry::free_list_node_> Node;
    typedef boost::intrusive::list<FlowEntry, Node> FreeList;

    FlowEntryFreeList(FlowTable *table);
    virtual ~FlowEntryFreeList();

    FlowEntry *Allocate(const FlowKey &key);
    void Free(FlowEntry *flow);
    void Grow();
    uint32_t max_count() const { return max_count_; }
    uint32_t free_count() const { return free_list_.size(); }
    uint32_t alloc_count() const { return (max_count_ - free_list_.size()); }
    uint32_t total_alloc() const { return total_alloc_; }
    uint32_t total_free() const { return total_free_; }
private:
    FlowTable *table_;
    uint32_t max_count_;
    bool grow_pending_;
    uint64_t total_alloc_;
    uint64_t total_free_;
    FreeList free_list_;
    DISALLOW_COPY_AND_ASSIGN(FlowEntryFreeList);
};

/////////////////////////////////////////////////////////////////////////////
// Flow addition is a two step process.
// - FlowHandler :
//   Flow is created in this context (file pkt_flow_info.cc).
//   There can potentially be multiple FlowHandler task running in parallel
// - FlowTable :
//   This module will maintain a tree of all flows created. It is also
//   responsible to generate KSync events. It is run in a single task context
//
//   Functionality of FlowTable:
//   1. Manage flow_entry_map_ which contains all flows
//   2. Enforce the per-VM flow limits
//   3. Generate events to KSync and FlowMgmt modueles
/////////////////////////////////////////////////////////////////////////////
struct FlowTaskMsg : public InterTaskMsg {
    FlowTaskMsg(FlowEntry * fe) : InterTaskMsg(0), fe_ptr(fe) { }
    virtual ~FlowTaskMsg() { }

    FlowEntryPtr fe_ptr;
};

struct Inet4FlowKeyCmp {
    bool operator()(const FlowKey &lhs, const FlowKey &rhs) const {
        const FlowKey &lhs_base = static_cast<const FlowKey &>(lhs);
        return lhs_base.IsLess(rhs);
    }
};

class FlowTable {
public:
    static boost::uuids::random_generator rand_gen_;

    typedef std::map<FlowKey, FlowEntry *, Inet4FlowKeyCmp> FlowEntryMap;
    typedef std::pair<FlowKey, FlowEntry *> FlowEntryMapPair;
    typedef std::map<const VmEntry *, VmFlowInfo *> VmFlowTree;
    typedef std::pair<const VmEntry *, VmFlowInfo *> VmFlowPair;
    typedef boost::function<bool(FlowEntry *flow)> FlowEntryCb;
    typedef std::vector<FlowEntryPtr> FlowIndexTree;

    struct LinkLocalFlowInfo {
        uint32_t flow_index;
        FlowKey flow_key;
        uint64_t timestamp;

        LinkLocalFlowInfo(uint32_t index, const FlowKey &key, uint64_t t) :
            flow_index(index), flow_key(key), timestamp(t) {}
    };
    typedef std::map<int, LinkLocalFlowInfo> LinkLocalFlowInfoMap;
    typedef std::pair<int, LinkLocalFlowInfo> LinkLocalFlowInfoPair;

    FlowTable(Agent *agent, uint16_t table_index);
    virtual ~FlowTable();

    void Init();
    void InitDone();
    void Shutdown();

    // Accessor routines
    void set_ksync_object(FlowTableKSyncObject *obj) { ksync_object_ = obj; }
    FlowTableKSyncObject *ksync_object() const { return ksync_object_; }

    // Table managment routines
    FlowEntry *Locate(FlowEntry *flow, uint64_t t);
    FlowEntry *Find(const FlowKey &key);
    void Add(FlowEntry *flow, FlowEntry *rflow);
    void Update(FlowEntry *flow, FlowEntry *rflow);
    bool Delete(const FlowKey &key, bool del_reverse_flow);
    bool Delete(const FlowKey &flow_key);
    void DeleteAll();
    // Test code only used method
    void DeleteFlow(const AclDBEntry *acl, const FlowKey &key,
                    AclEntryIDList &id_list);
    bool ValidFlowMove(const FlowEntry *new_flow,
                       const FlowEntry *old_flow) const;

    // VM/VN flow info routines
    uint32_t VmFlowCount(const VmEntry *vm);
    uint32_t VmLinkLocalFlowCount(const VmEntry *vm);

    // Accessor routines
    Agent *agent() const { return agent_; }
    uint16_t table_index() const { return table_index_; }
    size_t Size() { return flow_entry_map_.size(); }
    uint32_t linklocal_flow_count() const { return linklocal_flow_count_; }
    FlowTable::FlowEntryMap::iterator begin() {
        return flow_entry_map_.begin();
    }
    FlowTable::FlowEntryMap::iterator end() {
        return flow_entry_map_.end(); 
    }

    const LinkLocalFlowInfoMap &linklocal_flow_info_map() {
        return linklocal_flow_info_map_;
    }
    void AddLinkLocalFlowInfo(int fd, uint32_t index, const FlowKey &key,
                              const uint64_t timestamp);
    void DelLinkLocalFlowInfo(int fd);

    static const char *TaskName() { return kTaskFlowEvent; }
    // Sandesh routines
    void Copy(FlowEntry *lhs, const FlowEntry *rhs);
    void SetAclFlowSandeshData(const AclDBEntry *acl, AclFlowResp &data, 
                               const int last_count);
    void SetAceSandeshData(const AclDBEntry *acl, AclFlowCountResp &data, 
                           int ace_id);
   
    void RevaluateFlow(FlowEntry *flow);
    void DeleteMessage(FlowEntry *flow);

    void RevaluateInterface(FlowEntry *flow);
    void RevaluateVn(FlowEntry *flow);
    void RevaluateAcl(FlowEntry *flow);
    void RevaluateNh(FlowEntry *flow);
    void DeleteVrf(VrfEntry *vrf);
    void RevaluateRoute(FlowEntry *flow, const AgentRoute *route);
    bool FlowResponseHandler(const FlowEvent *req);

    bool FlowRouteMatch(const InetUnicastRouteEntry *rt, uint32_t vrf,
                        Address::Family family, const IpAddress &ip,
                        uint8_t plen);
    bool FlowInetRpfMatch(FlowEntry *flow, const InetUnicastRouteEntry *rt);
    bool FlowInetSrcMatch(FlowEntry *flow, const InetUnicastRouteEntry *rt);
    bool FlowInetDstMatch(FlowEntry *flow, const InetUnicastRouteEntry *rt);
    bool FlowBridgeSrcMatch(FlowEntry *flow, const BridgeRouteEntry *rt);
    bool FlowBridgeDstMatch(FlowEntry *flow, const BridgeRouteEntry *rt);
    bool RevaluateSgList(FlowEntry *flow, const AgentRoute *rt,
                         const SecurityGroupList &sg_list);
    bool RevaluateRpfNH(FlowEntry *flow, const AgentRoute *rt);

    void UpdateKSync(FlowEntry *flow, bool update);
    void DeleteKSync(FlowEntry *flow);

    // FlowStatsCollector request queue events
    void NotifyFlowStatsCollector(FlowEntry *fe);
    void KSyncSetFlowHandle(FlowEntry *flow, uint32_t flow_handle);

    // Free list
    void GrowFreeList();
    FlowEntryFreeList *free_list() { return &free_list_; }

    friend class FlowStatsCollector;
    friend class PktSandeshFlow;
    friend class PktSandeshFlowStats;
    friend class FetchFlowRecord;
    friend class PktFlowInfo;
    friend void intrusive_ptr_release(FlowEntry *fe);
private:

    void DeleteInternal(FlowEntryMap::iterator &it, uint64_t t);
    void ResyncAFlow(FlowEntry *fe);
    void DeleteFlowInfo(FlowEntry *fe);
    void DeleteVmFlowInfo(FlowEntry *fe);
    void DeleteVmFlowInfo(FlowEntry *fe, const VmEntry *vm);
    void DeleteVmFlows(const VmEntry *vm);

    void AddFlowInfo(FlowEntry *fe);
    void AddVmFlowInfo(FlowEntry *fe);
    void AddVmFlowInfo(FlowEntry *fe, const VmEntry *vm);

    void UpdateReverseFlow(FlowEntry *flow, FlowEntry *rflow);

    void AddInternal(FlowEntry *flow, FlowEntry *new_flow, FlowEntry *rflow,
                     FlowEntry *new_rflow, bool update);
    void Add(FlowEntry *flow, FlowEntry *new_flow, FlowEntry *rflow,
             FlowEntry *new_rflow, bool update);
    void GetMutexSeq(tbb::mutex &mutex1, tbb::mutex &mutex2,
                     tbb::mutex **mutex_ptr_1, tbb::mutex **mutex_ptr_2);
    Agent *agent_;
    uint16_t table_index_;
    FlowTableKSyncObject *ksync_object_;
    FlowEntryMap flow_entry_map_;

    VmFlowTree vm_flow_tree_;
    uint32_t linklocal_flow_count_;  // total linklocal flows in the agent
    FlowIndexTree flow_index_tree_;
    // maintain the linklocal flow info against allocated fd, debug purpose only
    LinkLocalFlowInfoMap linklocal_flow_info_map_;
    FlowEntryFreeList free_list_;
    tbb::mutex mutex_;
    DISALLOW_COPY_AND_ASSIGN(FlowTable);
};

struct FlowEntryCmp {
    bool operator()(const FlowEntryPtr &l, const FlowEntryPtr &r) {
        FlowEntry *lhs = l.get();
        FlowEntry *rhs = r.get();

        return (lhs < rhs);
    }
};

typedef std::set<FlowEntryPtr, FlowEntryCmp> FlowEntryTree;
struct VmFlowInfo {
    VmFlowInfo() : linklocal_flow_count() {}
    ~VmFlowInfo() {}

    VmEntryConstRef vm_entry;
    FlowEntryTree fet;
    uint32_t linklocal_flow_count;
};

extern SandeshTraceBufferPtr FlowTraceBuf;
extern void SetActionStr(const FlowAction &, std::vector<ActionStr> &);

#define FLOW_TRACE(obj, ...)\
do {\
    Flow##obj::TraceMsg(FlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\

#endif
