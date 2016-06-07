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
////////////////////////////////////////////////////////////////////////////
class FlowEntry;
class FlowProto;
class KSyncFlowIndexManager;
class FlowTableKSyncObject;
class SandeshFlowIndexInfo;

class KSyncFlowIndexManager {
public:
    // use buffer of 127 (half of the gen-id range) to identify Active
    // gen id while accounting for roll-over of gen-id
    static const uint8_t kActiveGenIdDiffMax = 127;

    struct IndexEntry {
        IndexEntry() : owner_(NULL) { }
        virtual ~IndexEntry() {
            assert(owner_.get() == NULL);
        }

        tbb::mutex mutex_;
        FlowEntryPtr owner_;
    };
    typedef std::vector<IndexEntry> IndexList;

    KSyncFlowIndexManager(KSync *ksync);
    virtual ~KSyncFlowIndexManager();
    void InitDone(uint32_t count);

    FlowEntryPtr FindByIndex(uint32_t idx);

    void Update(FlowEntry *flow);
    void Delete(FlowEntry *flow);
    void DisableSend(FlowEntry *flow, uint8_t evict_gen_id);
    void UpdateFlowHandle(FlowTableKSyncEntry *kentry, uint32_t index,
                          uint8_t gen_id);
    void TriggerKSyncEvent(FlowTableKSyncEntry *kentry,
                           KSyncEntry::KSyncEvent event);

    uint16_t sm_log_count() const { return sm_log_count_; }

private:
    uint8_t AcquireIndexUnLocked(uint32_t index, uint8_t gen_id,
                                 FlowEntry *flow);
    void ReleaseIndexUnLocked(FlowEntry *flow);

    void CreateInternal(FlowEntry *flow);

    KSync *ksync_;
    FlowProto *proto_;
    uint32_t count_;
    IndexList index_list_;
    uint16_t sm_log_count_;
};

#endif //  __VNSW_AGENT_VROUTER_KSYNC_KSYNC_FLOW_INDEX_MANAGER_H__
