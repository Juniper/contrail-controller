/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <bitset>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/unordered_map.hpp>
#include <sandesh/sandesh_trace.h>
#include <net/address_util.h>
#include <pkt/flow_table.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/ksync/ksync_flow_index_manager.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <base/os.h>

#include <route/route.h>
#include <cmn/agent_cmn.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>

#include <init/agent_param.h>
#include <cmn/agent_cmn.h>
#include <cmn/agent_stats.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/vm.h>
#include <oper/sg.h>

#include <filter/packet_header.h>
#include <filter/acl.h>

#include <pkt/proto.h>
#include <pkt/proto_handler.h>
#include <pkt/pkt_handler.h>
#include <pkt/flow_proto.h>
#include <pkt/pkt_types.h>
#include <pkt/pkt_sandesh_flow.h>
#include <pkt/flow_mgmt.h>
#include <pkt/flow_event.h>

const uint32_t FlowEntryFreeList::kInitCount;
const uint32_t FlowEntryFreeList::kTestInitCount;
const uint32_t FlowEntryFreeList::kGrowSize;
const uint32_t FlowEntryFreeList::kMinThreshold;

SandeshTraceBufferPtr FlowTraceBuf(SandeshTraceBufferCreate("Flow", 5000));

/////////////////////////////////////////////////////////////////////////////
// FlowTable constructor/destructor
/////////////////////////////////////////////////////////////////////////////
FlowTable::FlowTable(Agent *agent, uint16_t table_index) :
    agent_(agent),
    rand_gen_(boost::uuids::random_generator()),
    table_index_(table_index),
    ksync_object_(NULL),
    flow_entry_map_(),
    free_list_(this),
    flow_task_id_(0),
    flow_update_task_id_(0),
    flow_delete_task_id_(0),
    flow_ksync_task_id_(0),
    flow_logging_task_id_(0) {
}

FlowTable::~FlowTable() {
    assert(flow_entry_map_.size() == 0);
}

void FlowTable::Init() {
    flow_task_id_ = agent_->task_scheduler()->GetTaskId(kTaskFlowEvent);
    flow_update_task_id_ = agent_->task_scheduler()->GetTaskId(kTaskFlowUpdate);
    flow_delete_task_id_ = agent_->task_scheduler()->GetTaskId(kTaskFlowDelete);
    flow_ksync_task_id_ = agent_->task_scheduler()->GetTaskId(kTaskFlowKSync);
    flow_logging_task_id_ = agent_->task_scheduler()->GetTaskId(kTaskFlowLogging);
    FlowEntry::Init();
    return;
}

void FlowTable::InitDone() {
}

void FlowTable::Shutdown() {
}

// Concurrency check to ensure all flow-table and free-list manipulations
// are done from FlowEvent task context only
//exception: freelist free function can be accessed by flow logging task
bool FlowTable::ConcurrencyCheck(int task_id, bool check_task_instance) {
    Task *current = Task::Running();
    // test code invokes FlowTable API from main thread. The running task
    // will be NULL in such cases
    if (current == NULL) {
        return true;
    }

    if (current->GetTaskId() != task_id)
        return false;
    if (check_task_instance) {
	if (current->GetTaskInstance() != table_index_)
	   return false;
    }
    return true;
}

bool FlowTable::ConcurrencyCheck(int task_id) {
	return ConcurrencyCheck(task_id, true);
}

/////////////////////////////////////////////////////////////////////////////
// FlowTable Add/Delete routines
/////////////////////////////////////////////////////////////////////////////

// When multiple lock are taken, there is possibility of deadlocks. We do
// deadlock avoidance by ensuring "consistent ordering of locks"
void FlowTable::GetMutexSeq(tbb::mutex &mutex1, tbb::mutex &mutex2,
                            tbb::mutex **mutex_ptr_1,
                            tbb::mutex **mutex_ptr_2) {
    *mutex_ptr_1 = NULL;
    *mutex_ptr_2 = NULL;
    if (&mutex1 < &mutex2) {
        *mutex_ptr_1 = &mutex1;
        *mutex_ptr_2 = &mutex2;
    } else {
        *mutex_ptr_1 = &mutex2;
        *mutex_ptr_2 = &mutex1;
    }
}

FlowEntry *FlowTable::Find(const FlowKey &key) {
    assert(ConcurrencyCheck(flow_task_id_) == true);
    FlowEntryMap::iterator it;

    it = flow_entry_map_.find(key);
    if (it != flow_entry_map_.end()) {
        return it->second;
    } else {
        return NULL;
    }
}

void FlowTable::Copy(FlowEntry *lhs, FlowEntry *rhs, bool update) {
    /* Flow copy, if results in UUID change, stop updating UVE stats
     * for old flow
     */
    if (update==false)
        DeleteFlowUveInfo(lhs);

    RevFlowDepParams params;
    lhs->RevFlowDepInfo(&params);
    DeleteFlowInfo(lhs, params);
    if (rhs)
        lhs->Copy(rhs, update);
}

FlowEntry *FlowTable::Locate(FlowEntry *flow, uint64_t time) {
    assert(ConcurrencyCheck(flow_task_id_) == true);
    std::pair<FlowEntryMap::iterator, bool> ret;
    ret = flow_entry_map_.insert(FlowEntryMapPair(flow->key(), flow));
    if (ret.second == true) {
        agent_->stats()->incr_flow_created();
        ret.first->second->set_on_tree();
        return flow;
    }

    return ret.first->second;
}

void FlowTable::Add(FlowEntry *flow, FlowEntry *rflow) {
    uint64_t time = UTCTimestampUsec();
    FlowEntry *new_flow = Locate(flow, time);
    FlowEntry *new_rflow = (rflow != NULL) ? Locate(rflow, time) : NULL;

    FLOW_LOCK(new_flow, new_rflow, FlowEvent::FLOW_MESSAGE);
    AddInternal(flow, new_flow, rflow, new_rflow, false, false);
}

void FlowTable::Update(FlowEntry *flow, FlowEntry *rflow) {
    bool fwd_flow_update = true;
    FlowEntry *new_flow = Find(flow->key());

    FlowEntry *new_rflow = (rflow != NULL) ? Find(rflow->key()) : NULL;
    bool rev_flow_update = true;
    if (rflow && new_rflow == NULL) {
        uint64_t time = UTCTimestampUsec();
        new_rflow = Locate(rflow, time);
        rev_flow_update = false;
    }

    FLOW_LOCK(new_flow, new_rflow, FlowEvent::FLOW_MESSAGE);
    AddInternal(flow, new_flow, rflow, new_rflow, fwd_flow_update,
                rev_flow_update);
}

void FlowTable::AddInternal(FlowEntry *flow_req, FlowEntry *flow,
                            FlowEntry *rflow_req, FlowEntry *rflow,
                            bool fwd_flow_update, bool rev_flow_update) {
    // Set trace flags for a flow
    bool trace = agent_->pkt()->get_flow_proto()->ShouldTrace(flow, rflow);
    if (flow)
        flow->set_trace(trace);
    if (rflow)
        rflow->set_trace(trace);

    // The forward and reverse flow in request are linked. Unlink the flows
    // first. Flow table processing will link them if necessary
    flow_req->set_reverse_flow_entry(NULL);
    if (rflow_req)
        rflow_req->set_reverse_flow_entry(NULL);

    bool force_update_rflow = false;
    if (fwd_flow_update) {
        if (flow == NULL)
            return;

        if (flow->deleted() || flow->IsShortFlow()) {
            return;
        }
    }

    if (flow_req != flow) {
        if (flow->flow_handle() == FlowEntry::kInvalidFlowHandle &&
            !flow->deleted()) {
            // In this scenario packet trap for forward flow should
            // not cause eviction of the reverse flow due to add event
            // so trigger a force update instead of add for reverse flow
            force_update_rflow = true;
        }
        Copy(flow, flow_req, fwd_flow_update);
        flow->set_deleted(false);
        // this flow entry is reused , increment the transaction id
        // so that flow events with  old transaction id will be ingnored
        flow->IncrementTransactionId();
    }

    if (rflow) {
        if (rflow_req != rflow) {
            Copy(rflow, rflow_req, (rev_flow_update || force_update_rflow));
            // if the reverse flow was marked delete, reset its flow handle
            // to invalid index to assure it is attempted to reprogram using
            // kInvalidFlowHandle, this also ensures that flow entry wont
            // give fake notion of being available in the flow index tree
            // delete for which has already happend while triggering delete
            // for flow entry
            if (rflow->deleted()) {
                rflow->flow_handle_ = FlowEntry::kInvalidFlowHandle;
                // rflow was delete marked skip force update
                force_update_rflow = false;
            }
            rflow->set_deleted(false);
            rflow->IncrementTransactionId();
        } else {
            // we are creating a new reverse flow, so avoid triggering
            // force update in this case
            force_update_rflow = false;
        }
    }

    if (flow) {
        if (fwd_flow_update) {
            flow->set_last_event(FlowEvent::FLOW_MESSAGE);
        } else {
            flow->set_last_event(FlowEvent::VROUTER_FLOW_MSG);
        }
    }
    if (rflow) {
        if (rev_flow_update) {
            rflow->set_last_event(FlowEvent::FLOW_MESSAGE);
        } else {
            rflow->set_last_event(FlowEvent::VROUTER_FLOW_MSG);
        }
    }

    // If the flows are already present, we want to retain the Forward and
    // Reverse flow characteristics for flow.
    // We have following conditions,
    // flow has ReverseFlow set, rflow has ReverseFlow reset
    //      Swap flow and rflow
    // flow has ReverseFlow set, rflow has ReverseFlow set
    //      Unexpected case. Continue with flow as forward flow
    // flow has ReverseFlow reset, rflow has ReverseFlow reset
    //      Unexpected case. Continue with flow as forward flow
    // flow has ReverseFlow reset, rflow has ReverseFlow set
    //      No change in forward/reverse flow. Continue as forward-flow
    if (flow->is_flags_set(FlowEntry::ReverseFlow) &&
        rflow && !rflow->is_flags_set(FlowEntry::ReverseFlow)) {
        FlowEntry *tmp = flow;
        flow = rflow;
        rflow = tmp;
    }

    UpdateReverseFlow(flow, rflow);

    // Add the forward flow after adding the reverse flow first to avoid 
    // following sequence
    // 1. Agent adds forward flow
    // 2. vrouter releases the packet
    // 3. Packet reaches destination VM and destination VM replies
    // 4. Agent tries adding reverse flow. vrouter processes request in core-0
    // 5. vrouter gets reverse packet in core-1
    // 6. If (4) and (3) happen together, vrouter can allocate 2 hash entries
    //    for the flow.
    //
    // While the scenario above cannot be totally avoided, programming reverse
    // flow first will reduce the probability
    if (rflow) {
        UpdateKSync(rflow, (rev_flow_update || force_update_rflow));
        AddFlowInfo(rflow);
    }

    UpdateKSync(flow, fwd_flow_update);
    AddFlowInfo(flow);
}

void FlowTable::DeleteInternal(FlowEntry *fe, uint64_t time,
                               const RevFlowDepParams &params) {
    fe->set_deleted(true);

    // Unlink the reverse flow, if one exists
    FlowEntry *rflow = fe->reverse_flow_entry();
    if (rflow) {
        rflow->set_reverse_flow_entry(NULL);
    }
    fe->set_reverse_flow_entry(NULL);

    DeleteFlowInfo(fe, params);
    DeleteKSync(fe);

    agent_->stats()->incr_flow_aged();
}

bool FlowTable::DeleteFlows(FlowEntry *flow, FlowEntry *rflow) {
    uint64_t time = UTCTimestampUsec();

    /* Fetch reverse-flow info for both flows before their reverse-flow
     * links are broken. This info is required during FlowExport
     *
     * DeleteFlows() is invoked for both forward and reverse flows. So, get
     * reverse-flow info only when flows are not deleted
     */
    RevFlowDepParams r_params;
    if (rflow && rflow->deleted() == false) {
        rflow->RevFlowDepInfo(&r_params);
    }
    if (flow && flow->deleted() == false) {
        RevFlowDepParams f_params;
        flow->RevFlowDepInfo(&f_params);
        /* Delete the forward flow */
        DeleteInternal(flow, time, f_params);
    }

    if (rflow && rflow->deleted() == false) {
        DeleteInternal(rflow, time, r_params);
    }
    return true;
}

void FlowTable::PopulateFlowEntriesUsingKey(const FlowKey &key,
                                            bool reverse_flow,
                                            FlowEntry** flow,
                                            FlowEntry** rflow) {
    *flow = Find(key);
    *rflow = NULL;

    //No flow entry, nothing to populate
    if (!(*flow)) {
        return;
    }

    //No reverse flow requested, so dont populate rflow
    if (!reverse_flow) {
        return;
    }

    FlowEntry *reverse_flow_entry = (*flow)->reverse_flow_entry();
    if (reverse_flow_entry) {
        *rflow = Find(reverse_flow_entry->key());
    }
}

//Caller makes sure lock is taken on flow.
bool FlowTable::DeleteUnLocked(bool del_reverse_flow,
                               FlowEntry *flow,
                               FlowEntry *rflow) {
    if (!flow) {
        return false;
    }

    DeleteFlows(flow, rflow);

    //If deletion of reverse flow is to be done,
    //make sure that rflow is populated if flow has a reverse flow pointer.
    //In case rflow is not located with the reverse flow key, consider it as
    //failure.
    if (del_reverse_flow && flow->reverse_flow_entry() && !rflow) {
        return false;
    }
    return true;
}

//Caller has to ensure lock is taken for flow.
bool FlowTable::DeleteUnLocked(const FlowKey &key, bool del_reverse_flow) {
    FlowEntry *flow = NULL;
    FlowEntry *rflow = NULL;

    PopulateFlowEntriesUsingKey(key, del_reverse_flow, &flow, &rflow);
    return DeleteUnLocked(del_reverse_flow, flow, rflow);
}

bool FlowTable::Delete(const FlowKey &key, bool del_reverse_flow) {
    FlowEntry *flow = NULL;
    FlowEntry *rflow = NULL;

    PopulateFlowEntriesUsingKey(key, del_reverse_flow, &flow, &rflow);
    FLOW_LOCK(flow, rflow, FlowEvent::DELETE_FLOW);
    return DeleteUnLocked(del_reverse_flow, flow, rflow);
}

void FlowTable::DeleteAll() {
    FlowEntryMap::iterator it;

    it = flow_entry_map_.begin();
    while (it != flow_entry_map_.end()) {
        FlowEntry *entry = it->second;
        FlowEntry *reverse_entry = NULL;
        ++it;
        if (it != flow_entry_map_.end() &&
            it->second == entry->reverse_flow_entry()) {
            reverse_entry = it->second;
            ++it;
        }
        FLOW_LOCK(entry, reverse_entry, FlowEvent::DELETE_FLOW);
        DeleteUnLocked(true, entry, reverse_entry);
    }
}

void FlowTable::UpdateReverseFlow(FlowEntry *flow, FlowEntry *rflow) {
    FlowEntry *flow_rev = flow->reverse_flow_entry();
    FlowEntry *rflow_rev = NULL;

    if (rflow) {
        rflow_rev = rflow->reverse_flow_entry();
    }

    if (rflow_rev) {
        assert(rflow_rev->reverse_flow_entry() == rflow);
        rflow_rev->set_reverse_flow_entry(NULL);
    }

    if (flow_rev) {
        flow_rev->set_reverse_flow_entry(NULL);
    }

    flow->set_reverse_flow_entry(rflow);
    if (rflow) {
        rflow->set_reverse_flow_entry(flow);
    }

    if (flow_rev && (flow_rev->reverse_flow_entry() == NULL)) {
        flow_rev->MakeShortFlow(FlowEntry::SHORT_NO_REVERSE_FLOW);
    }

    if (rflow_rev && (rflow_rev->reverse_flow_entry() == NULL)) {
        rflow_rev->MakeShortFlow(FlowEntry::SHORT_NO_REVERSE_FLOW);
    }

    if (flow->reverse_flow_entry() == NULL) {
        flow->MakeShortFlow(FlowEntry::SHORT_NO_REVERSE_FLOW);
    }

    if (rflow && rflow->reverse_flow_entry() == NULL) {
        rflow->MakeShortFlow(FlowEntry::SHORT_NO_REVERSE_FLOW);
    }

    if (rflow) {
        if (flow->is_flags_set(FlowEntry::ShortFlow) ||
            rflow->is_flags_set(FlowEntry::ShortFlow)) {
            flow->MakeShortFlow(FlowEntry::SHORT_REVERSE_FLOW_CHANGE);
        }
        if (flow->is_flags_set(FlowEntry::Multicast)) {
            rflow->set_flags(FlowEntry::Multicast);
        }
    }
}

void FlowTable::DeleteFlowUveInfo(FlowEntry *fe) {
    agent_->pkt()->flow_mgmt_manager(table_index_)->EnqueueUveDeleteEvent(fe);
}

////////////////////////////////////////////////////////////////////////////
// Flow Info tree management
////////////////////////////////////////////////////////////////////////////
void FlowTable::AddFlowInfo(FlowEntry *fe) {
    agent_->pkt()->flow_mgmt_manager(table_index_)->AddEvent(fe);
}

void FlowTable::DeleteFlowInfo(FlowEntry *fe, const RevFlowDepParams &params) {
    agent_->pkt()->flow_mgmt_manager(table_index_)->DeleteEvent(fe, params);
}

/////////////////////////////////////////////////////////////////////////////
// Flow revluation routines. Processing will vary based on DBEntry type
/////////////////////////////////////////////////////////////////////////////
boost::uuids::uuid FlowTable::rand_gen() {
    return rand_gen_();
}

// Enqueue message to recompute a flow
void FlowTable::RecomputeFlow(FlowEntry *flow) {
    if (flow->is_flags_set(FlowEntry::ShortFlow))
        return;

    // If this is reverse flow, enqueue the corresponding forward flow
    if (flow->is_flags_set(FlowEntry::ReverseFlow)) {
        flow = flow->reverse_flow_entry();
    }

    if (flow != NULL)
        agent_->pkt()->get_flow_proto()->MessageRequest(flow);
}

// Handle deletion of a Route. Flow management module has identified that route
// must be deleted
void FlowTable::DeleteMessage(FlowEntry *flow) {
    DeleteUnLocked(true, flow, flow->reverse_flow_entry());
}

void FlowTable::EvictFlow(FlowEntry *flow, FlowEntry *reverse_flow,
                          uint32_t evict_gen_id) {
    DisableKSyncSend(flow, evict_gen_id);
    DeleteUnLocked(false, flow, NULL);

    // Reverse flow unlinked with forward flow. Make it short-flow
    // Dont update ksync, it will shortly get either evicted or deleted by
    // ageing process
    if (reverse_flow)
        reverse_flow->MakeShortFlow(FlowEntry::SHORT_NO_REVERSE_FLOW);
}

void FlowTable::HandleRevaluateDBEntry(const DBEntry *entry, FlowEntry *flow,
                                       bool active_flow, bool deleted_flow) {
    // Ignore revluate of deleted/short flows
    if (flow->IsShortFlow())
        return;

    if (flow->deleted())
        return;

    FlowEntry *rflow = flow->reverse_flow_entry();
    // Update may happen for reverse-flow. We act on both forward and
    // reverse-flow. Get both forward and reverse flows
    if (flow->is_flags_set(FlowEntry::ReverseFlow)) {
        FlowEntry *tmp = flow;
        flow = rflow;
        rflow = tmp;
    }

    // We want to update only if both forward and reverse flow are valid
    if (flow == NULL || rflow == NULL)
        return;

    // Ignore update, if any of the DBEntries referred is deleted
    if (flow->vn_entry() && flow->vn_entry()->IsDeleted())
        return;

    if (flow->rpf_nh() && flow->rpf_nh()->IsDeleted())
        return;

    if (flow->intf_entry() && flow->intf_entry()->IsDeleted())
        return;

    // Revaluate flood unknown-unicast flag. If flow has UnknownUnicastFlood and
    // VN doesnt allow it, make Short Flow
    if (flow->vn_entry() &&
        flow->vn_entry()->flood_unknown_unicast() == false &&
        flow->is_flags_set(FlowEntry::UnknownUnicastFlood)) {
        flow->MakeShortFlow(FlowEntry::SHORT_NO_DST_ROUTE);
    }

    flow->UpdateL2RouteInfo();
    rflow->UpdateL2RouteInfo();

    // Get policy attributes again and redo the flows
    flow->GetPolicyInfo();
    rflow->GetPolicyInfo();

    // Resync reverse flow first and then forward flow
    // as forward flow resync will try to update reverse flow
    rflow->ResyncFlow();
    flow->ResyncFlow();

    // RPF computation can be done only after policy processing.
    // Do RPF computation now
    flow->RpfUpdate();
    rflow->RpfUpdate();

    // the SG action could potentially have changed
    // due to reflexive nature. Update KSync for reverse flow first
    UpdateKSync(rflow, true);
    UpdateKSync(flow, true);

    // Update flow-mgmt with new values
    AddFlowInfo(flow);
    AddFlowInfo(rflow);
    return;
}

void FlowTable::HandleKSyncError(FlowEntry *flow,
                                 FlowTableKSyncEntry *ksync_entry,
                                 int ksync_error, uint32_t flow_handle,
                                 uint32_t gen_id) {
    // flow not associated with ksync anymore. Ignore the message
    if (flow == NULL || flow != ksync_entry->flow_entry()) {
        return;
    }

    // VRouter can return EBADF and ENONENT error if flow-handle changed before
    // getting KSync response back. Avoid making short-flow in such case
    if ((ksync_error == EBADF || ksync_error == ENOENT)) {
        if (flow->flow_handle() != flow_handle || flow->gen_id() != gen_id) {
            return;
        }
    }

    // If VRouter returns error, mark the flow entry as short flow and
    // update ksync error event to ksync index manager
    //
    // For EEXIST error donot mark the flow as ShortFlow since Vrouter
    // generates EEXIST only for cases where another add should be
    // coming from the pkt trap from Vrouter
    if (ksync_error != EEXIST ||
          (flow->is_flags_set(FlowEntry::NatFlow) &&
           !(flow->is_flags_set(FlowEntry::BgpRouterService)))) {
        // FIXME : We dont have good scheme to handle following scenario,
        // - VM1 in VN1 has floating-ip FIP1 in VN2
        // - VM2 in VN2
        // - VM1 pings VM2 (using floating-ip)
        // The forward and reverse flows go to different partitions.
        //
        // If packets for both forward and reverse flows are trapped together
        // we try to setup following flows from different partitions,
        // FlowPair-1
        //    - VM1 to VM2
        //    - VM2 to FIP1
        // FlowPair-2
        //    - VM2 to FIP1
        //    - VM1 to VM2
        //
        // The reverse flows for both FlowPair-1 and FlowPair-2 are not
        // installed due to EEXIST error. We are converting flows to
        // short-flow till this case is handled properly
        flow->MakeShortFlow(FlowEntry::SHORT_FAILED_VROUTER_INSTALL);
    }
    return;
}

/////////////////////////////////////////////////////////////////////////////
// KSync Routines
/////////////////////////////////////////////////////////////////////////////
void FlowTable::DeleteKSync(FlowEntry *flow) {
    KSyncFlowIndexManager *mgr = agent()->ksync()->ksync_flow_index_manager();
    mgr->Delete(flow);
}

void FlowTable::UpdateKSync(FlowEntry *flow, bool update) {
    KSyncFlowIndexManager *mgr = agent()->ksync()->ksync_flow_index_manager();
    if (flow->deleted()) {
        // ignore update on a deleted flow
        // flow should already be non deleted of an Add case
        assert(update == false);
        return;
    }
    mgr->Update(flow);
}

void FlowTable::DisableKSyncSend(FlowEntry *flow, uint32_t evict_gen_id) {
    KSyncFlowIndexManager *mgr = agent()->ksync()->ksync_flow_index_manager();
    mgr->DisableSend(flow, evict_gen_id);
}

/////////////////////////////////////////////////////////////////////////////
// Link local flow information tree
/////////////////////////////////////////////////////////////////////////////
void FlowTable::AddLinkLocalFlowInfo(int fd, uint32_t index, const FlowKey &key,
                                     const uint64_t timestamp) {
    tbb::mutex::scoped_lock mutext(mutex_);
    LinkLocalFlowInfoMap::iterator it = linklocal_flow_info_map_.find(fd);
    if (it == linklocal_flow_info_map_.end()) {
        linklocal_flow_info_map_.insert(
          LinkLocalFlowInfoPair(fd, LinkLocalFlowInfo(index, key, timestamp)));
    } else {
        it->second.flow_index = index;
        it->second.flow_key = key;
    }
}

void FlowTable::DelLinkLocalFlowInfo(int fd) {
    tbb::mutex::scoped_lock mutext(mutex_);
    linklocal_flow_info_map_.erase(fd);
}

/////////////////////////////////////////////////////////////////////////////
// Event handler routines
/////////////////////////////////////////////////////////////////////////////

// KSync flow event handler. Handles response for both vr_flow message only
void FlowTable::ProcessKSyncFlowEvent(const FlowEventKSync *req,
                                      FlowEntry *flow) {
    FlowTableKSyncEntry *ksync_entry =
        (static_cast<FlowTableKSyncEntry *> (req->ksync_entry()));
    KSyncFlowIndexManager *imgr = agent()->ksync()->ksync_flow_index_manager();

    // flow not associated with ksync anymore. Ignore the message
    if (flow == NULL) {
        return;
    }

    // Ignore error for Delete messages
    if (req->ksync_event() == KSyncEntry::DEL_ACK) {
        return;
    }

    // if transaction id is not same, then ignore the old
    // vrouter add-ack response. this is possible that
    // after vrouter add-ack response, flows will be evicted
    // and new wflow with same flow tuple as evicted one will
    // trigger new flow request to agent. this old add-ack response
    // has no relevance, so should be ignored.
    if (req->transaction_id() != flow->GetTransactionId()) {
        return;
    }
    if (req->ksync_error() != 0) {
        // Handle KSync Errors
        HandleKSyncError(flow, ksync_entry, req->ksync_error(),
                         req->flow_handle(), req->gen_id());
    } else {
        // Operation succeeded. Update flow-handle if not assigned
        KSyncFlowIndexManager *mgr =
            agent()->ksync()->ksync_flow_index_manager();
        mgr->UpdateFlowHandle(ksync_entry, req->flow_handle(),
                              req->gen_id());
    }

    // Log message if flow-handle change
    if (flow->flow_handle() != FlowEntry::kInvalidFlowHandle) {
        if (flow->flow_handle() != req->flow_handle()) {
            LOG(DEBUG, "Flow index changed from <"
                << flow->flow_handle() << "> to <"
                << req->flow_handle() << ">");
        }
    }

    // When vrouter allocates a flow-index or changes flow-handle, its
    // possible that a flow in vrouter is evicted. Update stats for
    // evicted flow
    if (req->flow_handle() != FlowEntry::kInvalidFlowHandle &&
        req->flow_handle() != flow->flow_handle()) {
        FlowEntryPtr evicted_flow = imgr->FindByIndex(req->flow_handle());
        if (evicted_flow.get() && evicted_flow->deleted() == false) {
            FlowMgmtManager *mgr = agent()->pkt()->flow_mgmt_manager(table_index_);
            mgr->FlowStatsUpdateEvent(evicted_flow.get(),
                                      req->evict_flow_bytes(),
                                      req->evict_flow_packets(),
                                      req->evict_flow_oflow(),
                                      evicted_flow->uuid());
        }
    }

    return;
}

bool FlowTable::ProcessFlowEvent(const FlowEvent *req, FlowEntry *flow,
                                 FlowEntry *rflow) {
    //Take lock
    FLOW_LOCK(flow, rflow, req->event());

    if (flow)
        flow->set_last_event(req->event());
    if (rflow)
        rflow->set_last_event(req->event());
    bool active_flow = true;
    bool deleted_flow = flow->deleted();
    if (deleted_flow || flow->is_flags_set(FlowEntry::ShortFlow))
        active_flow = false;

    //Now process events.
    switch (req->event()) {
    case FlowEvent::DELETE_FLOW: {
        //In case of continous stream of short lived TCP flows with same 5 tuple,
        //flow eviction logic might cause below set of event
        //1> F1 and R1 flow are added to flow table
        //2> R1 is written to vrouter
        //3> F1 is written to vrouter
        //4> R1 flow add response is received, triggering update of
        //   F1(not needed now as reverse flow index is not written to kernel?)
        //5> In the meantime flow is evicted in vrouter, hence flow update for F1
        //   would result in error from vrouter resulting in short flow
        //6> Since F1 is shortflow Flow delete gets enqueued
        //7> Since R1 is evict marked, flow evict gets enqueued
        //8> Both event F1 and R1 delete and evict event can run in parallel,
        //   and hence reverse flow pointer obtained before FLOW lock could
        //   be invalid, hence read back the same
        rflow = flow->reverse_flow_entry();
        DeleteUnLocked(true, flow, rflow);
        break;
    }

    case FlowEvent::DELETE_DBENTRY: {
        DeleteMessage(flow);
        break;
    }

    case FlowEvent::REVALUATE_DBENTRY: {
        const DBEntry *entry = req->db_entry();
        HandleRevaluateDBEntry(entry, flow, active_flow, deleted_flow);
        break;
    }

    case FlowEvent::RECOMPUTE_FLOW: {
        if (active_flow)
            RecomputeFlow(flow);
        break;
    }

    // Check if flow-handle changed. This can happen if vrouter tries to
    // setup the flow which was evicted earlier
    case FlowEvent::EVICT_FLOW: {
        if (flow->flow_handle() != req->flow_handle() ||
            flow->gen_id() != req->gen_id())
            break;
        EvictFlow(flow, rflow, req->evict_gen_id());
        break;
    }

    case FlowEvent::KSYNC_EVENT: {
        const FlowEventKSync *ksync_event =
            static_cast<const FlowEventKSync *>(req);
        // Handle vr_flow message
        ProcessKSyncFlowEvent(ksync_event, flow);
        // Handle vr_response message
        // Trigger the ksync flow event to move ksync state-machine
        KSyncFlowIndexManager *imgr =
            agent()->ksync()->ksync_flow_index_manager();
        FlowTableKSyncEntry *ksync_entry = static_cast<FlowTableKSyncEntry *>
            (ksync_event->ksync_entry());
        imgr->TriggerKSyncEvent(ksync_entry, ksync_event->ksync_event());
        break;
    }

    case FlowEvent::UNRESOLVED_FLOW_ENTRY: {
        if (flow->deleted()) {
            break;
        }

        if (flow->GetMaxRetryAttempts() < FlowEntry::kFlowRetryAttempts) {
            flow->IncrementRetrycount();
        } else {
            flow->MakeShortFlow(FlowEntry::SHORT_NO_MIRROR_ENTRY);
            flow->ResetRetryCount();
        } 

        UpdateKSync(flow, true);
        break;
    }

    default: {
        assert(0);
        break;
    }
    }
    return true;
}

void FlowTable::GetFlowSandeshActionParams(const FlowAction &action_info,
                                           std::string &action_str) {
    std::bitset<32> bs(action_info.action);
    for (unsigned int i = 0; i <= bs.size(); i++) {
        if (bs[i]) {
            if (!action_str.empty()) {
                action_str += "|";
            }
            action_str += TrafficAction::ActionToString(
                static_cast<TrafficAction::Action>(i));
        }
    }
}
/////////////////////////////////////////////////////////////////////////////
// FlowEntryFreeList implementation
/////////////////////////////////////////////////////////////////////////////
void FlowTable::GrowFreeList() {
    free_list_.Grow();
    ksync_object_->GrowFreeList();
}

FlowEntryFreeList::FlowEntryFreeList(FlowTable *table) :
    table_(table), max_count_(0), grow_pending_(false), total_alloc_(0),
    total_free_(0), free_list_(), grow_count_(0) {
    uint32_t count = kInitCount;
    if (table->agent()->test_mode()) {
        count = kTestInitCount;
    }

    while (max_count_ < count) {
        free_list_.push_back(*new FlowEntry(table));
        max_count_++;
    }
}

FlowEntryFreeList::~FlowEntryFreeList() {
    while (free_list_.empty() == false) {
        FreeList::iterator it = free_list_.begin();
        FlowEntry *flow = &(*it);
        free_list_.erase(it);
        delete flow;
    }
}

// Allocate a chunk of FlowEntries
void FlowEntryFreeList::Grow() {
    assert(table_->ConcurrencyCheck(table_->flow_task_id()) == true);
    grow_pending_ = false;
    if (free_list_.size() >= kMinThreshold)
        return;

    grow_count_++;
    for (uint32_t i = 0; i < kGrowSize; i++) {
        free_list_.push_back(*new FlowEntry(table_));
        max_count_++;
    }
}

FlowEntry *FlowEntryFreeList::Allocate(const FlowKey &key) {
    assert(table_->ConcurrencyCheck(table_->flow_task_id()) == true);
    FlowEntry *flow = NULL;
    if (free_list_.size() == 0) {
        flow = new FlowEntry(table_);
        max_count_++;
    } else {
        FreeList::iterator it = free_list_.begin();
        flow = &(*it);
        free_list_.erase(it);
    }

    if (grow_pending_ == false && free_list_.size() < kMinThreshold) {
        grow_pending_ = true;
        FlowProto *proto = table_->agent()->pkt()->get_flow_proto();
        proto->GrowFreeListRequest(table_);
    }
    flow->Reset(key);
    total_alloc_++;
    return flow;
}

void FlowEntryFreeList::Free(FlowEntry *flow) {
   // only flow logging task and flow event task can  free up the flow entry ,
   assert((table_->ConcurrencyCheck(table_->flow_task_id()) == true) ||
	(table_->ConcurrencyCheck(
            table_->flow_logging_task_id(), false) == true));
    total_free_++;
    flow->Reset();
    free_list_.push_back(*flow);
    assert(flow->flow_mgmt_info() == NULL);
    // TODO : Free entry if beyound threshold
}
