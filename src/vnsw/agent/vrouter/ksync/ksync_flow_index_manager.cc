/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#include <pkt/flow_proto.h>
#include <init/agent_param.h>
#include "ksync_flow_index_manager.h"
#include "flowtable_ksync.h"
#include "ksync_init.h"

//////////////////////////////////////////////////////////////////////////////
// KSyncFlowIndexEntry routines
//////////////////////////////////////////////////////////////////////////////
KSyncFlowIndexEntry::KSyncFlowIndexEntry() :
    state_(INIT), index_(FlowEntry::kInvalidFlowHandle), ksync_entry_(NULL),
    index_owner_(NULL), skip_delete_(false), delete_in_progress_(false),
    event_log_index_(0), event_logs_(NULL) {
}

KSyncFlowIndexEntry::~KSyncFlowIndexEntry() {
    assert(index_owner_.get() == NULL);
    assert(ksync_entry_ == NULL);
    if (event_logs_ != NULL) {
        delete [] event_logs_;
    }
}

static const char *kStateDescription[] = {
    "INIT",
    "SET",
    "CHANGE",
    "EVICT",
    "UNASSIGNED",
    "FAILED",
    "INVALID"
};
static const char *StateToString(KSyncFlowIndexEntry::State state) {
    assert(state <= KSyncFlowIndexEntry::INVALID);
    return kStateDescription[(int)state];
}

static const char *kEventDescription[] = {
    "ADD",
    "CHANGE",
    "DELETE",
    "INDEX_ASSIGN",
    "VROUTER_ERROR",
    "KSYNC_FREE",
    "INVALID"
};
static const char *EventToString(KSyncFlowIndexEntry::Event event) {
    assert(event <= KSyncFlowIndexEntry::INVALID_EVENT);
    return kEventDescription[(int)event];
}

void KSyncFlowIndexEntry::LogInternal(KSyncFlowIndexManager *manager,
                                      const std::string &description,
                                      FlowEntry *flow,
                                      Event event, uint32_t index,
                                      FlowEntry *evict_flow) {
    int idx1 = (index_ == FlowEntry::kInvalidFlowHandle) ? -1 : index_;
    int idx2 = (index == FlowEntry::kInvalidFlowHandle) ? -1 : index;
    LOG(DEBUG, description
        << " Flow : " << (void *)flow
        << " State : " << StateToString(state_)
        << " Handle : " << flow->flow_handle()
        << " Event : " << EventToString(event)
        << " Index : " << idx1
        << " NewIndex : " << idx2
        << " SkipDel : " << (skip_delete_ ? "true" : "false")
        << " Key < " << flow->KeyString() << " >");

    if (manager->sm_log_count() == 0)
        return;

    if (event_logs_ == NULL) {
        event_log_index_ = 0;
        event_logs_ = new EventLog[manager->sm_log_count()];
    }

    EventLog *log = &event_logs_[(event_log_index_ % manager->sm_log_count())];
    event_log_index_++;

    log->time_ = ClockMonotonicUsec();
    log->state_ = state_;
    log->event_ = event;
    if (flow)
        log->flow_handle_ = flow->flow_handle();
    else
        log->flow_handle_ = FlowEntry::kInvalidFlowHandle;
    log->index_ = index_;
    log->ksync_entry_ = ksync_entry_;
    log->skip_delete_ = skip_delete_;
    log->delete_in_progress_ = delete_in_progress_;
    log->evict_flow_ = evict_flow;
    log->vrouter_flow_handle_ = index;
}

void KSyncFlowIndexEntry::Log(KSyncFlowIndexManager *manager, FlowEntry *flow,
                              Event event, uint32_t index) {
    LogInternal(manager, "FlowIndexSm", flow, event, index, NULL);
}

void KSyncFlowIndexEntry::SetSandeshFlowTrace(EventLog *log,
                                              SandeshFlowIndexTrace *info) {
    info->set_timestamp(log->time_);
    info->set_index(log->index_);
    info->set_flow_handle(log->flow_handle_);
    info->set_state(StateToString(log->state_));
    info->set_event(EventToString(log->event_));
    info->set_skip_delete(log->skip_delete_);
    info->set_delete_in_progress(log->delete_in_progress_);
    info->set_vrouter_flow_handle(log->vrouter_flow_handle_);
}

void KSyncFlowIndexEntry::SetSandeshData(KSyncFlowIndexManager *manager,
                                         SandeshFlowIndexInfo *info) {
    info->set_index(index_);
    info->set_state(StateToString(state_));
    info->set_delete_in_progress(delete_in_progress_);
    info->set_trace_count(event_log_index_);
    if (manager->sm_log_count() == 0)
        return;
    int start = 0;
    if (event_log_index_ >= manager->sm_log_count())
        start = event_log_index_ % manager->sm_log_count();
    int count = event_log_index_;
    if (count > manager->sm_log_count())
        count = manager->sm_log_count();
    std::vector<SandeshFlowIndexTrace> trace_list;
    for (int i = 0; i < count; i++) {
        SandeshFlowIndexTrace trace;
        EventLog *log = &event_logs_[((start + i) % manager->sm_log_count())];
        SetSandeshFlowTrace(log, &trace);
        trace_list.push_back(trace);
    }
    info->set_flow_index_trace(trace_list);
}

//////////////////////////////////////////////////////////////////////////////
// KSyncFlowIndexEntry State Machine routines
//////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexEntry::KSyncAddChange(KSyncFlowIndexManager *manager,
                                         FlowEntry *flow) {
    FlowTableKSyncObject *object = flow->flow_table()->ksync_object();
    if (ksync_entry_ == NULL) {
        FlowTableKSyncEntry key(object, flow, index_);
        ksync_entry_ = (static_cast<FlowTableKSyncEntry *>
                        (object->Create(&key, true)));
        // Add called for deleted flow. This happens when Reverse flow is
        // deleted before getting ACK from vrouter.
        // Create and delete KSync Entry
        if (flow->deleted()) {
            KSyncDelete(manager, flow);
        }
    } else {
        // Add called for deleted flow. This happens when Reverse flow is
        // deleted before getting ACK from vrouter.
        // Create and delete KSync Entry
        if (flow->deleted()) {
            KSyncDelete(manager, flow);
        } else {
            object->Change(ksync_entry_);
        }
    }
}

void KSyncFlowIndexEntry::KSyncDelete(KSyncFlowIndexManager *manager,
                                      FlowEntry *flow) {
    if (ksync_entry_ == NULL) {
        assert(index_ == FlowEntry::kInvalidFlowHandle);
        // Nothing to do if KSync entry not allocated
        // Invoke Release to move the state machine
        manager->Release(flow);
        return;
    }

    if (delete_in_progress_) {
        // Delete already issued
        return;
    }
    delete_in_progress_ = true;
    FlowTableKSyncObject *object = flow->flow_table()->ksync_object();
    object->Delete(ksync_entry_);
}

void KSyncFlowIndexEntry::KSyncUpdateFlowHandle(KSyncFlowIndexManager *manager,
                                                FlowEntry *flow) {
    FlowTableKSyncObject *object = flow->flow_table()->ksync_object();
    object->UpdateFlowHandle(ksync_entry_, index_);
    if (flow->deleted()) {
        KSyncDelete(manager, flow);
    }
}

void KSyncFlowIndexEntry::HandleEvent(KSyncFlowIndexManager *manager,
                                      FlowEntry *flow, Event event,
                                      uint32_t index) {
    Log(manager, flow, event, index);
    switch (state_) {
    case INIT:
        InitSm(manager, flow, event, index);
        break;

    case INDEX_UNASSIGNED:
        IndexUnassignedSm(manager, flow, event, index);
        break;

    case INDEX_SET:
        IndexSetSm(manager, flow, event, index);
        break;

    case INDEX_CHANGE:
        IndexChangeSm(manager, flow, event, index);
        break;

    case INDEX_EVICT:
        IndexEvictSm(manager, flow, event, index);
        break;

    case INDEX_FAILED:
        IndexFailedSm(manager, flow, event, index);
        break;

    default:
        assert(0);
        break;
    }
}

void KSyncFlowIndexEntry::EvictFlow(KSyncFlowIndexManager *manager,
                                    FlowEntry *flow, State next_state,
                                    bool skip_del) {
    if (index_ == FlowEntry::kInvalidFlowHandle) {
        KSyncDelete(manager, flow);
        return;
    }

    flow->ksync_index_entry()->skip_delete_ = skip_del;
    state_ = next_state;
    // KSyncDelete below will release ksync entry for old handle.
    // Its possible that old ksync-entry has pending request and hence not
    // released immediately. Further processing on flow will happen after
    // current KSync entry is released
    KSyncDelete(manager, flow);
}

void KSyncFlowIndexEntry::AcquireIndex(KSyncFlowIndexManager *manager,
                                       FlowEntry *flow, uint32_t index) {
    manager->AcquireIndex(flow, index);
}

/////////////////////////////////////////////////////////////////////////////
//  INIT : Initial state on allocation and
//         KSync entry is freed with no pending operation on the flow
//  ADD :
//      Assert(index_ == -1)
//      If flow_handle in flow is -1
//          Set state_ to INDEX_UNASSIGN
//          Do KSyncAddChange
//      Else (flow_handle != -1)
//          If index not available
//              Evict flow holding index
//          Acquire index_
//          set state to INDEX_SET
//          Do KSyncAddChange
//  CHANGE, DELETE, EVICT, INDEX_ASSIGN, KSYNC_FREE, VROUTER_ERROR : Assert
/////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexEntry::InitSm(KSyncFlowIndexManager *manager,
                                 FlowEntry *flow, Event event, uint32_t index) {
    // Sanity checks
    assert(index_ == FlowEntry::kInvalidFlowHandle);
    assert(event == ADD || event == CHANGE);
    assert(ksync_entry_ == NULL);

    // Index not assigned. Allocate KSync entry
    if (flow->flow_handle() == FlowEntry::kInvalidFlowHandle) {
        KSyncAddChange(manager, flow);
        state_ = INDEX_UNASSIGNED;
        return;
    }

    AcquireIndex(manager, flow, flow->flow_handle());
    state_ = INDEX_SET;
    KSyncAddChange(manager, flow);
    return;
}

/////////////////////////////////////////////////////////////////////////////
//  INDEX_UNASSIGNED : A reverse flow waiting for VRouter to allocate index
//
//  ADD          :
//      /* Got flow-add from VRouter while waiting for index allocation.
//         Can happen when VRouter traps both forward and reverse flow.
//         flow_handle_ is updated with new value in flow-add. When VRouter
//         responds back, we will delete the index allocated and retain
//         flow_handle from flow-add
//       */
//       Ignore state
//  CHANGE       : Flow changed when waiting for index allocation.
//                 Ignore the change.
//
//  DELETE       : Flow deleted when waiting for index allocation
//                 Ignore the operation. Flow is already marked deleted_, flow
//                 will be deleted on receiving index allocation event
//
//  INDEX_ASSIGN :
//      If flow_handle == -1
//          /* Flow not changed since we asked for index allocation */
//          Acquire index given by vrouter
//          set state to INDEX_SET
//          UpdateKSync handle. There is no need to send VRouter message
//      Else If (flow_handle == index from VRouter)
//          /* flow-add message received from vrouter after index allocation
//             request sent to vrouter. flow_handle_ in flow-add same as value
//             in VRouter response
//           */
//          Acquire index
//          set state to INDEX_SET
//          Do KSyncAdd
//      Else
//          /* flow-add message received from vrouter after index allocation
//             request sent to vrouter. flow_handle_ in flow-add is different
//             than index allocated in VRouter response
//             Retain index given by flow-add since vrouter is holding packet
//             for it
//           */
//          Delete VRouter assigned Index
//          Set state to INDEX_CHANGE
//          flow_handle will be acquired after VRouter index is released
//
//  KSYNC_FREE    :
//      /* Flow can get KSYNC_FREE in INDEX_UNASSIGNED change in following case,
//         - flow-1 is in EVICT state
//         - vrouter sent flow-add for flow-2
//         - flow-1 is reverse flow for flow-2
//         In such case, try allocating new handle for the flow
//  VROUTER_ERROR :
//      If flow_handle == -1
//          Set state to INDEX_FAILED
//      Else 
//          /* flow-add was received from vrouter after index allocation
//             Honor the index given in flow-add
//           */
//          Acquire index
//          set state to INDEX_SET
//          Do KSyncAddChange
/////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexEntry::IndexUnassignedSm(KSyncFlowIndexManager *manager,
                                            FlowEntry *flow, Event event,
                                            uint32_t index) {
    switch (event) {
    case ADD:
        // Flow-handle in flow already modified.
        // Index manager will act on it on receiving VRouter response
        break;

    case CHANGE:
        // FIXME : The changes is not triggered after index is assigned
        // Ignore change.
        // The flow will be updated after index is allocated
        break;

    case DELETE:
        // deleted_ flag is set in flow. It will be acted on receiving
        // VRouter response
        assert(flow->deleted() == true);
        break;

    case INDEX_ASSIGN:
        if (flow->flow_handle() == FlowEntry::kInvalidFlowHandle) {
            flow->set_flow_handle(index);
            AcquireIndex(manager, flow, flow->flow_handle());
            state_ = INDEX_SET;
            KSyncUpdateFlowHandle(manager, flow);
            break;
        }

        // Flow-handle modified before VRouter responded for flow-add request
        // Can happen when there is race between agent trying to add reverse
        // flow and VRouter seeing packet from reverse flow (most likely
        // when agent restarts)
        //
        // We will honour the flow-index in flow-add since VRouter is holding
        // a packet for the flow to complete
        if (index == flow->flow_handle()) {
            AcquireIndex(manager, flow, flow->flow_handle());
            state_ = INDEX_SET;
            KSyncUpdateFlowHandle(manager, flow);
            break;
        }

        // Index from flow-add different than index allocated by vrouter.
        // Must delete the index given by vrouter
        index_ = index;
        AcquireIndex(manager, flow, index_);
        KSyncUpdateFlowHandle(manager, flow);

        // Need to ensure delete message is sent to VRouter
        // We have acquired index above, it ensures evicted_ is false
        // and the flow-delete is generated
        KSyncDelete(manager, flow);
        // Set state to INDEX_CHANGE so that on release of this KSync entry
        // we will try to acquire the new index
        state_ = INDEX_CHANGE;
        break;

    case KSYNC_FREE:
        if (flow->flow_handle() != FlowEntry::kInvalidFlowHandle) {
            state_ = INDEX_SET;
            AcquireIndex(manager, flow, flow->flow_handle());
        } else {
            if (flow->deleted()) {
                state_ = INIT;
                break;
            }
        }
        KSyncAddChange(manager, flow);
        break;

    case VROUTER_ERROR:
        if (flow->flow_handle() == FlowEntry::kInvalidFlowHandle) {
            state_ = INDEX_FAILED;
            break;
        }

        // Flow-handle modified before VRouter responded for flow-add request
        // Can happen when there is race between agent trying to add reverse
        // flow and VRouter seeing packet from reverse flow (most likely
        // when agent restarts)
        AcquireIndex(manager, flow, flow->flow_handle());
        state_ = INDEX_SET;
        KSyncUpdateFlowHandle(manager, flow);
        break;

    default:
        assert(0);
    }
}

/////////////////////////////////////////////////////////////////////////////
//  INDEX_SET : Flow assigned an index
//  ADD :
//      /* flow-add got for existing flow. Flow is evicted and re-added
//         by vrouter. Evict the current flow. After current flow is evicted
//         add new flow */
//      Evict current flow
//      If new flow_handle is -1
//          Set new state as INDEX_UNASSIGNED
//      Else
//          Set new state os INDEX_CHANGE
//      Delete the KSyncEntry
//  CHANGE : Invoke KSync Change
//  DELETE : Invoke KSync Delete
//  EVICT  : Invoke KSync Delete (No KSync message)
//  INDEX_ASSIGN : Assert
//  KSYNC_FREE : Release the index
//  VROUTER_ERROR : Make short-flow
/////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexEntry::IndexSetSm(KSyncFlowIndexManager *manager,
                                     FlowEntry *flow, Event event,
                                     uint32_t index) {
    switch (event) {
    case ADD:
    case CHANGE: {
        bool skip_del = true;
        if (index_ == flow->flow_handle()) {
            if (!delete_in_progress_) {
                KSyncAddChange(manager, flow);
                break;
            }
        } else {
            skip_del = false;
        }

        // Release old index. It will delete current KSync entry. After
        // deleting ksync entry, we need to assign the new index
        if (flow->flow_handle() == FlowEntry::kInvalidFlowHandle)
            EvictFlow(manager, flow, INDEX_UNASSIGNED, skip_del);
        else
            EvictFlow(manager, flow, INDEX_CHANGE, skip_del);
        break;
    }

    case DELETE:
        KSyncDelete(manager, flow);
        break;

    case INDEX_ASSIGN:
        assert(0);
        break;

    case KSYNC_FREE:
        state_ = INIT;
        break;

    case VROUTER_ERROR:
        // Flow already set as Short-Flow. Dont move to INDEX_FAILED state
        break;

    default:
        assert(0);
    }
}

/////////////////////////////////////////////////////////////////////////////
//  INDEX_CHANGE :
//          Index for the flow changed. Delete for old index is already
//          sent and waiting for old KSync entry to be freed.
//          If there are no KSync operation pending for flow, this is
//          only a temporary state and state changes within context of
//          the function.
//          However, if the KSync operation is pending, object can
//          be in this state till KSync operation is complete
//
//      ADD          : Assert
//          /* Agent has not "acquired" flow-handle given by vrouter earlier
//             Another ADD is not expected in this case as it will mean
//             duplicate flow allocation by VRouter
//           */
//      CHANGE       : Ignore
//      DELETE       : Ignore  /* deleted_ flag is already set in flow */
//      INDEX_ASSIGN : Assert
//          /* We had deleted KSync entry and expecting KSYNC_FREE only */
//      KSYNC_FREE   :
//          Old-index is freed in KSync destructor
//          Acquire index_
//          set state to INDEX_SET
//          Do KSyncAddChange
//
//      VROUTER_ERROR: Ignore
/////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexEntry::IndexChangeSm(KSyncFlowIndexManager *manager,
                                        FlowEntry *flow, Event event,
                                        uint32_t index) {
    switch (event) {
    case ADD:
        // FIXME : We are seeing cases of double eviction, that is two-add from
        // vrouter with different indexes. Ignore the event
    case CHANGE:
        if (flow->flow_handle() == FlowEntry::kInvalidFlowHandle) {
            state_ = INDEX_UNASSIGNED;
        }
        break;

    case DELETE:
        break;

    case INDEX_ASSIGN:
        assert(0);
        break;

    case KSYNC_FREE:
        // Old index freed. Acquire the new index
        AcquireIndex(manager, flow, flow->flow_handle());
        state_ = INDEX_SET;
        KSyncAddChange(manager, flow);
        break;

    case VROUTER_ERROR:
        // Flow already set as Short-Flow
        break;

    default:
        assert(0);
    }
}

/////////////////////////////////////////////////////////////////////////////
//  INDEX_EVICT :
//      This flow was evicted due to flow-add got on same index for another
//      flow. The index has already been freed. Waiting for KSync entry to be
//      deleted
//      ADD         :
//          /* Got flow-add for a flow in process of eviction.
//             If flow_handle already allocated, acquire it
//             Else send message to allocate an index
//           */
//          if flow_handle_ not set
//              set state_ = INDEX_UNASSIGNED
//          Else
//              set state_ = INDEX_CHANGE
//              New index will be acquired after KSync is freed
//      CHANGE      : Ignore
//      DELETE      : Invoke KSync Delete
//      INDEX_ASSIGN : Assert(0)
//      VROUTER_ERROR: Ignore
/////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexEntry::IndexEvictSm(KSyncFlowIndexManager *manager,
                                       FlowEntry *flow, Event event,
                                       uint32_t index) {
    // We don't have eviction case as of now
    assert(0);
}

/////////////////////////////////////////////////////////////////////////////
//  INDEX_FAILED :
//         One of the operation had failed and flow is in errored state
//         Flow does not own any index.
//         The only operations allowed are,
//         - flow-add from vrouter
//         - flow delete
//         - ksync-free
//
//      ADD         :
//          Acquire index
//          set state to INDEX_SET
//          Do KSyncAdd
//      CHANGE       : Ignore
//      DELETE       : Invoke KSync Delete
//      KSYNC_FREE   :
//          KSync entry for errored flow is deleted. Move to INIT state
//          to start afresh
//      INDEX_ASSIGN : Assert
//      VROUTER_ERROR: Ignore
/////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexEntry::IndexFailedSm(KSyncFlowIndexManager *manager,
                                        FlowEntry *flow, Event event,
                                        uint32_t index) {
    assert(index_ == FlowEntry::kInvalidFlowHandle);
    switch (event) {
    case ADD:
    case CHANGE:
        if (flow->flow_handle() != FlowEntry::kInvalidFlowHandle) {
            if (delete_in_progress_) {
                state_ = INDEX_CHANGE;
                // add will be trigger on KSYNC_FREE
                break;
            } else {
                // trigger a delete for flow entry with invalid index
                // so that we create a new ksync entry with correct index
                // KSyncDelete will result in KSYNC_FREE inline causing
                // it to move to INDEX_INIT, which will be reset to
                // INDEX_SET in the subsequent path
                KSyncDelete(manager, flow);
                AcquireIndex(manager, flow, flow->flow_handle());
                state_ = INDEX_SET;
            }
            KSyncAddChange(manager, flow);
        } else {
            state_ = INDEX_UNASSIGNED;
            if (!delete_in_progress_) {
                // Delete is not done trigger delete for ksync entry
                // with -1 to allow an add of same entry instead of
                // change
                KSyncDelete(manager, flow);
            }
            // add will be trigger on KSYNC_FREE
        }
        break;

    case DELETE:
        KSyncDelete(manager, flow);
        break;

    case INDEX_ASSIGN:
        assert(0);
        break;

    case KSYNC_FREE:
        state_ = INIT;
        break;

    case VROUTER_ERROR:
        // Flow already set as Short-Flow
        assert(0);
        break;

    default:
        assert(0);
    }
}

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
// KSyncFlowIndexManager State Machine APIs
//////////////////////////////////////////////////////////////////////////////

void KSyncFlowIndexManager::HandleEvent(FlowEntry *flow,
                                        KSyncFlowIndexEntry::Event event,
                                        uint32_t index) {
    flow->ksync_index_entry()->HandleEvent(this, flow, event, index);
}

void KSyncFlowIndexManager::HandleEvent(FlowEntry *flow,
                                        KSyncFlowIndexEntry::Event event) {
    HandleEvent(flow, event, FlowEntry::kInvalidFlowHandle);
}

void KSyncFlowIndexManager::Release(FlowEntry *flow) {
    flow->ksync_index_entry()->index_ = FlowEntry::kInvalidFlowHandle;
    flow->ksync_index_entry()->ksync_entry_ = NULL;
    flow->ksync_index_entry()->delete_in_progress_ = false;
    flow->ksync_index_entry()->skip_delete_ = false;
    flow->ksync_index_entry()->HandleEvent(this, flow,
                                           KSyncFlowIndexEntry::KSYNC_FREE,
                                           FlowEntry::kInvalidFlowHandle);
}

void KSyncFlowIndexManager::Add(FlowEntry *flow) {
    HandleEvent(flow, KSyncFlowIndexEntry::ADD);
}

void KSyncFlowIndexManager::Change(FlowEntry *flow) {
    HandleEvent(flow, KSyncFlowIndexEntry::CHANGE);
}

void KSyncFlowIndexManager::Delete(FlowEntry *flow) {
    HandleEvent(flow, KSyncFlowIndexEntry::DELETE);
}

void KSyncFlowIndexManager::UpdateFlowHandle(FlowEntry *flow, uint32_t index) {
    HandleEvent(flow, KSyncFlowIndexEntry::INDEX_ASSIGN, index);
}

void KSyncFlowIndexManager::UpdateKSyncError(FlowEntry *flow) {
    HandleEvent(flow, KSyncFlowIndexEntry::VROUTER_ERROR);
}

void KSyncFlowIndexManager::KSyncFree(FlowEntry *flow) {
    HandleEvent(flow, KSyncFlowIndexEntry::KSYNC_FREE);
}

//////////////////////////////////////////////////////////////////////////////
// KSyncFlowIndexManager Utility methods
//////////////////////////////////////////////////////////////////////////////
void KSyncFlowIndexManager::AcquireIndex(FlowEntry *flow, uint32_t index) {
    // Sanity check
    assert(index != FlowEntry::kInvalidFlowHandle);

    flow->ksync_index_entry()->index_ = index;
    flow->ksync_index_entry()->skip_delete_ = false;
    return;
}

FlowEntryPtr KSyncFlowIndexManager::FindByIndex(uint32_t idx) {
    if (index_list_[idx].owner_.get() != NULL)
        return index_list_[idx].owner_;
    return FlowEntryPtr(NULL);
}

