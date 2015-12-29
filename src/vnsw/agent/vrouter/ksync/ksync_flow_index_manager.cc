/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#include <pkt/flow_proto.h>
#include "ksync_flow_index_manager.h"
#include "flowtable_ksync.h"
#include "ksync_init.h"

//////////////////////////////////////////////////////////////////////////////
// KSyncFlowIndexEntry routines
//////////////////////////////////////////////////////////////////////////////
KSyncFlowIndexEntry::KSyncFlowIndexEntry() :
    state_(INIT), index_(FlowEntry::kInvalidFlowHandle), ksync_entry_(NULL),
    index_owner_(NULL), evict_count_(0), delete_in_progress_(false) {
}

KSyncFlowIndexEntry::~KSyncFlowIndexEntry() {
    assert(index_owner_.get() == NULL);
    assert(ksync_entry_ == NULL);
}

bool KSyncFlowIndexEntry::IsEvicted() const {
    return (state_ == INDEX_EVICT || state_ == INDEX_CHANGE);
}

void KSyncFlowIndexEntry::Reset() {
    index_ = FlowEntry::kInvalidFlowHandle;
    state_ = INIT;
    ksync_entry_ = NULL;
    delete_in_progress_ = false;
}

//////////////////////////////////////////////////////////////////////////////
// KSyncFlowIndexManager routines
//////////////////////////////////////////////////////////////////////////////
KSyncFlowIndexManager::KSyncFlowIndexManager(KSync *ksync) :
    ksync_(ksync), proto_(NULL), count_(0), index_list_() {
}

KSyncFlowIndexManager::~KSyncFlowIndexManager() {
}

void KSyncFlowIndexManager::InitDone(uint32_t count) {
    proto_ = ksync_->agent()->pkt()->get_flow_proto();
    count_ = count;
    index_list_.resize(count);
}

//////////////////////////////////////////////////////////////////////////////
// KSyncFlowIndexManager APIs
//////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexManager::RetryIndexAcquireRequest(FlowEntry *flow,
                                                     uint32_t flow_handle) {
    if (flow == NULL)
        return;
    proto_->RetryIndexAcquireRequest(flow, flow_handle);
}

void KSyncFlowIndexManager::ReleaseRequest(FlowEntry *flow) {
    Release(flow);
}

// Handle add of a flow
void KSyncFlowIndexManager::Add(FlowEntry *flow) {
    FlowTableKSyncObject *object = GetKSyncObject(flow);
    KSyncFlowIndexEntry *index_entry = flow->ksync_index_entry();

    if (index_entry->state_ == KSyncFlowIndexEntry::INDEX_UNASSIGNED) {
        UpdateFlowHandle(flow);
        return;
    }

    // If flow already has a KSync entry, it means old flow was evicted.
    // Delete the old KSync entry. Once the KSync entry is freed, we will
    // get callback to "Release". New KSync entry will be allocated from
    // "Release" method
    if (index_entry->ksync_entry()) {
        EvictFlow(flow);
        return;
    }

    FlowEntryPtr old_flow = SafeAcquireIndex(flow);
    if (old_flow.get() != NULL) {
        // If index cannot be acquired, put flow into wait list of old_flow
        // (flow holding the index)
        EvictFlow(old_flow.get(), flow);
        return;
    }

    assert(index_entry->ksync_entry_ == NULL);
    if (index_entry->index_ == FlowEntry::kInvalidFlowHandle &&
        flow->flow_handle() == FlowEntry::kInvalidFlowHandle) {
        index_entry->state_ = KSyncFlowIndexEntry::INDEX_UNASSIGNED;
    } else {
        index_entry->state_ = KSyncFlowIndexEntry::INDEX_SET;
    }
    FlowTableKSyncEntry key(object, flow, flow->flow_handle());
    index_entry->ksync_entry_ =
        (static_cast<FlowTableKSyncEntry *>(object->Create(&key, true)));

    // Add called for deleted flow. This happens when Reverse flow is
    // deleted before getting ACK from vrouter.
    // Create and delete KSync Entry
    if (flow->deleted()) {
        Delete(flow);
    }
}

void KSyncFlowIndexManager::Change(FlowEntry *flow) {
    KSyncFlowIndexEntry *index_entry = flow->ksync_index_entry();
    if (flow->deleted() ||
        (index_entry->state_ != KSyncFlowIndexEntry::INDEX_SET)) {
        return;
    }

    // If index not yet assigned for flow, the flow will be written when later
    if (index_entry->state_ == KSyncFlowIndexEntry::INDEX_UNASSIGNED) {
        return;
    }

    FlowTableKSyncObject *object = GetKSyncObject(flow);
    object->Change(index_entry->ksync_entry_);
}

bool KSyncFlowIndexManager::Delete(FlowEntry *flow) {
    FlowTableKSyncObject *object = GetKSyncObject(flow);
    KSyncFlowIndexEntry *index_entry = flow->ksync_index_entry();
    if (index_entry->delete_in_progress_) {
        return false;
    }

    if (index_entry->state_ == KSyncFlowIndexEntry::INDEX_UNASSIGNED)
        return false;

    if (index_entry->ksync_entry_ == NULL) {
        assert(index_entry->index_ == FlowEntry::kInvalidFlowHandle);
        return false;
    }

    index_entry->delete_in_progress_ = true;
    // Hold reference to ksync-entry till this function call is over
    KSyncEntry::KSyncEntryPtr ksync_ptr = index_entry->ksync_entry_;
    object->Delete(index_entry->ksync_entry_);
    return true;
}

// Flow was written with -1 as index and vrouter allocated an index.
void KSyncFlowIndexManager::UpdateFlowHandle(FlowEntry *flow) {
    KSyncFlowIndexEntry *index_entry = flow->ksync_index_entry();

    if (index_entry->index_ == flow->flow_handle())
        return;

    // Ensure that flow is not evicted
    // A flow with index -1 will always have a flow with HOLD state as reverse
    // flow. VRouter does not evict flows in HOLD state. As a result, we dont
    // expect the flow to be evicted.
    assert(index_entry->state_ == KSyncFlowIndexEntry::INDEX_UNASSIGNED);
    assert(index_entry->index_ == FlowEntry::kInvalidFlowHandle);

    assert(index_entry->ksync_entry());
    FlowEntryPtr old_flow = SafeAcquireIndex(flow);
    if (old_flow.get() != NULL) {
        // If index cannot be acquired, put flow into wait list of old_flow
        // (flow holding the index)
        EvictFlow(old_flow.get(), flow);
        return;
    }

    flow->ksync_index_entry()->state_ = KSyncFlowIndexEntry::INDEX_SET;
    FlowTableKSyncObject *object = GetKSyncObject(flow);
    object->UpdateFlowHandle(index_entry->ksync_entry_, index_entry->index_);

    // If flow is deleted in the meanwhile, delete the flow entry
    if (flow->deleted()) {
        Delete(flow);
    }
}

void KSyncFlowIndexManager::Release(FlowEntry *flow) {
    FlowEntryPtr wait_flow = ReleaseIndex(flow);

    // Make a copy of values needed for subsequent processing before reset
    KSyncFlowIndexEntry::State state = flow->ksync_index_entry()->state_;
    FlowEntryPtr owner_entry = flow->ksync_index_entry()->index_owner_;
    uint32_t evict_index = flow->ksync_index_entry()->index_;

    // Reset the old index entry
    flow->ksync_index_entry()->Reset();

    switch (state) {

    // Entry has index set and no further transitions necessary
    case KSyncFlowIndexEntry::INDEX_SET:
        break;

    // Entry deleted when waiting for index.
    // Invoke Add() again so that the HOLD entry entry is deleted from VRouter
    case KSyncFlowIndexEntry::INDEX_WAIT: {
        RemoveWaitList(owner_entry.get(), flow);
        Add(flow);
        break;
    }

    // Index changed for flow and old index freed. Try acquring new index
    case KSyncFlowIndexEntry::INDEX_CHANGE: {
        RetryIndexAcquireRequest(wait_flow.get(), evict_index);
        Add(flow);
        break;
    }

    // Flow evicted. Now activate entry waiting for on the index
    case KSyncFlowIndexEntry::INDEX_EVICT: {
        RetryIndexAcquireRequest(wait_flow.get(), evict_index);
        break;
    }

    default:
        assert(0);
    }
    return;
}

// Release the index and return flow in wait list
FlowEntryPtr KSyncFlowIndexManager::ReleaseIndex(FlowEntry *flow) {
    FlowEntryPtr wait_flow = NULL;
    uint32_t index = flow->ksync_index_entry()->index_;
    if (index == FlowEntry::kInvalidFlowHandle) {
        return wait_flow;
    }

    tbb::mutex::scoped_lock lock(index_list_[index].mutex_);
    assert(index_list_[index].owner_.get() == flow);
    // Release the index_list_ entry
    index_list_[index].owner_ = NULL;
    wait_flow.swap(index_list_[index].wait_entry_);
    return wait_flow;
}

FlowEntryPtr KSyncFlowIndexManager::AcquireIndex(FlowEntry *flow) {
    FlowEntryPtr ret(NULL);
    // Sanity checks for a new flow
    assert(flow->ksync_index_entry()->index_ == FlowEntry::kInvalidFlowHandle);

    // Ignore entries with invalid index
    uint32_t index = flow->flow_handle();
    if (index == FlowEntry::kInvalidFlowHandle) {
        return ret;
    }

    tbb::mutex::scoped_lock lock(index_list_[index].mutex_);
    if (index_list_[index].owner_.get() != NULL)
        return index_list_[index].owner_;

    flow->ksync_index_entry()->index_ = index;
    index_list_[index].owner_ = flow;
    return ret;
}

FlowEntryPtr KSyncFlowIndexManager::SafeAcquireIndex(FlowEntry *flow) {
    if (flow->ksync_index_entry()->ksync_entry_ != NULL) {
        assert(flow->ksync_index_entry()->index_ ==
               FlowEntry::kInvalidFlowHandle);
    }
    return AcquireIndex(flow);
}

void KSyncFlowIndexManager::AddWaitList(FlowEntry *old_flow, FlowEntry *flow) {
    uint32_t index = old_flow->ksync_index_entry()->index_;
    tbb::mutex::scoped_lock lock(index_list_[index].mutex_);
    assert((index_list_[index].wait_entry_.get() == NULL) ||
           (index_list_[index].wait_entry_.get() == flow));
    index_list_[index].wait_entry_ = flow;
}

void KSyncFlowIndexManager::RemoveWaitList(FlowEntry *old_flow,
                                           FlowEntry *flow) {
    uint32_t index = old_flow->ksync_index_entry()->index_;
    tbb::mutex::scoped_lock lock(index_list_[index].mutex_);
    assert(index_list_[index].wait_entry_.get() == flow);
    index_list_[index].wait_entry_ = NULL;
}

void KSyncFlowIndexManager::EvictFlow(FlowEntry *flow) {
    KSyncFlowIndexEntry *index_entry = flow->ksync_index_entry();
    index_entry->evict_count_++;
    index_entry->state_ = KSyncFlowIndexEntry::INDEX_CHANGE;
    Delete(flow);
}

void KSyncFlowIndexManager::EvictFlow(FlowEntry *old_flow, FlowEntry *flow) {
    old_flow->ksync_index_entry()->evict_count_++;
    old_flow->ksync_index_entry()->state_ = KSyncFlowIndexEntry::INDEX_EVICT;
    KSyncFlowIndexEntry *index_entry = flow->ksync_index_entry();
    if (index_entry->state_ == KSyncFlowIndexEntry::INDEX_UNASSIGNED) {
        assert(index_entry->index_ == FlowEntry::kInvalidFlowHandle);
    } else {
        flow->ksync_index_entry()->state_ = KSyncFlowIndexEntry::INDEX_WAIT;
    }
    AddWaitList(old_flow, flow);
    proto_->EvictFlowRequest(old_flow, flow->flow_handle());
}

FlowTableKSyncObject *KSyncFlowIndexManager::GetKSyncObject(FlowEntry *flow) {
    return flow->flow_table()->ksync_object();
}
