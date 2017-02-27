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
class FlowEntry;
class FlowTable;
class FlowTableKSyncEntry;
class FlowTableKSyncObject;
class FlowEvent;
class FlowEventKSync;

#define FLOW_LOCK(flow, rflow, flow_event) \
    bool is_flow_rflow_key_same = false; \
    if (flow == rflow) { \
        if (flow_event == FlowEvent::DELETE_FLOW) { \
            assert(0); \
        } \
        is_flow_rflow_key_same = true; \
        rflow = NULL; \
    } \
    tbb::mutex tmp_mutex1, tmp_mutex2, *mutex_ptr_1, *mutex_ptr_2; \
    FlowTable::GetMutexSeq(flow ? flow->mutex() : tmp_mutex1, \
                           rflow ? rflow->mutex() : tmp_mutex2, \
                           &mutex_ptr_1, &mutex_ptr_2); \
    tbb::mutex::scoped_lock lock1(*mutex_ptr_1); \
    tbb::mutex::scoped_lock lock2(*mutex_ptr_2); \
    if (is_flow_rflow_key_same) { \
        flow->MakeShortFlow(FlowEntry::SHORT_SAME_FLOW_RFLOW_KEY); \
    }

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
    uint64_t grow_count_;
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
    static const uint32_t kPortNatFlowTableInstance = 0;
    static const uint32_t kInvalidFlowTableInstance = 0xFF;

    typedef std::map<FlowKey, FlowEntry *, Inet4FlowKeyCmp> FlowEntryMap;
    typedef std::pair<FlowKey, FlowEntry *> FlowEntryMapPair;
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
    //bool Delete(const FlowKey &flow_key);
    bool Delete(const FlowKey &key, bool del_reverse_flow);
    void DeleteAll();
    // Test code only used method
    void DeleteFlow(const AclDBEntry *acl, const FlowKey &key,
                    AclEntryIDList &id_list);

    // Accessor routines
    Agent *agent() const { return agent_; }
    uint16_t table_index() const { return table_index_; }
    size_t Size() { return flow_entry_map_.size(); }
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
    void Copy(FlowEntry *lhs, FlowEntry *rhs, bool update);
    void SetAclFlowSandeshData(const AclDBEntry *acl, AclFlowResp &data, 
                               const int last_count);
    void SetAceSandeshData(const AclDBEntry *acl, AclFlowCountResp &data, 
                           int ace_id);
   
    void RecomputeFlow(FlowEntry *flow);
    void DeleteMessage(FlowEntry *flow);

    void DeleteVrf(VrfEntry *vrf);

    void HandleRevaluateDBEntry(const DBEntry *entry, FlowEntry *flow,
                                bool active_flow, bool deleted_flow);
    void HandleKSyncError(FlowEntry *flow, FlowTableKSyncEntry *ksync_entry,
                          int ksync_error, uint32_t flow_handle,
                          uint32_t gen_id);
    boost::uuids::uuid rand_gen();

    void UpdateKSync(FlowEntry *flow, bool update);
    void DeleteKSync(FlowEntry *flow);

    // Free list
    void GrowFreeList();
    FlowEntryFreeList *free_list() { return &free_list_; }

    void ProcessKSyncFlowEvent(const FlowEventKSync *req, FlowEntry *flow);
    bool ProcessFlowEvent(const FlowEvent *req, FlowEntry *flow,
                          FlowEntry *rflow);
    void PopulateFlowEntriesUsingKey(const FlowKey &key, bool reverse_flow,
                                     FlowEntry** flow, FlowEntry** rflow);

    // Concurrency check to ensure all flow-table and free-list manipulations
    // are done from FlowEvent task context only
    bool ConcurrencyCheck(int task_id);
    int flow_task_id() const { return flow_task_id_; }
    int flow_update_task_id() const { return flow_update_task_id_; }
    int flow_delete_task_id() const { return flow_delete_task_id_; }
    int flow_ksync_task_id() const { return flow_ksync_task_id_; }
    static void GetMutexSeq(tbb::mutex &mutex1, tbb::mutex &mutex2,
                            tbb::mutex **mutex_ptr_1, tbb::mutex **mutex_ptr_2);

    friend class FlowStatsCollector;
    friend class PktSandeshFlow;
    friend class PktSandeshFlowStats;
    friend class FetchFlowRecord;
    friend class PktFlowInfo;
    friend void intrusive_ptr_release(FlowEntry *fe);
private:
    void DisableKSyncSend(FlowEntry *flow, uint32_t evict_gen_id);
    bool IsEvictedFlow(const FlowKey &key);

    void DeleteInternal(FlowEntry *fe, uint64_t t, const RevFlowDepParams &p);
    void DeleteFlowInfo(FlowEntry *fe, const RevFlowDepParams &params);

    void AddFlowInfo(FlowEntry *fe);
    void UpdateReverseFlow(FlowEntry *flow, FlowEntry *rflow);

    void UpdateUnLocked(FlowEntry *flow, FlowEntry *rflow);
    void AddInternal(FlowEntry *flow, FlowEntry *new_flow, FlowEntry *rflow,
                     FlowEntry *new_rflow, bool fwd_flow_update,
                     bool rev_flow_update);
    void Add(FlowEntry *flow, FlowEntry *new_flow, FlowEntry *rflow,
             FlowEntry *new_rflow, bool fwd_flow_update, bool rev_flow_update);
    void EvictFlow(FlowEntry *flow, FlowEntry *rflow, uint32_t evict_gen_id);
    bool DeleteFlows(FlowEntry *flow, FlowEntry *rflow);
    bool DeleteUnLocked(const FlowKey &key, bool del_reverse_flow);
    bool DeleteUnLocked(bool del_reverse_flow, FlowEntry *flow,
                        FlowEntry *rflow);

    Agent *agent_;
    boost::uuids::random_generator rand_gen_;
    uint16_t table_index_;
    FlowTableKSyncObject *ksync_object_;
    FlowEntryMap flow_entry_map_;

    FlowIndexTree flow_index_tree_;
    // maintain the linklocal flow info against allocated fd, debug purpose only
    LinkLocalFlowInfoMap linklocal_flow_info_map_;
    FlowEntryFreeList free_list_;
    tbb::mutex mutex_;
    int flow_task_id_;
    int flow_update_task_id_;
    int flow_delete_task_id_;
    int flow_ksync_task_id_;
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
extern SandeshTraceBufferPtr FlowTraceBuf;
extern void SetActionStr(const FlowAction &, std::vector<ActionStr> &);

#define FLOW_TRACE(obj, ...)\
do {\
    Flow##obj::TraceMsg(FlowTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\

#endif
