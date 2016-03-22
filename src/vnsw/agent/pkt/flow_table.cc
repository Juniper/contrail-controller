/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <bitset>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/unordered_map.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
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

#define FLOW_LOCK(flow, rflow) \
    tbb::mutex tmp_mutex1, tmp_mutex2, *mutex_ptr_1, *mutex_ptr_2; \
    GetMutexSeq(flow ? flow->mutex() : tmp_mutex1, \
                rflow ? rflow->mutex() : tmp_mutex2, \
                &mutex_ptr_1, &mutex_ptr_2); \
    tbb::mutex::scoped_lock lock1(*mutex_ptr_1); \
    tbb::mutex::scoped_lock lock2(*mutex_ptr_2);

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
    flow_task_id_(0) {
}

FlowTable::~FlowTable() {
    assert(flow_entry_map_.size() == 0);
}

void FlowTable::Init() {
    flow_task_id_ = agent_->task_scheduler()->GetTaskId(kTaskFlowEvent);
    FlowEntry::Init();
    return;
}

void FlowTable::InitDone() {
}

void FlowTable::Shutdown() {
}

// Concurrency check to ensure all flow-table and free-list manipulations
// are done from FlowEvent task context only
bool FlowTable::ConcurrencyCheck() {
    Task *current = Task::Running();
    // test code invokes FlowTable API from main thread. The running task
    // will be NULL in such cases
    if (current == NULL) {
        return true;
    }
    if (current->GetTaskId() != flow_task_id_)
        return false;
    if (current->GetTaskId() != flow_task_id_)
        return false;
    return true;
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
    assert(ConcurrencyCheck() == true);
    FlowEntryMap::iterator it;

    it = flow_entry_map_.find(key);
    if (it != flow_entry_map_.end()) {
        return it->second;
    } else {
        return NULL;
    }
}

void FlowTable::Copy(FlowEntry *lhs, FlowEntry *rhs, bool update) {
    DeleteFlowInfo(lhs);
    if (rhs)
        lhs->Copy(rhs, update);
}

FlowEntry *FlowTable::Locate(FlowEntry *flow, uint64_t time) {
    assert(ConcurrencyCheck() == true);
    std::pair<FlowEntryMap::iterator, bool> ret;
    ret = flow_entry_map_.insert(FlowEntryMapPair(flow->key(), flow));
    if (ret.second == true) {
        agent_->stats()->incr_flow_created();
        agent_->stats()->UpdateFlowAddMinMaxStats(time);
        ret.first->second->set_on_tree();
        return flow;
    }

    return ret.first->second;
}

void FlowTable::Add(FlowEntry *flow, FlowEntry *rflow) {
    uint64_t time = UTCTimestampUsec();
    FlowEntry *new_flow = Locate(flow, time);
    FlowEntry *new_rflow = (rflow != NULL) ? Locate(rflow, time) : NULL;

    FLOW_LOCK(new_flow, new_rflow);
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

    FLOW_LOCK(new_flow, new_rflow);
    AddInternal(flow, new_flow, rflow, new_rflow, fwd_flow_update,
                rev_flow_update);
}

void FlowTable::AddInternal(FlowEntry *flow_req, FlowEntry *flow,
                            FlowEntry *rflow_req, FlowEntry *rflow,
                            bool fwd_flow_update, bool rev_flow_update) {
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
        } else {
            // we are creating a new reverse flow, so avoid triggering
            // force update in this case
            force_update_rflow = false;
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

void FlowTable::DeleteInternal(FlowEntry *fe, uint64_t time) {
    if (fe->deleted()) {
        /* Already deleted return from here. */
        return;
    }
    fe->set_deleted(true);

    // Unlink the reverse flow, if one exists
    FlowEntry *rflow = fe->reverse_flow_entry();
    if (rflow) {
        rflow->set_reverse_flow_entry(NULL);
    }
    fe->set_reverse_flow_entry(NULL);

    DeleteFlowInfo(fe);
    DeleteKSync(fe);

    agent_->stats()->incr_flow_aged();
    agent_->stats()->UpdateFlowDelMinMaxStats(time);
}

bool FlowTable::DeleteFlows(FlowEntry *flow, FlowEntry *rflow) {
    uint64_t time = UTCTimestampUsec();
    if (flow) {
        /* Delete the forward flow */
        DeleteInternal(flow, time);
    }

    if (rflow) {
        DeleteInternal(rflow, time);
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
    FLOW_LOCK(flow, rflow);
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
        DeleteUnLocked(true, entry, reverse_entry);
    }
}

bool FlowTable::ValidFlowMove(const FlowEntry *new_flow,
                              const FlowEntry *old_flow) const {
    if (!new_flow || !old_flow) {
        return false;
    }

    if (new_flow->is_flags_set(FlowEntry::EcmpFlow) == false) {
        return false;
    }

    if (new_flow->data().flow_source_vrf == old_flow->data().flow_source_vrf &&
        new_flow->key().src_addr == old_flow->key().src_addr &&
        new_flow->data().source_plen == old_flow->data().source_plen) {
        //Check if both flow originate from same source route
        return true;
    }

    return false;
}

void FlowTable::UpdateReverseFlow(FlowEntry *flow, FlowEntry *rflow) {
    FlowEntry *flow_rev = flow->reverse_flow_entry();
    FlowEntry *rflow_rev = NULL;
    bool flow_rev_notify = false;
    bool rflow_rev_notify = false;

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
        if (ValidFlowMove(rflow, flow_rev)== false) {
            flow->MakeShortFlow(FlowEntry::SHORT_REVERSE_FLOW_CHANGE);
        }
        flow_rev_notify = true;
    }

    if (rflow && rflow->is_flags_set(FlowEntry::BgpRouterService)) {
        //In BGP router service for some reason if tcp connection does not
        //succeed, then client will try again with new source port and this will
        //create a new flow. Now there will be two flows - one with old source
        //port and other with new source port. However both of them will have
        //same reverse flow as its is nat'd with fabric sip/dip.
        //To avoid this delete old flow and dont let new flow to be short flow.
        if (rflow_rev) {
            DeleteUnLocked(rflow_rev->key(), false);
            rflow_rev = NULL;
        }
    }
    
    if (rflow_rev && (rflow_rev->reverse_flow_entry() == NULL)) {
        rflow_rev->MakeShortFlow(FlowEntry::SHORT_NO_REVERSE_FLOW);
        if (ValidFlowMove(flow, rflow_rev) == false) {
            flow->MakeShortFlow(FlowEntry::SHORT_REVERSE_FLOW_CHANGE);
        }
        rflow_rev_notify = true;
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
    //Has been marked for short flow, notify stats collector
    if (flow_rev_notify) {
        FlowEntryPtr flow_rev_ptr(flow_rev);
        agent()->flow_stats_manager()->AddEvent(flow_rev_ptr);
    }
    if (rflow_rev_notify) {
        FlowEntryPtr rflow_rev_ptr(rflow_rev);
        agent()->flow_stats_manager()->AddEvent(rflow_rev_ptr);
    }
}

////////////////////////////////////////////////////////////////////////////
// Flow Info tree management
////////////////////////////////////////////////////////////////////////////
void FlowTable::AddFlowInfo(FlowEntry *fe) {
    agent_->pkt()->flow_mgmt_manager()->AddEvent(fe);
}

void FlowTable::DeleteFlowInfo(FlowEntry *fe) {
    agent_->pkt()->flow_mgmt_manager()->DeleteEvent(fe);
}

/////////////////////////////////////////////////////////////////////////////
// Flow revluation routines. Processing will vary based on DBEntry type
/////////////////////////////////////////////////////////////////////////////
void FlowTable::ResyncAFlow(FlowEntry *fe) {
    fe->ResyncFlow();

    // If this is forward flow, the SG action could potentially have changed
    // due to reflexive nature. Update KSync for reverse flow first
    FlowEntry *rflow = (fe->is_flags_set(FlowEntry::ReverseFlow) == false) ?
        fe->reverse_flow_entry() : NULL;
    if (rflow) {
        UpdateKSync(rflow, true);
    }

    UpdateKSync(fe, true);
}

void FlowTable::RevaluateInterface(FlowEntry *flow) {
    const VmInterface *vmi = static_cast<const VmInterface *>
        (flow->intf_entry());
    if (vmi == NULL)
        return;

    if (flow->is_flags_set(FlowEntry::LocalFlow) &&
        flow->is_flags_set(FlowEntry::ReverseFlow)) {
        FlowEntry *fwd_flow = flow->reverse_flow_entry();
        if (fwd_flow) {
            fwd_flow->GetPolicyInfo();
            ResyncAFlow(fwd_flow);
            AddFlowInfo(fwd_flow);
        }
    }

    flow->GetPolicyInfo(vmi->vn());
    ResyncAFlow(flow);
    AddFlowInfo(flow);
}

void FlowTable::RevaluateVn(FlowEntry *flow) {
    const VnEntry *vn = flow->vn_entry();
    if (vn == NULL)
        return;

    // Revaluate flood unknown-unicast flag. If flow has UnknownUnicastFlood and
    // VN doesnt allow it, make Short Flow
    if (vn->flood_unknown_unicast() == false &&
        flow->is_flags_set(FlowEntry::UnknownUnicastFlood)) {
        flow->MakeShortFlow(FlowEntry::SHORT_NO_DST_ROUTE);
    }
    flow->GetPolicyInfo(vn);
    ResyncAFlow(flow);
    AddFlowInfo(flow);
}

void FlowTable::RevaluateAcl(FlowEntry *flow) {
    flow->GetPolicyInfo();
    ResyncAFlow(flow);
    AddFlowInfo(flow);
}

void FlowTable::RevaluateNh(FlowEntry *flow) {
    flow->GetPolicyInfo();
    ResyncAFlow(flow);
    AddFlowInfo(flow);
}

bool FlowTable::FlowRouteMatch(const InetUnicastRouteEntry *rt,
                               uint32_t vrf, Address::Family family,
                               const IpAddress &ip, uint8_t plen) {
    if (rt->vrf_id() != vrf)
        return false;

    if (rt->plen() != plen)
        return false;

    if (family == Address::INET) {
        if (ip.is_v4() == false)
            return false;

        return (Address::GetIp4SubnetAddress(ip.to_v4(), plen)
                == rt->addr().to_v4());
    }

    if (family == Address::INET6) {
        if (ip.is_v6() == false)
            return false;

        return (Address::GetIp6SubnetAddress(ip.to_v6(), rt->plen())
                == rt->addr().to_v6());
    }

    assert(0);
    return false;
}

bool FlowTable::FlowInetRpfMatch(FlowEntry *flow,
                                 const InetUnicastRouteEntry *rt) {
    if (flow->l3_flow()) {
        return FlowRouteMatch(rt, flow->data().flow_source_vrf,
                              flow->key().family, flow->key().src_addr,
                              flow->data().source_plen);
    } else {
        return FlowRouteMatch(rt, flow->data().flow_source_vrf,
                              flow->key().family, flow->key().src_addr,
                              flow->data().l2_rpf_plen);
    }
}

bool FlowTable::FlowInetSrcMatch(FlowEntry *flow,
                                 const InetUnicastRouteEntry *rt) {
    return FlowRouteMatch(rt, flow->data().flow_source_vrf, flow->key().family,
                          flow->key().src_addr, flow->data().source_plen);
}

bool FlowTable::FlowInetDstMatch(FlowEntry *flow,
                                 const InetUnicastRouteEntry *rt) {
    return FlowRouteMatch(rt, flow->data().flow_dest_vrf, flow->key().family,
                          flow->key().dst_addr, flow->data().dest_plen);
}

bool FlowTable::FlowBridgeSrcMatch(FlowEntry *flow,
                                   const BridgeRouteEntry *rt) {
    if (rt->vrf_id() != flow->data().flow_source_vrf)
        return false;

    return rt->mac() == flow->data().smac;
}

bool FlowTable::FlowBridgeDstMatch(FlowEntry *flow,
                                   const BridgeRouteEntry *rt) {
    if (rt->vrf_id() != flow->data().flow_dest_vrf)
        return false;

    return rt->mac() == flow->data().dmac;
}

bool FlowTable::RevaluateSgList(FlowEntry *flow, const AgentRoute *rt,
                                const SecurityGroupList &sg_list) {
    bool changed = false;
    if (flow->l3_flow()) {
        const InetUnicastRouteEntry *inet =
            dynamic_cast<const InetUnicastRouteEntry *>(rt);
        if (inet && FlowInetSrcMatch(flow, inet)) {
            flow->set_source_sg_id_l(sg_list);
            changed = true;
        } else if (inet && FlowInetDstMatch(flow, inet)) {
            flow->set_dest_sg_id_l(sg_list);
            changed = true;
        }
    } else {
        const BridgeRouteEntry *bridge =
            dynamic_cast<const BridgeRouteEntry *>(rt);
        if (bridge && FlowBridgeSrcMatch(flow, bridge)) {
            flow->set_source_sg_id_l(sg_list);
            changed = true;
        } else if (bridge && FlowBridgeDstMatch(flow, bridge)) {
            flow->set_dest_sg_id_l(sg_list);
            changed = true;
        }
    }

    return changed;

}

// Revaluate RPF-NH for a flow.
bool FlowTable::RevaluateRpfNH(FlowEntry *flow, const AgentRoute *rt) {
    const InetUnicastRouteEntry *inet =
        dynamic_cast<const InetUnicastRouteEntry *>(rt);
    if (inet && FlowInetRpfMatch(flow, inet)) {
        return flow->SetRpfNH(this, rt);
    }

    const BridgeRouteEntry *bridge =
        dynamic_cast<const BridgeRouteEntry *>(rt);
    if (bridge && FlowBridgeSrcMatch(flow, bridge)) {
        return flow->SetRpfNH(this, rt);
    }

    return false;
}

boost::uuids::uuid FlowTable::rand_gen() {
    return rand_gen_();
}

// Handle flow revaluation on a route change
// Route change can result in multiple changes to flow
// InetRoute   : 
//    L3 flows : Can result in change of SG-ID list for src-ip or dst-ip
//               Can result in change of RPF-NH
//
//    L2 flows : Can result in change of RPF-NH
// BridgeRoute : 
//    L2 flows : Can result in change of SG-ID list for src-mac or dst-mac
void FlowTable::RevaluateRoute(FlowEntry *flow, const AgentRoute *route) {
    VrfEntry *vrf = route->vrf();
    if (vrf == NULL || vrf->IsDeleted()) {
        DeleteMessage(flow);
        return;
    }

    // Is route deleted in meanwhile?
    if (route->IsDeleted()) {
        DeleteMessage(flow);
        return;
    }

    bool sg_changed = false;
    bool rpf_changed = false;
    FlowEntry *rflow = flow->reverse_flow_entry();
    const SecurityGroupList &sg_list = route->GetActivePath()->sg_list();

    // Handle SG Change for flow
    if (RevaluateSgList(flow, route, sg_list)) {
        sg_changed = true;
    }

    if (rflow && RevaluateSgList(rflow, route, sg_list)) {
        sg_changed = true;
    }

    // SG change should always be applied on forward flow.
    if (sg_changed) {
        if (flow->is_flags_set(FlowEntry::ReverseFlow)) {
            flow = flow->reverse_flow_entry();
        }
    }

    // Revaluate RPF-NH for the flow
    if (RevaluateRpfNH(flow, route)) {
        rpf_changed = true;
    }

    // If there is change in SG, Resync the flow
    if (sg_changed) {
        flow->GetPolicyInfo();
        ResyncAFlow(flow);
        AddFlowInfo(flow);
    } else if (rpf_changed) {
        // No change in SG, but RPF changed. On RPF change we only need to do
        // KSync update. There is no need to do Resync of flow since no flow
        // matching attributes have changed
        // when RPF is changed.
        UpdateKSync(flow, true);
    }
}

// Enqueue message to revaluate a flow
void FlowTable::RevaluateFlow(FlowEntry *flow) {
    if (flow->is_flags_set(FlowEntry::ShortFlow))
        return;

    // If this is reverse flow, enqueue the corresponding forward flow
    if (flow->is_flags_set(FlowEntry::ReverseFlow)) {
        flow = flow->reverse_flow_entry();
    }

    if (flow->set_pending_recompute(true)) {
        agent_->pkt()->get_flow_proto()->MessageRequest(new FlowTaskMsg(flow));
    }
}

// Handle deletion of a Route. Flow management module has identified that route
// must be deleted
void FlowTable::DeleteMessage(FlowEntry *flow) {
    DeleteUnLocked(true, flow, flow->reverse_flow_entry());
    DeleteFlowInfo(flow);
}

void FlowTable::EvictFlow(FlowEntry *flow, FlowEntry *reverse_flow) {
    DeleteUnLocked(false, flow, NULL);
    DeleteFlowInfo(flow);

    // Reverse flow unlinked with forward flow. Make it short-flow
    if (reverse_flow && reverse_flow->deleted() == false) {
        reverse_flow->MakeShortFlow(FlowEntry::SHORT_NO_REVERSE_FLOW);
        FlowEntryPtr reverse_flow_ptr(reverse_flow);
        agent()->flow_stats_manager()->AddEvent(reverse_flow_ptr);
        UpdateKSync(reverse_flow, true);
    }
}

// Handle events from Flow Management module for a flow
bool FlowTable::FlowResponseHandlerUnLocked(const FlowEvent *resp,
                                            FlowEntry *flow,
                                            FlowEntry *rflow) {
    const DBEntry *entry = resp->db_entry();

    bool active_flow = true;
    bool deleted_flow = flow->deleted();
    if (deleted_flow || flow->is_flags_set(FlowEntry::ShortFlow))
        active_flow = false;

    switch (resp->event()) {
    case FlowEvent::REVALUATE_FLOW: {
        if (active_flow)
            RevaluateFlow(flow);
        break;
    }

    case FlowEvent::REVALUATE_DBENTRY: {
        const Interface *intf = dynamic_cast<const Interface *>(entry);
        if (intf && active_flow) {
            RevaluateInterface(flow);
            break;
        }

        const VnEntry *vn = dynamic_cast<const VnEntry *>(entry);
        // TODO: check if the following need not be done for short flows
        if (vn && (deleted_flow == false)) {
            RevaluateVn(flow);
            break;
        }

        const AclDBEntry *acl = dynamic_cast<const AclDBEntry *>(entry);
        if (acl && active_flow) {
            RevaluateAcl(flow);
            break;
        }

        const NextHop *nh = dynamic_cast<const NextHop *>(entry);
        if (nh && active_flow) {
            RevaluateNh(flow);
            break;
        }

        const AgentRoute *rt = dynamic_cast<const AgentRoute *>(entry);
        if (rt && active_flow) {
            RevaluateRoute(flow, rt);
            break;
        }

        assert(active_flow == false);
        break;
    }

    case FlowEvent::DELETE_DBENTRY: {
        DeleteMessage(flow);
        break;
    }

    default:
        assert(0);
    }

    return true;
}

bool FlowTable::FlowResponseHandler(const FlowEvent *resp) {
    FlowEntry *flow = resp->flow();
    FlowEntry *rflow = flow->reverse_flow_entry_.get();
    FLOW_LOCK(flow, rflow);
    return FlowResponseHandlerUnLocked(resp, flow, rflow);
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
    if (update) {
        mgr->Change(flow);
        return;
    }
    mgr->Add(flow);
}

// Update FlowHandle for a flow
void FlowTable::KSyncSetFlowHandle(FlowEntry *flow, uint32_t flow_handle) {
    FlowEntry *rflow = flow->reverse_flow_entry();
    assert(flow_handle != FlowEntry::kInvalidFlowHandle);

    // compare update of flow_handle against the ksync index entry, since
    // flow handle in flow can change while before we process the response
    // from KSync and cause invalid state transition
    if (flow->ksync_index_entry()->index() == flow_handle) {
        return;
    }

    // flow-handle changed. We will need to update ksync-entry for flow with
    // new flow-handle
    KSyncFlowIndexManager *mgr = agent()->ksync()->ksync_flow_index_manager();
    mgr->UpdateFlowHandle(flow, flow_handle);
    NotifyFlowStatsCollector(flow);
    if (rflow) {
        UpdateKSync(rflow, true);
    }
}

void FlowTable::NotifyFlowStatsCollector(FlowEntry *fe) {
    /* FlowMgmt Task does not do anything apart from notifying
     * FlowStatsCollector on Flow Index change. We don't directly enqueue
     * the index change event to FlowStatsCollector to avoid Flow Index change
     * event reaching FlowStatsCollector before Flow Add
     */
    agent_->pkt()->flow_mgmt_manager()->FlowIndexUpdateEvent(fe);
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
// FlowEntryFreeList implementation
/////////////////////////////////////////////////////////////////////////////
void FlowTable::GrowFreeList() {
    free_list_.Grow();
    ksync_object_->GrowFreeList();
}

bool FlowTable::PopulateFlowPointersFromRequest(const FlowEvent *req,
                                                FlowEntry **flow,
                                                FlowEntry **rflow) {
    //First identify flow and rflow, to take lock.
    switch (req->event()) {
    case FlowEvent::DELETE_FLOW: {
        PopulateFlowEntriesUsingKey(req->get_flow_key(),
                                    req->get_del_rev_flow(),
                                    flow, rflow);
        if (!(*flow)) {
            return false;
        }
        break;
    }

    case FlowEvent::RETRY_INDEX_ACQUIRE:
    case FlowEvent::REVALUATE_FLOW:
    case FlowEvent::EVICT_FLOW: {
        *flow = req->flow();
        *rflow = (*flow)->reverse_flow_entry();
        break;
    }

    case FlowEvent::FLOW_HANDLE_UPDATE:
    case FlowEvent::KSYNC_EVENT:
    case FlowEvent::KSYNC_VROUTER_ERROR: {
        FlowTableKSyncEntry *ksync_entry =
            (static_cast<FlowTableKSyncEntry *> (req->ksync_entry()));
        *flow = ksync_entry->flow_entry().get();
        *rflow = (*flow)->reverse_flow_entry();
        break;
    }

    default: {
        assert(0);
        break;
    }
    }
    return true;
}

bool FlowTable::ProcessFlowEventInternal(const FlowEvent *req,
                                         FlowEntry *flow,
                                         FlowEntry *rflow) {
    //Take lock
    FLOW_LOCK(flow, rflow);

    //Now process events.
    switch (req->event()) {
    case FlowEvent::DELETE_FLOW: {
        DeleteUnLocked(req->get_del_rev_flow(), flow, rflow);
        break;
    }

    // Check if flow-handle changed. This can happen if vrouter tries to
    // setup the flow which was evicted earlier
    case FlowEvent::EVICT_FLOW: {
        if (flow->flow_handle() != req->flow_handle())
            break;
        EvictFlow(flow, rflow);
        break;
    }

    // Flow was waiting for an index. Index is available now. Retry acquiring
    // the index
    case FlowEvent::RETRY_INDEX_ACQUIRE: {
        if (flow->flow_handle() != req->flow_handle())
            break;
        UpdateKSync(flow, false);
        break;
    }

    case FlowEvent::FLOW_HANDLE_UPDATE: {
        KSyncSetFlowHandle(flow, req->flow_handle());
        break;
    }

    case FlowEvent::KSYNC_VROUTER_ERROR: {
        // Mark the flow entry as short flow and update ksync error event
        // to ksync index manager
        // For EEXIST error donot mark the flow as ShortFlow since Vrouter
        // generates EEXIST only for cases where another add should be
        // coming from the pkt trap from Vrouter
        if (req->ksync_error() != EEXIST) {
            flow->MakeShortFlow(FlowEntry::SHORT_FAILED_VROUTER_INSTALL);
            // Enqueue Add request to flow-stats-collector
            // to update flow flags in stats collector
            FlowEntryPtr flow_ptr(flow);
            agent()->flow_stats_manager()->AddEvent(flow_ptr);
        }
        KSyncFlowIndexManager *mgr =
            agent()->ksync()->ksync_flow_index_manager();
        mgr->UpdateKSyncError(flow);
        break;
    }

    case FlowEvent::KSYNC_EVENT: {
        FlowTableKSyncEntry *ksync_entry =
            (static_cast<FlowTableKSyncEntry *> (req->ksync_entry()));
        FlowTableKSyncObject *ksync_object = static_cast<FlowTableKSyncObject *>
            (ksync_entry->GetObject());
        ksync_object->GenerateKSyncEvent(ksync_entry, req->ksync_event());
        break;
    }

    case FlowEvent::REVALUATE_FLOW: {
        FlowResponseHandlerUnLocked(req, flow, rflow);
        break;
    }

    default: {
        assert(0);
        break;
    }
    }
    return true;
}

bool FlowTable::ProcessFlowEvent(const FlowEvent *req) {
    FlowEntry *flow = NULL;
    FlowEntry *rflow = NULL;
    if (PopulateFlowPointersFromRequest(req, &flow, &rflow) == false) {
        return false;
    }
    //Take reference of flow and rflow before operating,
    //especially meant for delete of flow eentry
    FlowEntryPtr flow_ref_ptr(flow);
    FlowEntryPtr rflow_ref_ptr(rflow);
    return ProcessFlowEventInternal(req, flow, rflow);
}

FlowEntryFreeList::FlowEntryFreeList(FlowTable *table) :
    table_(table), max_count_(0), grow_pending_(false), total_alloc_(0),
    total_free_(0), free_list_() {
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
    grow_pending_ = false;
    if (free_list_.size() >= kMinThreshold)
        return;

    for (uint32_t i = 0; i < kGrowSize; i++) {
        free_list_.push_back(*new FlowEntry(table_));
        max_count_++;
    }
}

FlowEntry *FlowEntryFreeList::Allocate(const FlowKey &key) {
    assert(table_->ConcurrencyCheck() == true);
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
        proto->GrowFreeListRequest(key);
    }
    flow->Reset(key);
    total_alloc_++;
    return flow;
}

void FlowEntryFreeList::Free(FlowEntry *flow) {
    assert(table_->ConcurrencyCheck() == true);
    total_free_++;
    flow->Reset();
    free_list_.push_back(*flow);
    // TODO : Free entry if beyound threshold
}
