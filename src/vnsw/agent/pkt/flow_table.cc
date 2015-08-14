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
#include <pkt/flow_table.h>
#include <vrouter/flow_stats/flow_stats_collector.h>
#include <vrouter/ksync/ksync_init.h>
#include <ksync/ksync_entry.h>
#include <vrouter/ksync/flowtable_ksync.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <base/os.h>

#include <route/route.h>
#include <cmn/agent_cmn.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>

#include <init/agent_param.h>
#include <cmn/agent_cmn.h>
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
#include <pkt/agent_stats.h>
#include <pkt/flow_mgmt.h>
#include <pkt/flow_mgmt_response.h>
#include <uve/agent_uve.h>
#include <uve/vm_uve_table.h>
#include <uve/vn_uve_table.h>
#include <uve/vrouter_uve_entry.h>

SandeshTraceBufferPtr FlowTraceBuf(SandeshTraceBufferCreate("Flow", 5000));
const string FlowTable::kTaskName = "Agent::FlowTable";
boost::uuids::random_generator FlowTable::rand_gen_;

/////////////////////////////////////////////////////////////////////////////
// FlowTable constructor/destructor
/////////////////////////////////////////////////////////////////////////////
FlowTable::FlowTable(Agent *agent) : 
    agent_(agent),
    flow_entry_map_(),
    linklocal_flow_count_(),
    request_queue_(agent_->task_scheduler()->GetTaskId(kTaskName), 1,
                   boost::bind(&FlowTable::RequestHandler, this, _1)) {
    max_vm_flows_ = (uint32_t)
        (agent->ksync()->flowtable_ksync_obj()->flow_table_entries_count() *
         agent->params()->max_vm_flows()) / 100;
}

FlowTable::~FlowTable() {
    assert(flow_entry_map_.size() == 0);
}

void FlowTable::Init() {
    FlowEntry::Init();
    rand_gen_ = boost::uuids::random_generator();
    agent_->acl_table()->set_ace_flow_sandesh_data_cb
        (boost::bind(&FlowTable::SetAceSandeshData, this, _1, _2, _3));
    agent_->acl_table()->set_acl_flow_sandesh_data_cb
        (boost::bind(&FlowTable::SetAclFlowSandeshData, this, _1, _2, _3));

    return;
}

void FlowTable::InitDone() {
    max_vm_flows_ = (uint32_t)
        (agent_->ksync()->flowtable_ksync_obj()->flow_table_entries_count() *
         agent_->params()->max_vm_flows()) / 100;
}

void FlowTable::Shutdown() {
    request_queue_.Shutdown();
}

// Generate flow events to FlowTable queue
void FlowTable::FlowEvent(FlowTableRequest::Event event, FlowEntry *flow) {
    FlowTableRequest req;
    req.flow_ = flow;
    req.event_ = FlowTableRequest::INVALID;

    switch (event) {
    case FlowTableRequest::ADD_FLOW:
        // The method can work in SYNC mode by calling Add routine below or
        // in ASYNC mode by enqueuing a request
        // Add(flow, flow->reverse_flow_entry());
        req.event_ = FlowTableRequest::ADD_FLOW;
        break;

    case FlowTableRequest::UPDATE_FLOW:
        req.event_ = FlowTableRequest::UPDATE_FLOW;
        break;

    default:
        assert(0);
    }

    if (req.event_ != FlowTableRequest::INVALID)
        request_queue_.Enqueue(req);
}

bool FlowTable::RequestHandler(const FlowTableRequest &req) {
    switch (req.event_) {
    case FlowTableRequest::ADD_FLOW: {
        // The reference to reverse flow can be dropped in the call. This can
        // potentially release the reverse flow. Hold reference to reverse flow
        // till this method is complete
        FlowEntryPtr rflow = req.flow_->reverse_flow_entry();
        Add(req.flow_.get(), rflow.get());
        break;
    }

    case FlowTableRequest::UPDATE_FLOW: {
        // The reference to reverse flow can be dropped in the call. This can
        // potentially release the reverse flow. Hold reference to reverse flow
        // till this method is complete
        FlowEntryPtr rflow = req.flow_->reverse_flow_entry();
        Update(req.flow_.get(), rflow.get());
        break;
    }
    default:
         assert(0);

    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// FlowTable Add/Delete routines
/////////////////////////////////////////////////////////////////////////////
FlowEntry *FlowTable::Find(const FlowKey &key) {
    FlowEntryMap::iterator it;

    it = flow_entry_map_.find(key);
    if (it != flow_entry_map_.end()) {
        return it->second;
    } else {
        return NULL;
    }
}

void FlowTable::Copy(FlowEntry *lhs, const FlowEntry *rhs) {
    DeleteFlowInfo(lhs);
    if (rhs)
        lhs->Copy(rhs);
}

FlowEntry *FlowTable::Locate(FlowEntry *flow) {
    std::pair<FlowEntryMap::iterator, bool> ret;
    ret = flow_entry_map_.insert(FlowEntryMapPair(flow->key(), flow));
    if (ret.second == true) {
        flow->stats_.setup_time = UTCTimestampUsec();
        agent_->stats()->incr_flow_created();
        ret.first->second->set_on_tree();
        return flow;
    }

    return ret.first->second;
}

void FlowTable::Add(FlowEntry *flow, FlowEntry *rflow) {
    FlowEntry *new_flow = Locate(flow);
    FlowEntry *new_rflow = (rflow != NULL) ? Locate(rflow) : NULL;
    Add(flow, new_flow, rflow, new_rflow, false);
}

void FlowTable::Update(FlowEntry *flow, FlowEntry *rflow) {
    FlowEntry *new_flow = Find(flow->key());
    FlowEntry *new_rflow = (rflow != NULL) ? Find(rflow->key()) : NULL;
    Add(flow, new_flow, rflow, new_rflow, true);
}

void FlowTable::AddInternal(FlowEntry *flow_req, FlowEntry *flow,
                            FlowEntry *rflow_req, FlowEntry *rflow,
                            bool update) {
    // The forward and reverse flow in request are linked. Unlink the flows
    // first. Flow table processing will link them if necessary
    flow_req->set_reverse_flow_entry(NULL);
    if (rflow_req)
        rflow_req->set_reverse_flow_entry(NULL);

    if (update) {
        if (flow == NULL)
            return;

        if (flow->deleted() || flow->IsShortFlow()) {
            return;
        }
    }

    if (flow_req != flow) {
        Copy(flow, flow_req);
        flow->set_deleted(false);
    }

    if (rflow && rflow_req != rflow) {
        Copy(rflow, rflow_req);
        rflow->set_deleted(false);
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
        UpdateKSync(rflow);
        AddFlowInfo(rflow);
    }

    UpdateKSync(flow);
    AddFlowInfo(flow);
}

void FlowTable::Add(FlowEntry *flow_req, FlowEntry *flow,
                    FlowEntry *rflow_req, FlowEntry *rflow, bool update) {
    if (flow) {
        flow->mutex().lock();
    }
    if (rflow) {
        rflow->mutex().lock();
    }

    AddInternal(flow_req, flow, rflow_req, rflow, update);

    if (rflow) {
        rflow->mutex().unlock();
    }
    if (flow) {
        flow->mutex().unlock();
    }
}

void FlowTable::DeleteInternal(FlowEntryMap::iterator &it) {
    FlowEntry *fe = it->second;
    if (fe->deleted()) {
        /* Already deleted return from here. */
        return;
    }
    fe->set_deleted(true);
    FlowTableKSyncObject *ksync_obj = 
        agent_->ksync()->flowtable_ksync_obj();

    // Unlink the reverse flow, if one exists
    FlowEntry *rflow = fe->reverse_flow_entry();
    if (rflow) {
        rflow->set_reverse_flow_entry(NULL);
    }
    fe->set_reverse_flow_entry(NULL);

    DeleteFlowInfo(fe);

    FlowTableKSyncEntry *ksync_entry = fe->ksync_entry_;
    KSyncEntry::KSyncEntryPtr ksync_ptr = ksync_entry;
    if (ksync_entry) {
        ksync_obj->Delete(ksync_entry);
        fe->ksync_entry_ = NULL;
    } else {
        FLOW_TRACE(Err, fe->flow_handle(), "Entry not found in ksync");
        if (fe->reverse_flow_entry() != NULL) {
            fe->set_reverse_flow_entry(NULL);
        }
    }

    agent_->stats()->incr_flow_aged();
}

bool FlowTable::Delete(const FlowKey &key, bool del_reverse_flow) {
    FlowEntryMap::iterator it;
    FlowEntry *fe;

    it = flow_entry_map_.find(key);
    if (it == flow_entry_map_.end()) {
        return false;
    }
    fe = it->second;

    FlowEntry *reverse_flow = NULL;
    if (del_reverse_flow) {
        reverse_flow = fe->reverse_flow_entry();
    }

    /* Send flow log messages for both forward and reverse flows before we
     * delete any flows because we need relationship between forward and
     * reverse flow during FlowExport. This relationship will be broken if
     * either of forward or reverse flow is deleted */
    SendFlows(fe, reverse_flow);
    /* Delete the forward flow */
    DeleteInternal(it);

    if (!reverse_flow) {
        return true;
    }

    it = flow_entry_map_.find(reverse_flow->key());
    if (it != flow_entry_map_.end()) {
        DeleteInternal(it);
        return true;
    }
    return false;
}

void FlowTable::DeleteAll() {
    FlowEntryMap::iterator it;

    it = flow_entry_map_.begin();
    while (it != flow_entry_map_.end()) {
        FlowEntry *entry = it->second;
        ++it;
        if (it != flow_entry_map_.end() &&
            it->second == entry->reverse_flow_entry()) {
            ++it;
        }
        Delete(entry->key(), true);
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
    }

    if (rflow_rev && (rflow_rev->reverse_flow_entry() == NULL)) {
        rflow_rev->MakeShortFlow(FlowEntry::SHORT_NO_REVERSE_FLOW);
        if (ValidFlowMove(flow, rflow_rev) == false) {
            flow->MakeShortFlow(FlowEntry::SHORT_REVERSE_FLOW_CHANGE);
        }
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

////////////////////////////////////////////////////////////////////////////
// VM notification handler
////////////////////////////////////////////////////////////////////////////
void FlowTable::DeleteVmFlowInfo(FlowEntry *fe, const VmEntry *vm) {
    VmFlowTree::iterator vm_it = vm_flow_tree_.find(vm);
    if (vm_it != vm_flow_tree_.end()) {
        VmFlowInfo *vm_flow_info = vm_it->second;
        if (vm_flow_info->fet.erase(fe)) {
            if (fe->linklocal_src_port()) {
                vm_flow_info->linklocal_flow_count--;
                linklocal_flow_count_--;
            }
            if (vm_flow_info->fet.empty()) {
                delete vm_flow_info;
                vm_flow_tree_.erase(vm_it);
            }
        }
    }
}

void FlowTable::DeleteVmFlowInfo(FlowEntry *fe) {
    if (fe->in_vm_entry()) {
        DeleteVmFlowInfo(fe, fe->in_vm_entry());
    }
    if (fe->out_vm_entry()) {
        DeleteVmFlowInfo(fe, fe->out_vm_entry());
    }
}

void FlowTable::AddVmFlowInfo(FlowEntry *fe) {
    if (fe->in_vm_entry()) {
        AddVmFlowInfo(fe, fe->in_vm_entry());
    }
    if (fe->out_vm_entry()) {
        AddVmFlowInfo(fe, fe->out_vm_entry());
    }
}

void FlowTable::AddVmFlowInfo(FlowEntry *fe, const VmEntry *vm) {
    if (fe->is_flags_set(FlowEntry::ShortFlow)) {
        // do not include short flows
        // this is done so that we allow atleast the minimum allowed flows
        // for a VM; Otherwise, in a continuous flow scenario, all flows
        // become short and we dont allow any flows to a VM.
        return;
    }

    bool update = false;
    VmFlowTree::iterator it;
    it = vm_flow_tree_.find(vm);
    VmFlowInfo *vm_flow_info;
    if (it == vm_flow_tree_.end()) {
        vm_flow_info = new VmFlowInfo();
        vm_flow_info->vm_entry = vm;
        vm_flow_info->fet.insert(fe);
        vm_flow_tree_.insert(VmFlowPair(vm, vm_flow_info));
        update = true;
    } else {
        vm_flow_info = it->second;
        /* fe can already exist. In that case it won't be inserted */
        if (vm_flow_info->fet.insert(fe).second) {
            update = true;
        }
    }
    if (update) {
        if (fe->linklocal_src_port()) {
            vm_flow_info->linklocal_flow_count++;
            linklocal_flow_count_++;
        }
    }
}

void FlowTable::DeleteVmFlows(const VmEntry *vm) {
    VmFlowTree::iterator vm_it;
    vm_it = vm_flow_tree_.find(vm);
    if (vm_it == vm_flow_tree_.end()) {
        return;
    }
    FLOW_TRACE(ModuleInfo, "Delete VM flows");
    FlowEntryTree fet = vm_it->second->fet;
    FlowEntryTree::iterator fet_it;
    for (fet_it = fet.begin(); fet_it != fet.end(); ++fet_it) {
        Delete((*fet_it)->key(), true);
    }
}

uint32_t FlowTable::VmFlowCount(const VmEntry *vm) {
    VmFlowTree::iterator it = vm_flow_tree_.find(vm);
    if (it != vm_flow_tree_.end()) {
        VmFlowInfo *vm_flow_info = it->second;
        return vm_flow_info->fet.size();
    }

    return 0;
}

uint32_t FlowTable::VmLinkLocalFlowCount(const VmEntry *vm) {
    VmFlowTree::iterator it = vm_flow_tree_.find(vm);
    if (it != vm_flow_tree_.end()) {
        VmFlowInfo *vm_flow_info = it->second;
        return vm_flow_info->linklocal_flow_count;
    }

    return 0;
}

////////////////////////////////////////////////////////////////////////////
// Flow Info tree management
////////////////////////////////////////////////////////////////////////////
void FlowTable::NewFlow(const FlowEntry *flow) {
    uint8_t proto = flow->key().protocol;
    uint16_t sport = flow->key().src_port;
    uint16_t dport = flow->key().dst_port;
    AgentUveBase *uve = agent()->uve();

    // Update vrouter port bitmap
    VrouterUveEntry *vre = static_cast<VrouterUveEntry *>(
        uve->vrouter_uve_entry());
    vre->UpdateBitmap(proto, sport, dport);

    // Update source-vn port bitmap
    VnUveTable *vnte = static_cast<VnUveTable *>(uve->vn_uve_table());
    vnte->UpdateBitmap(flow->data().source_vn, proto, sport, dport);

    // Update dest-vn port bitmap
    vnte->UpdateBitmap(flow->data().dest_vn, proto, sport, dport);

    const Interface *intf = flow->data().intf_entry.get();

    const VmInterface *port = dynamic_cast<const VmInterface *>(intf);
    if (port == NULL) {
        return;
    }
    const VmEntry *vm = port->vm();
    if (vm == NULL) {
        return;
    }

    // update vm and interface (all interfaces of vm) bitmap
    VmUveTable *vmt = static_cast<VmUveTable *>(uve->vm_uve_table());
    vmt->UpdateBitmap(vm, proto, sport, dport);
}

void FlowTable::AddFlowInfo(FlowEntry *fe) {
    NewFlow(fe);
    // Add VmFlowTree
    AddVmFlowInfo(fe);
    agent_->pkt()->flow_mgmt_manager()->AddEvent(fe);
}

void FlowTable::DeleteFlow(const FlowEntry *flow) {
    /* We need not reset bitmaps on flow deletion. We will have to
     * provide introspect to reset this */
}

void FlowTable::DeleteFlowInfo(FlowEntry *fe) {
    DeleteFlow(fe);
    // Remove from VmFlowTree
    DeleteVmFlowInfo(fe);
    agent_->pkt()->flow_mgmt_manager()->DeleteEvent(fe);
}

void FlowTable::VnFlowCounters(const VnEntry *vn, uint32_t *in_count, 
                               uint32_t *out_count) {
    *in_count = 0;
    *out_count = 0;
    if (vn == NULL)
        return;

    agent_->pkt()->flow_mgmt_manager()->VnFlowCounters(vn, in_count, out_count);
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
        UpdateKSync(rflow);
    }

    UpdateKSync(fe);
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

    if (RevaluateSgList(rflow, route, sg_list)) {
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
        UpdateKSync(flow);
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
        agent_->pkt()->pkt_handler()->SendMessage(PktHandler::FLOW,
                                                  new FlowTaskMsg(flow));
    }
}

// Handle deletion of a Route. Flow management module has identified that route
// must be deleted
void FlowTable::DeleteMessage(FlowEntry *flow) {
    Delete(flow->key(), true);
    DeleteFlowInfo(flow);
}

// Handle events from Flow Management module for a flow
bool FlowTable::FlowResponseHandler(const FlowMgmtResponse *resp) {
    FlowEntry *flow = resp->flow();
    const DBEntry *entry = resp->db_entry();
    tbb::mutex::scoped_lock mutex(flow->mutex());
    FlowEntry *rflow = flow->reverse_flow_entry_.get();
    if (rflow)
        rflow->mutex().lock();

    bool active_flow = true;
    bool deleted_flow = flow->deleted();
    if (deleted_flow || flow->is_flags_set(FlowEntry::ShortFlow))
        active_flow = false;

    switch (resp->event()) {
    case FlowMgmtResponse::REVALUATE_FLOW: {
        if (active_flow)
            RevaluateFlow(flow);
        break;
    }

    case FlowMgmtResponse::REVALUATE_DBENTRY: {
        const Interface *intf = dynamic_cast<const Interface *>(entry);
        if (intf && active_flow) {
            RevaluateInterface(flow);
            break;
        }

        const VnEntry *vn = dynamic_cast<const VnEntry *>(entry);
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

    case FlowMgmtResponse::DELETE_DBENTRY: {
        DeleteMessage(flow);
        break;
    }

    default:
        assert(0);
    }

    if (rflow)
        rflow->mutex().unlock();

    return true;
}

/////////////////////////////////////////////////////////////////////////////
// KSync Routines
/////////////////////////////////////////////////////////////////////////////
void FlowTable::UpdateKSync(FlowEntry *flow) {
    FlowTableKSyncObject *ksync_obj = agent_->ksync()->flowtable_ksync_obj();
    if (flow->ksync_entry() == NULL) {
        FlowTableKSyncEntry key(ksync_obj, flow, flow->flow_handle());
        flow->set_ksync_entry
            (static_cast<FlowTableKSyncEntry *>(ksync_obj->Create(&key)));
        if (flow->deleted()) {
            /*
             * Create and delete a KSync Entry when update ksync entry is
             * triggered for a deleted flow entry.
             * This happens when Reverse flow deleted  is deleted before
             * getting an ACK from vrouter.
             */
            ksync_obj->Delete(flow->ksync_entry());
            flow->set_ksync_entry(NULL);
        }
    } else {
        if (flow->flow_handle() != flow->ksync_entry()->hash_id()) {
            /*
             * if flow handle changes delete the previous record from
             * vrouter and install new
             */
            ksync_obj->Delete(flow->ksync_entry());
            FlowTableKSyncEntry key(ksync_obj, flow, flow->flow_handle());
            flow->set_ksync_entry
                (static_cast<FlowTableKSyncEntry *>(ksync_obj->Create(&key)));
        } else {
            ksync_obj->Change(flow->ksync_entry());
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
string FlowTable::GetAceSandeshDataKey(const AclDBEntry *acl, int ace_id) {
    string uuid_str = UuidToString(acl->GetUuid());
    stringstream ss;
    ss << uuid_str << ":";
    ss << ace_id;
    return ss.str();
}

void FlowTable::DispatchFlowMsg(SandeshLevel::type level, FlowDataIpv4 &flow) {
    FLOW_DATA_IPV4_OBJECT_LOG("", level, flow);
}

void FlowTable::FlowExport(FlowEntry *flow, uint64_t diff_bytes,
                           uint64_t diff_pkts) {
}

void FlowTable::SetAceSandeshData(const AclDBEntry *acl, AclFlowCountResp &data,
                                  int ace_id) {
}

void FlowTable::SetAclFlowSandeshData(const AclDBEntry *acl, AclFlowResp &data, 
                                      const int last_count) {
}

void FlowTable::SendFlowInternal(FlowEntry *fe) {
    if (fe->deleted()) {
        /* Already deleted return from here. */
        return;
    }
    FlowStatsCollector *fec = agent_->flow_stats_collector();
    uint64_t diff_bytes, diff_packets;
    fec->UpdateFlowStats(fe, diff_bytes, diff_packets);

    fe->stats_.teardown_time = UTCTimestampUsec();
    FlowExport(fe, diff_bytes, diff_packets);
    /* Reset stats and teardown_time after these information is exported during
     * flow delete so that if the flow entry is reused they point to right
     * values */
    fe->ResetStats();
    fe->stats_.teardown_time = 0;
}

void FlowTable::SendFlows(FlowEntry *flow, FlowEntry *rflow) {
    SendFlowInternal(flow);
    if (rflow) {
        SendFlowInternal(rflow);
    }
}
