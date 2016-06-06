/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#include <pkt/flow_proto.h>
#include <pkt/pkt_types.h>
#include <pkt/flow_entry.h>
#include <init/agent_param.h>
#include "ksync_flow_index_manager.h"
#include "flowtable_ksync.h"
#include "ksync_init.h"

#define INDEX_LOCK(idx) \
    tbb::mutex tmp_mutex, *mutex_ptr;\
    if (idx == FlowEntry::kInvalidFlowHandle) {\
        mutex_ptr = &tmp_mutex;\
    } else {\
        mutex_ptr = &index_list_[idx].mutex_;\
    }\
    tbb::mutex::scoped_lock lock(*mutex_ptr);

//////////////////////////////////////////////////////////////////////////////
// KSyncFlowIndexManager routines
//////////////////////////////////////////////////////////////////////////////
KSyncFlowIndexManager::KSyncFlowIndexManager(KSync *ksync) :
    ksync_(ksync), proto_(NULL), count_(0), index_list_(), sm_log_count_(0) {
}

KSyncFlowIndexManager::~KSyncFlowIndexManager() {
}

void KSyncFlowIndexManager::InitDone(uint32_t count) {
    proto_ = ksync_->agent()->pkt()->get_flow_proto();
    count_ = count;
    index_list_.resize(count);
    sm_log_count_ = ksync_->agent()->params()->flow_index_sm_log_count();
}

//////////////////////////////////////////////////////////////////////////////
// KSyncFlowIndexManager Utility methods
//////////////////////////////////////////////////////////////////////////////
FlowEntryPtr KSyncFlowIndexManager::FindByIndex(uint32_t idx) {
    if (index_list_[idx].owner_.get() != NULL)
        return index_list_[idx].owner_;
    return FlowEntryPtr(NULL);
}

//////////////////////////////////////////////////////////////////////////////
// Update:
//     Flow module triggers this API to propagate Add/Change to vrouter
//
//     API tries to acquire flow handle and create the KSync entry for
//     flow, if there is an existing KSync entry, if validates for change
//     in flow handle and triggers change / Delete-Add accordingly
//////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexManager::Update(FlowEntry *flow) {
    FlowTableKSyncObject *object = flow->flow_table()->ksync_object();
    // flow should not be delete marked
    assert(!flow->deleted());
    if (flow->ksync_entry_ == NULL) {
        CreateInternal(flow);
    } else {
        if (flow->flow_handle() != flow->ksync_entry_->hash_id()) {
            // if flow handle changes delete the previous record from
            // vrouter and install new
            Delete(flow);
            CreateInternal(flow);
        } else {
            INDEX_LOCK(flow->flow_handle());
            uint8_t evict_gen_id = AcquireIndexUnLocked(flow->flow_handle(),
                                                        flow->gen_id(),
                                                        flow);
            flow->ksync_entry_->set_gen_id(flow->gen_id());
            flow->ksync_entry_->set_evict_gen_id(evict_gen_id);
            flow->LogFlow(FlowEventLog::FLOW_UPDATE, flow->ksync_entry_,
                          flow->flow_handle(), flow->gen_id());
            object->Change(flow->ksync_entry_);
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
// Delete:
//     Flow module triggers this API to propagate Delete to vrouter
//
//     API tries triggers Delete and Release index ownership
//////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexManager::Delete(FlowEntry *flow) {
    if (flow->ksync_entry_ != NULL) {
        FlowTableKSyncObject *object = flow->flow_table()->ksync_object();
        INDEX_LOCK(flow->ksync_entry_->hash_id());
        ReleaseIndexUnLocked(flow);
        flow->LogFlow(FlowEventLog::FLOW_DELETE, flow->ksync_entry_,
                      flow->flow_handle(), flow->gen_id());
        FlowTableKSyncEntry *kentry = flow->ksync_entry_;
        // reset ksync_entry_ before triggering delete as Delete
        // may just free the entry, if there is nothing to encode
        // which may inturn free the flow entry pointer
        flow->ksync_entry_ = NULL;
        object->Delete(kentry);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Delete:
//     Flow module triggers this API to suppress message to vrouter
//
//     API ensures that evict_gen_id has different value from gen_id
//////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexManager::DisableSend(FlowEntry *flow,
                                        uint8_t evict_gen_id) {
    FlowTableKSyncEntry *kentry = flow->ksync_entry_;
    if (kentry != NULL) {
        INDEX_LOCK(flow->flow_handle());
        kentry->set_evict_gen_id(evict_gen_id);
    }
}

//////////////////////////////////////////////////////////////////////////////
// UpdateFlowHandle:
//     Flow module triggers this API to update flow handle for the reverse
//     flow.
//
//     API Assigns index and gen id received from vrouter to KSync entry
//     an let the further operation use correct index and gen-id to
//     communicate with vrouter
//     KsyncEntry here should always have kInvalidFlowHandle
//////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexManager::UpdateFlowHandle(FlowTableKSyncEntry *kentry,
                                             uint32_t index,
                                             uint8_t gen_id) {
    assert(index != FlowEntry::kInvalidFlowHandle);
    FlowEntry *flow = kentry->flow_entry().get();
    FlowTableKSyncObject *object = flow->flow_table()->ksync_object();
    uint8_t evict_gen_id;
    if (!kentry->IsDeleted()) {
        // if kentry is not deleted flow should always point to this
        // ksync entry only
        assert(kentry == flow->ksync_entry_);
        FlowEntry *rflow = flow->reverse_flow_entry();
        // Check if index and gen id is corresponding to the info sent
        // to vrouter, if vrouter has allocated flow index, following
        // check will fail and it will follow through
        if (kentry->hash_id() == index &&
            kentry->vrouter_gen_id() == gen_id) {
            return;
        }

        // Index allocation should happen only if flow_handle is
        // kInvalidFlowHandle
        assert(flow->flow_handle() == FlowEntry::kInvalidFlowHandle);
        INDEX_LOCK(index);
        flow->LogFlow(FlowEventLog::FLOW_HANDLE_ASSIGN, kentry, index, gen_id);
        flow->set_flow_handle(index, gen_id);
        object->UpdateFlowHandle(kentry, index);
        evict_gen_id = AcquireIndexUnLocked(flow->flow_handle(),
                                            flow->gen_id(), flow);
        kentry->set_gen_id(gen_id);
        kentry->set_evict_gen_id(evict_gen_id);
        if (rflow) {
            rflow->flow_table()->UpdateKSync(rflow, true);
        }
    } else {
        // KSync entry is deleted, This happens when Reverse flow
        // is deleted before getting an ACK from vrouter.
        // this can happen if we delete the flow itself or if we
        // get a new index from packet processing which will result
        // in deletion of this KSync entry
        // just use the correct key to encode delete msg
        INDEX_LOCK(index);
        flow->LogFlow(FlowEventLog::FLOW_HANDLE_ASSIGN, kentry, index, gen_id);
        evict_gen_id = AcquireIndexUnLocked(index, gen_id, NULL);
        // following processing is required only if kentry successfully
        // acquired the index, on index acquire failure we will anyway
        // skip sending message to vrouter due to evicted state.
        // So avoid changing ksync entry handle to avoid replacing
        // hash id of an active ksync entry (Bug - 1587540)
        if (evict_gen_id == gen_id) {
            object->UpdateFlowHandle(kentry, index);
            kentry->set_gen_id(gen_id);
            kentry->set_evict_gen_id(evict_gen_id);
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
// TriggerKSyncEvent:
//     Flow module triggers this API to propagate KSyncEvent to KSync Entry
//
//     API ensures that we hold Index Lock, to allow any pending operation
//     on KSync Entry to happen with Index Lock Held
//////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexManager::TriggerKSyncEvent(FlowTableKSyncEntry *kentry,
                                              KSyncEntry::KSyncEvent event) {
    FlowTableKSyncObject *object = static_cast<FlowTableKSyncObject *>
        (kentry->GetObject());
    INDEX_LOCK(kentry->hash_id());
    object->GenerateKSyncEvent(kentry, event);
}

//////////////////////////////////////////////////////////////////////////////
// AcquireIndexUnLocked:
//     Tries to Acquire flow index by comparing gen id, evicts the entry
//     which has an older gen id
//////////////////////////////////////////////////////////////////////////////
uint8_t KSyncFlowIndexManager::AcquireIndexUnLocked(uint32_t index,
                                                    uint8_t gen_id,
                                                    FlowEntry *flow) {
    uint8_t evict_gen_id = gen_id;
    if (index == FlowEntry::kInvalidFlowHandle) {
        return evict_gen_id;
    }

    if (index_list_[index].owner_ != flow) {
        if (index_list_[index].owner_ != NULL) {
            FlowTableKSyncEntry *old = index_list_[index].owner_->ksync_entry_;
            uint8_t diff = gen_id - old->gen_id();
            if (diff < kActiveGenIdDiffMax) {
                // evict old entry
                old->set_evict_gen_id(gen_id);
                old->flow_entry()->LogFlow(FlowEventLog::FLOW_EVICT, old,
                                           index, gen_id);
                proto_->EvictFlowRequest(old->flow_entry().get(),
                                         old->hash_id(), old->gen_id(), gen_id);
                index_list_[index].owner_ = flow;
            } else {
                // evict current entry
                evict_gen_id = old->gen_id();
                if (flow) {
                    // KSyncEntry can be NULL at this point since acquire
                    // index can be done before creating KSyncEntry
                    // LogFlow needs to handle NULL KSyncEntry pointer
                    flow->LogFlow(FlowEventLog::FLOW_EVICT, flow->ksync_entry_,
                                  flow->flow_handle(), evict_gen_id);
                    proto_->EvictFlowRequest(flow, flow->flow_handle(),
                                             flow->gen_id(), evict_gen_id);
                }
            }
        } else {
            index_list_[index].owner_ = flow;
        }
    }

    return evict_gen_id;
}

//////////////////////////////////////////////////////////////////////////////
// ReleaseIndexUnLocked:
//     Release the held flow index
//////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexManager::ReleaseIndexUnLocked(FlowEntry *flow) {
    if (flow->ksync_entry_->hash_id() == FlowEntry::kInvalidFlowHandle) {
        return;
    }

    if (index_list_[flow->ksync_entry_->hash_id()].owner_ == flow) {
        index_list_[flow->ksync_entry_->hash_id()].owner_ = NULL;
    }
}

void KSyncFlowIndexManager::CreateInternal(FlowEntry *flow) {
    FlowTableKSyncObject *object = flow->flow_table()->ksync_object();
    INDEX_LOCK(flow->flow_handle());
    uint8_t evict_gen_id = AcquireIndexUnLocked(flow->flow_handle(),
                                                flow->gen_id(),
                                                flow);
    FlowTableKSyncEntry key(object, flow, flow->flow_handle());
    key.set_evict_gen_id(evict_gen_id);
    flow->ksync_entry_ =
        static_cast<FlowTableKSyncEntry *>(object->Create(&key));
    // Update gen id after create to handle case where ksync entry
    // was not constructed newly and was resued, thus gen id was
    // not updated in create
    flow->ksync_entry_->set_gen_id(flow->gen_id());
    flow->ksync_entry_->set_evict_gen_id(evict_gen_id);
    flow->LogFlow(FlowEventLog::FLOW_ADD, flow->ksync_entry_,
                  flow->flow_handle(), flow->gen_id());
}
