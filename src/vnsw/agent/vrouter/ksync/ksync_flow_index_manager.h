/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __VNSW_AGENT_VROUTER_KSYNC_KSYNC_FLOW_INDEX_MANAGER_H__
#define __VNSW_AGENT_VROUTER_KSYNC_KSYNC_FLOW_INDEX_MANAGER_H__

#include <tbb/mutex.h>
#include <pkt/flow_entry.h>

////////////////////////////////////////////////////////////////////////////
// The module is responsible to manage assignment of vrouter flow-table index
// to the flow.
//
// The module maintains following information,
//
// KSyncFlowIndexManager::IndexTree
//     Common table containing information about which flow owns the index.
//     Each entry in tree is of type KSyncFlowIndexManager::IndexEntry.
//     It contains,
//     owner_      : Flow owning the index
//     wait_entry_ : Flow waiting for index to be aviable.
//                   VRouter does not evict flows in Hold state. Hence, the
//                   assumtion is, there can atmost be one flow waiting
//                   on an index
//
// KSyncFlowIndexEntry
//     Entry allocated per flow-entry. Important fields are,
//     index_       : The flow index currently owned by the flow.
//     state_       : Evication state for the entry. (Documented below)
//     index_owner_ : If the index for flow is not available, the entry
//                    points to flow owning the index
//
// The module tracks index last used by a flow. When a flow is invoked, agent
// will agent will get one more flow add message. The new flow-index used may
// be same as old one or different.
//
// On a flow-add, the module checks if index is free or not.
// 1. New flow is created and gets an new free index
//    Scenario:
//    Flow-1 is created and gets index-1 which is free
//
//    Processing:
//    Flow-1 is marked for INDEX_CHANGE
//    Agent allocates entry in flow-index tree and writes flow to VRouter
//
// 2. New flow is created and gets index allocated to an evicted flow
//    Scenario:
//    Flow-1 is created and gets index-1 which was earlier allocated to an
//    evicted flow(Flow-2)
//
//    Processing:
//    Sets state for Flow-1 to INDEX_WAIT
//    Sets state for Flow-2 to INDEX_EVICT
//    Starts eviction of flow-2 by deleting it
//    After Flow-1 is deleted, Flow-1 is allocated index index-1
//
// 3. Flow is evicted and created again with same index as before
//    Scenario:
//    Flow-1 with index-1 is evicted. Later Flow-1 is created again with index-1
//
//    Processing:
//    Flow-1 is marked for INDEX_CHANGE
//    Flow-1 is evicted in agent. KSync entry is deleted as part of eviction
//    After KSync entry is deleted, Flow-1 is allocated index-1 again
//
// 4. Flow is evicted and created with a new free index
//    Scenario:
//    Flow-1 with index-1 is evicted. Later Flow-1 is created with index-2
//
//    Processing:
//    Flow-1 is marked for INDEX_CHANGE
//    Flow-1 is evicted in agent. KSync entry is deleted as part of eviction
//    After KSync entry is deleted, Flow-1 is allocated index-1 again
//
// 5. Flow is evicted and gets created with index of another evicted flow
//    Scenario:
//    Flow-1 with index-1 is evicted. Flow-2 with index-2 is evicted
//    Later Flow-1 is created with index-2
//
//    Processing:
//    Sets state for Flow-1 to INDEX_WAIT
//    Sets state for Flow-2 to INDEX_EVICT
//    Starts eviction of flow-2 by deleting it
//    After Flow-1 is deleted, Flow-1 is allocated index index-1
//
// 6. Flow-1 has index-1, Flow-2 has index-2. Both Flow-1 and Flow-2 evicted
//    In next iteration Flow-1 has index-2 and Flow-2 has index-1
//
//    Flow-1 releases the index-1 first
//    Flow-1 tries to acquire index-1. Results in eviction of Flow-2
//    Flow-2 in meanwhile has changed flow_handle
//    Eviction request for Flow-2 is ignored since flow-handle do not match
//    Eventually Flow-2 releases index-2 and Flow-1 acquires the index
////////////////////////////////////////////////////////////////////////////
class FlowEntry;
class FlowProto;
class KSyncFlowIndexManager;
class FlowTableKSyncObject;

class KSyncFlowIndexEntry {
public:
    enum State {
        //  Initial state for entry on creation. A temporary state
        //  index_ set to -1, flow_handle_ may be -1 or a valid index
        INIT,
        //  Index allocated for the flow. This is steady state with
        //  flow_handle and index_ having same values
        INDEX_SET,
        // Index changed for a flow. The old index will be evicted and new
        // index allocated after eviction
        INDEX_CHANGE,
        // Index changed for flow. Waiting for new index to be available. The
        // flow will sit in wait_entry_ for owning entry
        INDEX_WAIT,
        // Flow evicted and flow deletion initiated. Once flow is deleted, the
        // index will be freed. The flow waiting for index in wait_entry_ will
        // be triggered after index is released
        INDEX_EVICT,
        // Index not yet assigned for the flow
        INDEX_UNASSIGNED,
        INVALID
    };

    KSyncFlowIndexEntry();
    virtual ~KSyncFlowIndexEntry();

    uint32_t index() const { return index_; }
    FlowEntry *index_owner() const { return index_owner_.get(); }
    FlowTableKSyncEntry *ksync_entry() const { return ksync_entry_; }
    State state() const { return state_; }
    bool IsEvicted() const;
    void Reset();
    uint32_t evict_count() const { return evict_count_; }
private:
    friend class KSyncFlowIndexManager;

    State state_;
    // Index currently owned by the flow
    uint32_t index_;
    FlowTableKSyncEntry *ksync_entry_;
    // Flow owning index_
    FlowEntryPtr index_owner_;
    // Number of times flow is evicted
    uint32_t evict_count_;
    // Delete initiated for the flow
    bool delete_in_progress_;
};

class KSyncFlowIndexManager {
public:
    struct IndexEntry {
        IndexEntry() : owner_(NULL), wait_entry_() { }
        virtual ~IndexEntry() {
            assert(owner_.get() == NULL);
            assert(wait_entry_.get() == NULL);
        }

        tbb::mutex mutex_;
        FlowEntryPtr owner_;
        FlowEntryPtr wait_entry_;
    };
    typedef std::vector<IndexEntry> IndexList;

    KSyncFlowIndexManager(KSync *ksync);
    virtual ~KSyncFlowIndexManager();
    void InitDone(uint32_t count);

    void RetryIndexAcquireRequest(FlowEntry *flow, uint32_t handle);
    void ReleaseRequest(FlowEntry *flow);

    void Add(FlowEntry *flow);
    void Change(FlowEntry *flow);
    bool Delete(FlowEntry *flow);
    void UpdateFlowHandle(FlowEntry *flow);
private:
    // Get FlowTableKSyncObject for a flow entry
    FlowTableKSyncObject *GetKSyncObject(FlowEntry *flow);
    // Invoked by KSync entry destructor to release an index
    void Release(FlowEntry *flow);

    void EvictFlow(FlowEntry *entry, FlowEntry *flow);
    void EvictFlow(FlowEntry *flow);

    void AddWaitList(FlowEntry *old_flow, FlowEntry *flow);
    void RemoveWaitList(FlowEntry *old_flow, FlowEntry *flow);

    // Try acquiring an index. Returns NULL on success and flow owning the
    // index on failure
    FlowEntryPtr AcquireIndex(FlowEntry *flow);
    // Try acquiring an index. Returns NULL on success and flow owning the
    // index on failure
    FlowEntryPtr SafeAcquireIndex(FlowEntry *flow);
    // Release the index and trigger flow waiting on it (if any)
    FlowEntryPtr ReleaseIndex(FlowEntry *flow);

private:
    KSync *ksync_;
    FlowProto *proto_;
    uint32_t count_;
    IndexList index_list_;
};

#endif //  __VNSW_AGENT_VROUTER_KSYNC_KSYNC_FLOW_INDEX_MANAGER_H__
