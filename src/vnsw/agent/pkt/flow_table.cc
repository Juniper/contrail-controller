/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <bitset>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>
#include <pkt/flow_table.h>
#include <uve/flow_stats.h>
#include <uve/inter_vn_stats.h>
#include <ksync/flowtable_ksync.h>
#include <ksync/ksync_init.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include "route/route.h"
#include "cmn/agent_cmn.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include "route/route.h"
#include "cmn/agent_cmn.h"
#include "cmn/agent_stats.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/agent_route.h"
#include "oper/vrf.h"
#include "oper/vm.h"
#include "oper/sg.h"

#include "filter/packet_header.h"
#include "filter/acl.h"

#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "pkt/pkt_handler.h"
#include "pkt/flow_proto.h"
#include "pkt/pkt_types.h"
#include "uve/flow_uve.h"
#include "uve/uve_init.h"
#include "pkt/pkt_sandesh_flow.h"

boost::uuids::random_generator FlowTable::rand_gen_ = boost::uuids::random_generator();
tbb::atomic<int> FlowEntry::alloc_count_;

inline void intrusive_ptr_add_ref(FlowEntry *fe) {
    fe->refcount_.fetch_and_increment();
}
inline void intrusive_ptr_release(FlowEntry *fe) {
    int prev = fe->refcount_.fetch_and_decrement();
    if (prev == 1) {
        FlowTable *table = Agent::GetInstance()->pkt()->flow_table();
        FlowTable::FlowEntryMap::iterator it = table->flow_entry_map_.find(fe->key);
        assert(it != table->flow_entry_map_.end());
        table->flow_entry_map_.erase(it);
        delete fe;
    }
}

static bool ShouldDrop(uint32_t action) {
    if ((action & TrafficAction::DROP_FLAGS) || (action & TrafficAction::IMPLICIT_DENY_FLAGS))
        return true;

    return false;
}

static uint32_t ReflexiveAction(uint32_t action) {
    if (ShouldDrop(action) == false) {
        return action;
    }

    // If forward flow is DROP, set action for reverse flow to
    // TRAP. If packet hits reverse flow, we will re-establish
    // the flows
    action &= ~(TrafficAction::DROP_FLAGS);
    return (action |= (1 << TrafficAction::TRAP));
}


bool RouteFlowKey::FlowSrcMatch(FlowEntry *flow) const {
    uint32_t prefix = GetPrefix(flow->key.src.ipv4, flow->data.source_plen);
    if (flow->data.flow_source_vrf == vrf &&
        prefix == ip.ipv4 &&
        flow->data.source_plen == plen) {
        return true;
    }
    return false;
}

bool RouteFlowKey::FlowDestMatch(FlowEntry *flow) const {
    uint32_t prefix = GetPrefix(flow->key.dst.ipv4, flow->data.dest_plen);
    if (flow->data.flow_dest_vrf == vrf &&
        prefix == ip.ipv4 &&
        flow->data.dest_plen == plen) {
        return true;
    }
    return false;
}

uint32_t FlowEntry::MatchAcl(const PacketHeader &hdr, MatchPolicy *policy,
                             std::list<MatchAclParams> &acl,
                             bool add_implicit_deny) {
    // If there are no ACL to match, make it pass
    if (acl.size() == 0) {
        return (1 << TrafficAction::PASS);
    }

    // PASS default GW traffic, if it is ICMP or DNS
    if ((hdr.protocol == IPPROTO_ICMP ||
         (hdr.protocol == IPPROTO_UDP && 
          (hdr.src_port == DNS_SERVER_PORT || hdr.dst_port == DNS_SERVER_PORT))) &&
        (Agent::GetInstance()->pkt()->pkt_handler()->
         IsGwPacket(data.intf_entry.get(), hdr.dst_ip) ||
         Agent::GetInstance()->pkt()->pkt_handler()->
         IsGwPacket(data.intf_entry.get(), hdr.src_ip))) {
        return (1 << TrafficAction::PASS);
    }

    uint32_t action = 0;
    for (std::list<MatchAclParams>::iterator it = acl.begin();
         it != acl.end(); ++it) {
        if (it->acl.get() == NULL) {
            continue;
        }

        if (it->acl->PacketMatch(hdr, *it)) {
            action |= it->action_info.action;
            if (it->action_info.action & (1 << TrafficAction::MIRROR)) {
                policy->action_info.mirror_l.insert
                    (policy->action_info.mirror_l.end(),
                     it->action_info.mirror_l.begin(),
                     it->action_info.mirror_l.end());
            }
            if (it->terminal_rule) {
                break;
            }
        }
    }

    // If no acl matched, make it imlicit deny
    if (action == 0 && add_implicit_deny) {
        action = (1 << TrafficAction::DROP) | 
            (1 << TrafficAction::IMPLICIT_DENY);;
    }

    return action;
}

// Recompute FlowEntry action
bool FlowEntry::ActionRecompute(MatchPolicy *policy) {
    uint32_t action = 0;

    action = policy->policy_action | policy->sg_action |
        policy->out_policy_action | policy->out_sg_action |
        policy->mirror_action | policy->out_mirror_action;

    // check for conflicting actions and remove allowed action
    if (ShouldDrop(action)) {
        action = (action & ~TrafficAction::DROP_FLAGS & ~TrafficAction::PASS_FLAGS);
        action |= (1 << TrafficAction::DROP);
    }

    if (action & (1 << TrafficAction::TRAP)) {
        action = (1 << TrafficAction::TRAP);
    }

    if (action != policy->action_info.action) {
        policy->action_info.action = action;
        return true;
    }

    return false;
}

// Apply Policy and SG rules for a flow. Rules applied are based on flow type
// Non-Local Forward Flow
//      Network Policy. 
//      Out-Network Policy will be empty
//      SG
//      Out-SG will be empty
// Non-Local Reverse Flow
//      Network Policy. 
//      Out-Network Policy will be empty
//      SG and out-SG from forward flow
// Local Forward Flow
//      Network Policy. 
//      Out-Network Policy
//      SG
//      Out-SG 
// Local Reverse Flow
//      Network Policy. 
//      Out-Network Policy
//      SG and out-SG from forward flow
bool FlowEntry::DoPolicy(const PacketHeader &hdr, MatchPolicy *policy,
                         bool ingress_flow) {
    policy->action_info.Clear();
    policy->policy_action = 0;
    policy->out_policy_action = 0;
    policy->sg_action = 0;
    policy->out_sg_action = 0;
    policy->mirror_action = 0;
    policy->out_mirror_action = 0;

    // Mirror is valid even if packet is to be dropped. So, apply it first
    policy->mirror_action = MatchAcl(hdr, policy, policy->m_mirror_acl_l,
                                     false);
    policy->out_mirror_action = MatchAcl(hdr, policy,
                                         policy->m_out_mirror_acl_l, false);

    // Apply network policy
    policy->policy_action = MatchAcl(hdr, policy, policy->m_acl_l, true);
    if (ShouldDrop(policy->policy_action)) {
        goto done;
    }

    policy->out_policy_action = MatchAcl(hdr, policy, policy->m_out_acl_l,
                                         true);
    if (ShouldDrop(policy->out_policy_action)) {
        goto done;
    }

    // Apply security-group
    if (is_reverse_flow == false) {
        policy->sg_action = MatchAcl(hdr, policy, policy->m_sg_acl_l, true);
        if (ShouldDrop(policy->sg_action)) {
            goto done;
        }

        policy->out_sg_action = MatchAcl(hdr, policy, policy->m_out_sg_acl_l,
                                         true);
        if (ShouldDrop(policy->out_sg_action)) {
            goto done;
        }
    } else {
        // SG is reflexive ACL. For reverse-flow, copy SG action from
        // forward flow 
        if (data.reverse_flow.get()) {
            policy->sg_action = 
                ReflexiveAction(data.reverse_flow->data.match_p.sg_action);
            policy->out_sg_action = 
                ReflexiveAction(data.reverse_flow->data.match_p.out_sg_action);
        } else {
            policy->sg_action = (1 << TrafficAction::PASS);
            policy->out_sg_action = (1 << TrafficAction::PASS);
        }

        if (ShouldDrop(policy->sg_action) || ShouldDrop(policy->out_sg_action)){
            goto done;
        }
    }

done:
    // Summarize the actions based on lookups above
    ActionRecompute(policy);
    return true;
}

void FlowEntry::GetSgList(const Interface *intf, MatchPolicy *policy) {
    policy->m_sg_acl_l.clear();
    policy->m_out_sg_acl_l.clear();

    // Dont apply network-policy for linklocal flow
    if (linklocal_flow) {
        return;
    }

    // SG ACL's are reflexive. Skip SG for reverse flow
    if (is_reverse_flow) {
        return;
    }

    if (intf == NULL) {
        return;
    }

    const VmInterface *vm_port = static_cast<const VmInterface *>(intf);
    if (vm_port->sg_list().list_.size()) {
        policy->nw_policy = true;
        VmInterface::SecurityGroupEntrySet::const_iterator it;
        for (it = vm_port->sg_list().list_.begin();
             it != vm_port->sg_list().list_.end(); ++it) {
            if (it->sg_.get() == NULL)
                continue;
            MatchAclParams acl;
            acl.acl = it->sg_->GetAcl();
            // If SG does not have ACL. Skip it
            if (acl.acl == NULL)
                continue;
            policy->m_sg_acl_l.push_back(acl);
        }
    }

    // For local flows, we have to apply SG Policy from out-intf also
    FlowEntry *rflow = data.reverse_flow.get();
    if (local_flow == false || rflow == NULL) {
        // Not local flow
        return;
    }

    if (rflow->data.intf_entry.get() == NULL) {
        return;
    }

    if (rflow->data.intf_entry->type() != Interface::VM_INTERFACE) {
        return;
    }

    vm_port = static_cast<const VmInterface *>
        (rflow->data.intf_entry.get());
    if (vm_port->sg_list().list_.size()) {
        policy->nw_policy = true;
        VmInterface::SecurityGroupEntrySet::const_iterator it;
        for (it = vm_port->sg_list().list_.begin();
             it != vm_port->sg_list().list_.end(); ++it) {
            if (it->sg_.get() == NULL)
                continue;
            MatchAclParams acl;
            acl.acl = it->sg_->GetAcl();
            // If SG does not have ACL. Skip it
            if (acl.acl == NULL)
                continue;
            policy->m_out_sg_acl_l.push_back(acl);
        }
    }
}

void FlowEntry::GetPolicy(const VnEntry *vn, MatchPolicy *policy) {
    policy->m_acl_l.clear();
    policy->m_out_acl_l.clear();
    policy->m_mirror_acl_l.clear();
    policy->m_out_mirror_acl_l.clear();

    if (vn == NULL)
        return;

    MatchAclParams acl;

    // Get Mirror configuration first
    if (vn->GetMirrorAcl()) {
        acl.acl = vn->GetMirrorAcl();
        policy->m_mirror_acl_l.push_back(acl);
    }

    if (vn->GetMirrorCfgAcl()) {
        acl.acl = vn->GetMirrorCfgAcl();
        policy->m_mirror_acl_l.push_back(acl);
    }

    // Dont apply network-policy for linklocal flow
    if (linklocal_flow) {
        return;
    }

    if (vn->GetAcl()) {
        acl.acl = vn->GetAcl();
        policy->m_acl_l.push_back(acl);
        policy->nw_policy = true;
    }

    const VnEntry *rvn = NULL;
    FlowEntry *rflow = data.reverse_flow.get();
    // For local flows, we have to apply NW Policy from out-vn also
    if (local_flow == false || rflow == NULL) {
        // Not local flow
        return;
    }

    rvn = rflow->data.vn_entry.get();
    if (rvn == NULL) {
        return;
    }

    if (rvn->GetAcl()) {
        acl.acl = rvn->GetAcl();
        policy->m_out_acl_l.push_back(acl);
        policy->nw_policy = true;
    }

    if (rvn->GetMirrorAcl()) {
        acl.acl = rvn->GetMirrorAcl();
        policy->m_out_mirror_acl_l.push_back(acl);
    }

    if (rvn->GetMirrorCfgAcl()) {
        acl.acl = rvn->GetMirrorCfgAcl();
        policy->m_out_mirror_acl_l.push_back(acl);
    }
}

void FlowEntry::UpdateKSync(FlowTableKSyncEntry *entry, bool create) {
    FlowInfo flow_info;
    FillFlowInfo(flow_info);
    FlowStatsCollector::FlowExport(this, 0, 0);
    FlowTableKSyncObject *ksync_obj = 
        Agent::GetInstance()->ksync()->flowtable_ksync_obj();

    if (entry == NULL) {
        FLOW_TRACE(Trace, "Add", flow_info);
        FlowTableKSyncEntry key(ksync_obj, this, flow_handle);
        ksync_obj->Create(&key);    
    } else {
        FLOW_TRACE(Trace, "Change", flow_info);
        ksync_obj->Change(entry);    
    }
}

void FlowEntry::CompareAndModify(const MatchPolicy &m_policy, bool create) {
    FlowTableKSyncEntry *entry = 
        Agent::GetInstance()->ksync()->flowtable_ksync_obj()->Find(this);

    data.match_p = m_policy;
    UpdateKSync(entry, create);
}

void FlowEntry::SetEgressUuid() {
    if (local_flow) {
        egress_uuid = FlowTable::rand_gen_();
    }
}

void FlowEntry::MakeShortFlow() {
    short_flow = true;
    if (data.reverse_flow) {
        data.reverse_flow->short_flow = true;
    }
}

void FlowEntry::GetPolicyInfo(MatchPolicy *policy) {
    // Default make it false
    data.match_p.nw_policy = false;

    if (short_flow == true) {
        data.match_p.action_info.action = (1 << TrafficAction::DROP);
        return;
    }

    // ACL supported on VMPORT interfaces only
    if (data.intf_entry == NULL)
        return;

    if  (data.intf_entry->type() != Interface::VM_INTERFACE)
        return;

    // Get Network policy/mirror cfg policy/mirror policies 
    GetPolicy(data.vn_entry.get(), policy);

    // Get Sg list
    GetSgList(data.intf_entry.get(), policy);
}

void FlowTable::Add(FlowEntry *flow, FlowEntry *rflow) {
    UpdateReverseFlow(flow, rflow);

    MatchPolicy policy;
    flow->GetPolicyInfo(&policy);
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
        MatchPolicy rpolicy;
        rflow->GetPolicyInfo(&rpolicy);
        ResyncAFlow(rflow, rpolicy, true);
        AddFlowInfo(rflow);
    }


    ResyncAFlow(flow, policy, true);
    AddFlowInfo(flow);
}

void FlowTable::UpdateReverseFlow(FlowEntry *flow, FlowEntry *rflow) {
    FlowEntry *flow_rev = flow->data.reverse_flow.get();
    FlowEntry *rflow_rev = NULL;

    if (rflow) {
        rflow_rev = rflow->data.reverse_flow.get();
    }

    if (rflow_rev) {
        assert(rflow_rev->data.reverse_flow.get() == rflow);
        rflow_rev->data.reverse_flow = NULL;
    }

    if (flow_rev) {
        flow_rev->data.reverse_flow = NULL;
    }

    flow->data.reverse_flow = rflow;
    if (rflow) {
        rflow->data.reverse_flow = flow;
    }

    if (flow_rev && (flow_rev->data.reverse_flow.get() == NULL)) {
        flow_rev->MakeShortFlow();
        flow->MakeShortFlow();
    }

    if (rflow_rev && (rflow_rev->data.reverse_flow.get() == NULL)) {
        rflow_rev->MakeShortFlow();
        flow->MakeShortFlow();
    }

    if (flow->data.reverse_flow.get() == NULL) {
        flow->MakeShortFlow();
    }

    if (rflow && rflow->data.reverse_flow.get() == NULL) {
        rflow->MakeShortFlow();
    }

    if (rflow) {
        if (flow->short_flow || rflow->short_flow) {
            flow->MakeShortFlow();
        }
    }
}

void FlowEntry::FillFlowInfo(FlowInfo &info) {
    info.set_flow_index(flow_handle);
    info.set_source_ip(Ip4Address(key.src.ipv4).to_string());
    info.set_source_port(key.src_port);
    info.set_destination_ip(Ip4Address(key.dst.ipv4).to_string());
    info.set_destination_port(key.dst_port);
    info.set_protocol(key.protocol);
    info.set_vrf(key.vrf);

    std::ostringstream str;
    uint32_t fe_action = data.match_p.action_info.action;
    if (fe_action & (1 << TrafficAction::DENY)) {
        str << "DENY";
    } else if (fe_action & (1 << TrafficAction::PASS)) {
        str << "ALLOW, ";
    }

    if (nat) {
        FlowEntry *nat_flow = data.reverse_flow.get();
        str << " NAT";
        if (nat_flow) {
            if (key.src.ipv4 != nat_flow->key.dst.ipv4) {
                info.set_nat_source_ip(Ip4Address(nat_flow->key.dst.ipv4).\
                                       to_string());
            }

            if (key.dst.ipv4 != nat_flow->key.src.ipv4) {
                info.set_nat_destination_ip(Ip4Address(nat_flow->key.src.ipv4).\
                                             to_string());
            }

            if (key.src_port != nat_flow->key.dst_port)  {
                info.set_nat_source_port(nat_flow->key.dst_port);
            }

            if (key.dst_port != nat_flow->key.src_port) {
                info.set_nat_destination_port(nat_flow->key.src_port);
            }
            info.set_nat_protocol(nat_flow->key.protocol);
            info.set_nat_vrf(data.dest_vrf);
            info.set_reverse_index(nat_flow->flow_handle);
            info.set_nat_mirror_vrf(nat_flow->data.mirror_vrf);
        }
    }

    if (data.match_p.action_info.action & (1 << TrafficAction::MIRROR)) {
        str << " MIRROR";
        std::vector<MirrorActionSpec>::iterator it;
        std::vector<MirrorInfo> mirror_l;
        for (it = data.match_p.action_info.mirror_l.begin();
             it != data.match_p.action_info.mirror_l.end();
             ++it) {
            MirrorInfo mirror_info;
            mirror_info.set_mirror_destination((*it).ip.to_string());
            mirror_info.set_mirror_port((*it).port);
            mirror_info.set_mirror_vrf((*it).vrf_name);
            mirror_info.set_analyzer((*it).analyzer_name);
            mirror_l.push_back(mirror_info);
        }
        info.set_mirror_l(mirror_l);
    }
    info.set_mirror_vrf(data.mirror_vrf);
    info.set_action(str.str());
    info.set_implicit_deny(ImplicitDenyFlow() ? "yes" : "no");
    info.set_short_flow(ShortFlow() ? "yes" : "no");
    if (data.ecmp == true && 
            data.component_nh_idx != CompositeNH::kInvalidComponentNHIdx) {
        info.set_ecmp_index(data.component_nh_idx);
    }
    if (data.trap) {
        info.set_trap("true");
    }
}

FlowEntry *FlowTable::Allocate(const FlowKey &key) {
    FlowEntry *flow = new FlowEntry(key);
    std::pair<FlowEntryMap::iterator, bool> ret;
    ret = flow_entry_map_.insert(std::pair<FlowKey, FlowEntry*>(key, flow));
    if (ret.second == false) {
        delete flow;
        flow = ret.first->second;
        flow->set_deleted(false);
        DeleteFlowInfo(flow);
    } else {
        flow->flow_uuid = FlowTable::rand_gen_();
        flow->egress_uuid = FlowTable::rand_gen_();
        flow->setup_time = UTCTimestampUsec();
        AgentStats::GetInstance()->incr_flow_created();
    }

    return flow;
}

FlowEntry *FlowTable::Find(const FlowKey &key) {
    FlowEntryMap::iterator it;

    it = flow_entry_map_.find(key);
    if (it != flow_entry_map_.end()) {
        return it->second;
    } else {
        return NULL;
    }
}

void FlowTable::DeleteInternal(FlowEntryMap::iterator &it)
{
    FlowInfo flow_info;
    FlowEntry *fe = it->second;
    if (fe->deleted()) {
        /* Already deleted return from here. */
        return;
    }
    fe->set_deleted(true);
    fe->FillFlowInfo(flow_info);
    FLOW_TRACE(Trace, "Delete", flow_info);
    FlowTableKSyncObject *ksync_obj = 
        Agent::GetInstance()->ksync()->flowtable_ksync_obj();

    FlowStatsCollector *fec = Agent::GetInstance()->uve()->
                                  GetFlowStatsCollector();
    uint64_t diff_bytes, diff_packets;
    fec->UpdateFlowStats(fe, diff_bytes, diff_packets);

    fe->teardown_time = UTCTimestampUsec();
    fec->FlowExport(fe, diff_bytes, diff_packets);

    // Unlink the reverse flow, if one exists
    FlowEntry *rflow = fe->data.reverse_flow.get();
    if (rflow) {
        rflow->data.reverse_flow = NULL;
    }
    fe->data.reverse_flow = NULL;

    DeleteFlowInfo(fe);

    FlowTableKSyncEntry *ksync_entry = ksync_obj->Find(fe);
    KSyncEntry::KSyncEntryPtr ksync_ptr = ksync_entry;
    if (ksync_entry) {
        ksync_obj->Delete(ksync_entry);
    } else {
        FLOW_TRACE(Err, fe->flow_handle, "Entry not found in ksync");
        if (fe->data.reverse_flow.get() != NULL) {
            fe->data.reverse_flow = NULL;
        }
    }

    AgentStats::GetInstance()->incr_flow_aged();
}

bool FlowTable::DeleteRevFlow(FlowKey &key, bool rev_flow)
{   
    FlowEntryMap::iterator it;
    FlowEntryPtr pfe;

    // Find the flow, get the reverse flow and delete flow. 
    it = flow_entry_map_.find(key);
    if (it == flow_entry_map_.end()) {
        return false;
    }
    pfe = it->second;
    FlowEntryPtr reverse_flow;
    reverse_flow = pfe->data.reverse_flow;
    DeleteInternal(it);
    if (!rev_flow) {
        return true;
    }

    // If reverse flow is not present, flag an err otherwise delete // reverse flow.
    if (!reverse_flow.get()) {
        FLOW_TRACE(Err, 0, "FlowTable Error: Reverse flow doesn't exist");
        return true;
    }

    it = flow_entry_map_.find(reverse_flow.get()->key);
    if (it == flow_entry_map_.end()) {
        return false;
    }
    DeleteInternal(it);
    return true;
}

bool FlowTable::Delete(FlowEntryMap::iterator &it, bool rev_flow)
{
    FlowEntry *fe;
    FlowEntryMap::iterator rev_it;

    fe = it->second;
    FlowEntry *reverse_flow = NULL;
    if (fe->nat || rev_flow) {
        reverse_flow = fe->data.reverse_flow.get();
    }
    DeleteInternal(it);

    if (!reverse_flow) {
        return true;
    }
    /* If reverse-flow is valid and the present iterator is pointing to it,
     * use that iterator to delete reverse flow
     */
    if (reverse_flow == it->second) {
        DeleteInternal(it);
        return true;
    }

    rev_it = flow_entry_map_.find(reverse_flow->key);
    if (rev_it != flow_entry_map_.end()) {
        DeleteInternal(rev_it);
        return true;
    }
    return false;
}

bool FlowTable::DeleteNatFlow(FlowKey &key, bool del_nat_flow)
{
    FlowEntryMap::iterator it;
    FlowEntry *fe;

    it = flow_entry_map_.find(key);
    if (it == flow_entry_map_.end()) {
        return false;
    }
    fe = it->second;

    FlowEntry *reverse_flow = NULL;
    if (del_nat_flow) {
        reverse_flow = fe->data.reverse_flow.get();
    }

    /* Delete the forward flow */
    DeleteInternal(it);

    if (!reverse_flow) {
        return true;
    }

    it = flow_entry_map_.find(reverse_flow->key);
    if (it != flow_entry_map_.end()) {
        DeleteInternal(it);
        return true;
    }
    return false;
}

void FlowTable::DeleteAll()
{
    FlowEntryMap::iterator it;

    it = flow_entry_map_.begin();
    while (it != flow_entry_map_.end()) {
        FlowEntry *entry = it->second;
        ++it;
        if (it != flow_entry_map_.end() &&
            it->second == entry->data.reverse_flow.get()) {
            ++it;
        }
        DeleteNatFlow(entry->key, true);
    }
}

void FlowTable::DeleteAclFlows(const AclDBEntry *acl)
{
    AclFlowTree::iterator it;
    it = acl_flow_tree_.find(acl);
    if (it == acl_flow_tree_.end()) {
        return;
    }
    // Get the ACL flow tree
    AclFlowInfo *af_info = it->second;
    FlowEntryTree fe_tree = af_info->fet;
    FlowEntryTree::iterator fe_tree_it;
    fe_tree_it  = fe_tree.begin();
    while(fe_tree_it != fe_tree.end()) {
        FlowKey fekey = (*fe_tree_it)->key;
        ++fe_tree_it;
        DeleteNatFlow(fekey, true);
    }
}

SandeshTraceBufferPtr FlowTraceBuf(SandeshTraceBufferCreate("Flow", 5000));

void FlowTable::Init() {

    FlowEntry::alloc_count_ = 0;

    acl_listener_id_ = Agent::GetInstance()->GetAclTable()->Register
        (boost::bind(&FlowTable::AclNotify, this, _1, _2));

    intf_listener_id_ = Agent::GetInstance()->GetInterfaceTable()->Register
        (boost::bind(&FlowTable::IntfNotify, this, _1, _2));

    vn_listener_id_ = Agent::GetInstance()->GetVnTable()->Register
        (boost::bind(&FlowTable::VnNotify, this, _1, _2));

    vrf_listener_id_ = Agent::GetInstance()->GetVrfTable()->Register
            (boost::bind(&FlowTable::VrfNotify, this, _1, _2));

    nh_listener_ = new NhListener();
    return;
}

void FlowTable::Shutdown() {
}

void FlowTable::IntfNotify(DBTablePartBase *part, DBEntryBase *e) {
    // Add/Delete SG: Later
    // Change VN:
    // Resync all intf flows with new VN network policies + SG
    Interface *intf = static_cast<Interface *>(e);
    if (intf->type() != Interface::VM_INTERFACE) {
        return;
    }

    VmInterface *vm_port = static_cast<VmInterface *>(intf);
    const VnEntry *new_vn = vm_port->vn();

    DBState *s = e->GetState(part->parent(), intf_listener_id_);
    VmIntfFlowHandlerState *state = static_cast<VmIntfFlowHandlerState *>(s);

    if (intf->IsDeleted() || new_vn == NULL) {
        DeleteVmIntfFlows(intf);
        if (state) {
            e->ClearState(part->parent(), intf_listener_id_);
            delete state;
        }
        return;
    }

    const VmInterface::SecurityGroupEntryList &new_sg_l = vm_port->sg_list();
    bool changed = false;

    if (state == NULL) {
        state = new VmIntfFlowHandlerState(NULL);
        e->SetState(part->parent(), intf_listener_id_, state);
        // Force change for first time
        state->policy_ = !vm_port->policy_enabled();
        state->sg_l_ = new_sg_l;
        state->vn_ = new_vn;
        changed = true;
    } else {
        if (state->vn_.get() != new_vn) {
            changed = true;
            state->vn_ = new_vn;
        }
        if (state->policy_ != vm_port->policy_enabled()) {
            changed = true;
            state->policy_ = vm_port->policy_enabled();
        }
        if (state->sg_l_.list_ != new_sg_l.list_) {
            changed = true;
            state->sg_l_ = new_sg_l;
        }
    }

    if (changed) {
        ResyncVmPortFlows(vm_port);
    }
}

void FlowTable::VnNotify(DBTablePartBase *part, DBEntryBase *e) 
{
    // Add/Delete Acl:
    // Resync all Vn flows with new VN network policies
    VnEntry *vn = static_cast<VnEntry *>(e);
    DBState *s = e->GetState(part->parent(), vn_listener_id_);
    VnFlowHandlerState *state = static_cast<VnFlowHandlerState *>(s);
    AclDBEntryConstRef acl = NULL;
    AclDBEntryConstRef macl = NULL;
    AclDBEntryConstRef mcacl = NULL;

    if (vn->IsDeleted()) {
        DeleteVnFlows(vn);
        if (state) {
            e->ClearState(part->parent(), vn_listener_id_);
            delete state;
        }
        return;
    }

    if (state != NULL) { 
        acl = state->acl_;
        macl = state->macl_;
        mcacl = state->mcacl_;
    }

    const AclDBEntry *new_acl = vn->GetAcl();
    const AclDBEntry *new_macl = vn->GetMirrorAcl();
    const AclDBEntry *new_mcacl = vn->GetMirrorCfgAcl();
    
    if (state == NULL) {
        state = new VnFlowHandlerState(new_acl, new_macl, new_mcacl);
        e->SetState(part->parent(), vn_listener_id_, state);
    }

    if (acl != new_acl || macl != new_macl || mcacl !=new_mcacl) {
        state->acl_ = new_acl;
        state->macl_ = new_macl;
        state->mcacl_ = new_mcacl;
        ResyncVnFlows(vn);
    }
}

void FlowTable::AclNotify(DBTablePartBase *part, DBEntryBase *e) 
{
    // Delete ACL: (could be ignored), VN gets anyway notification of delete ACL.
    // Modify ACL:
    // Get VN 
    // Resync with VN network policies
    AclDBEntry *acl = static_cast<AclDBEntry *>(e);
    if (e->IsDeleted()) {
        // VN entry must have got updated and VnNotify will take care of the chnages.
        // no need to do any here.
        DeleteAclFlows(acl);
    } else {
        ResyncAclFlows(acl);
    }
}

Inet4RouteUpdate::Inet4RouteUpdate(Inet4UnicastAgentRouteTable *rt_table):
    rt_table_(rt_table), marked_delete_(false), 
    table_delete_ref_(this, rt_table->deleter()) {
}

Inet4RouteUpdate::~Inet4RouteUpdate() {
    if (rt_table_) {
        rt_table_->Unregister(id_);
    }
    table_delete_ref_.Reset(NULL);
}

void Inet4RouteUpdate::ManagedDelete() {
    marked_delete_ = true;
}

bool Inet4RouteUpdate::DeleteState(DBTablePartBase *partition,
                                   DBEntryBase *entry) {
    State *state = static_cast<State *>
                          (entry->GetState(partition->parent(), id_));
    if (state) {
        entry->ClearState(partition->parent(), id_);
        delete state;
    }
    return true;
}

void Inet4RouteUpdate::WalkDone(DBTableBase *partition,
                                Inet4RouteUpdate *rt_update) {
    delete rt_update;
}

void Inet4RouteUpdate::Unregister() {
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();
    walker->WalkTable(rt_table_, NULL,
                      boost::bind(&Inet4RouteUpdate::DeleteState, this, _1, _2),
                      boost::bind(&Inet4RouteUpdate::WalkDone, _1, this));
}

void NhListener::Notify(DBTablePartBase *part, DBEntryBase *e) {
    NextHop *nh = static_cast<NextHop *>(e);
    NhState *state = 
        static_cast<NhState *>(e->GetState(part->parent(), id_));

    if (nh->IsDeleted()) {
        if (state && state->refcount() == 0) {
            e->ClearState(part->parent(), id_);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new NhState(nh);
    }
    nh->SetState(part->parent(), id_, state);
    return; 
}

void Inet4RouteUpdate::UnicastNotify(DBTablePartBase *partition, DBEntryBase *e)
{
    Inet4UnicastRouteEntry *route = static_cast<Inet4UnicastRouteEntry *>(e);
    State *state = static_cast<State *>(e->GetState(partition->parent(), id_));

    if (route->IsMulticast()) {
        return;
    }
    
    SecurityGroupList new_sg_l;
    if (route->GetActivePath()) {
        new_sg_l = route->GetActivePath()->GetSecurityGroupList();
    }
    FLOW_TRACE(RouteUpdate, 
               route->GetVrfEntry()->GetName(), 
               route->GetIpAddress().to_string(), 
               route->GetPlen(), 
               (route->GetActivePath()) ? route->GetDestVnName() : "",
               route->IsDeleted(),
               marked_delete_,
               new_sg_l.size(),
               new_sg_l);

    // Handle delete cases
    if (marked_delete_ || route->IsDeleted()) {
        RouteFlowKey rkey(route->GetVrfEntry()->GetVrfId(),
                          route->GetIpAddress().to_ulong(), route->GetPlen());
        Agent::GetInstance()->pkt()->flow_table()->DeleteRouteFlows(rkey);
        if (state) {
            route->ClearState(partition->parent(), id_);
            delete state;
        }
        return;
    }

    if (state == NULL) {
        state  = new State();
        route->SetState(partition->parent(), id_, state);
    }

    RouteFlowKey skey(route->GetVrfEntry()->GetVrfId(), 
                      route->GetIpAddress().to_ulong(), route->GetPlen());
    sort (new_sg_l.begin(), new_sg_l.end());
    if (state->sg_l_ != new_sg_l) {
        state->sg_l_ = new_sg_l;
        Agent::GetInstance()->pkt()->flow_table()->ResyncRouteFlows(skey, new_sg_l);
    }

    //Trigger RPF NH sync, if active nexthop changes
    const NextHop *active_nh = route->GetActiveNextHop();
    const NextHop *local_nh = NULL;
    if (active_nh->GetType() == NextHop::COMPOSITE) {
        //If destination is ecmp, all remote flow would
        //have RPF NH set to that local component NH
        const CompositeNH *comp_nh = 
            static_cast<const CompositeNH *>(active_nh);
        local_nh = comp_nh->GetLocalCompositeNH();
    }

    if ((state->active_nh_ != active_nh) || (state->local_nh_ != local_nh)) {
        Agent::GetInstance()->pkt()->flow_table()->ResyncRpfNH(skey, route);
        state->active_nh_ = active_nh;
        state->local_nh_ = local_nh;
    }
}

Inet4RouteUpdate *Inet4RouteUpdate::UnicastInit(
                              Inet4UnicastAgentRouteTable *table)
{
    Inet4RouteUpdate *rt_update = new Inet4RouteUpdate(table);
    rt_update->id_ = table->Register(
        boost::bind(&Inet4RouteUpdate::UnicastNotify, rt_update, _1, _2));
    return rt_update;
}


void FlowTable::VrfNotify(DBTablePartBase *part, DBEntryBase *e)
{   
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    DBState *s = e->GetState(part->parent(), vrf_listener_id_);
    VrfFlowHandlerState *state = static_cast<VrfFlowHandlerState *>(s);
    if (vrf->IsDeleted()) {
        if (state == NULL) {
            return;
        }
        state->inet4_unicast_update_->Unregister();
        e->ClearState(part->parent(), vrf_listener_id_);
        delete state;
        return;
    }
    if (state == NULL) {
        state = new VrfFlowHandlerState();
        state->inet4_unicast_update_ = 
            Inet4RouteUpdate::UnicastInit(
            static_cast<Inet4UnicastAgentRouteTable *>(vrf->
            GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)));
        vrf->SetState(part->parent(), vrf_listener_id_, state);
    }
    return;
}

void FlowTable::ResyncVnFlows(const VnEntry *vn) {
    VnFlowTree::iterator vn_it;
    vn_it = vn_flow_tree_.find(vn);
    if (vn_it == vn_flow_tree_.end()) {
        return;
    }

    FlowEntryTree fet = vn_it->second->fet;
    FlowEntryTree::iterator fet_it, it;
    it = fet.begin();
    while (it != fet.end()) {
        fet_it = it++;
        FlowEntry *fe = (*fet_it).get();
        DeleteFlowInfo(fe);
        MatchPolicy policy;
        fe->GetPolicy(vn, &policy);
        fe->GetSgList(fe->data.intf_entry.get(), &policy);
        ResyncAFlow(fe, policy, false);
        AddFlowInfo(fe);
        FlowInfo flow_info;
        fe->FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Evaluate Vn Flows", flow_info);
    }
}

void FlowTable::ResyncAclFlows(const AclDBEntry *acl)
{
    AclFlowTree::iterator acl_it;
    acl_it = acl_flow_tree_.find(acl);
    if (acl_it == acl_flow_tree_.end()) {
        return;
    }

    FlowEntryTree fet = acl_it->second->fet;
    FlowEntryTree::iterator fet_it, it;
    it = fet.begin();
    while (it != fet.end()) {
        fet_it = it++;
        FlowEntry *fe = (*fet_it).get();
        DeleteFlowInfo(fe);
        MatchPolicy policy;
        fe->GetPolicy(fe->data.vn_entry.get(), &policy);
        fe->GetSgList(fe->data.intf_entry.get(), &policy);
        ResyncAFlow(fe, policy, false);
        AddFlowInfo(fe);
        FlowInfo flow_info;
        fe->FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Evaluate Acl Flows", flow_info);
    }
}

void FlowTable::ResyncRpfNH(const RouteFlowKey &key, 
                            const Inet4UnicastRouteEntry *rt) {
    RouteFlowTree::iterator rf_it;
    rf_it = route_flow_tree_.find(key);
    if (rf_it == route_flow_tree_.end()) {
        return;
    }
    FlowEntryTree fet = rf_it->second->fet;
    FlowEntryTree::iterator fet_it, it;
    it = fet.begin();
    while (it != fet.end()) {
        fet_it = it++;
        FlowEntry *flow = (*fet_it).get();
        if (key.FlowSrcMatch(flow) == false) {
            continue;
        }

        const NextHop *nh = rt->GetActiveNextHop();
        if (nh->GetType() == NextHop::COMPOSITE && flow->local_flow == false &&
            flow->data.ingress == true) {
            //Logic for RPF check for ecmp
            //  Get reverse flow, and its corresponding ecmp index
            //  Check if source matches component nh at reverse flow ecmp index,
            //  if not DP would trap packet for ECMP resolve.
            //  If there is only one instance of ECMP in compute node, then 
            //  RPF NH would only point to local interface NH, as if packet
            //  oringates from other source just drop the packet in dp
            const CompositeNH *comp_nh = 
                static_cast<const CompositeNH *>(nh);
            nh = comp_nh->GetLocalNextHop();
        }

        const NhState *nh_state = NULL;
        if (nh) {
            nh_state = static_cast<const NhState *>(
                    nh->GetState(Agent::GetInstance()->GetNextHopTable(),
                        Agent::GetInstance()->pkt()->flow_table()->nh_listener_id()));
        }

        if (flow->data.nh_state_ != nh_state) {
            FlowInfo flow_info;
            flow->FillFlowInfo(flow_info);
            FLOW_TRACE(Trace, "Resync RPF NH", flow_info);

            flow->data.nh_state_ = nh_state;
            FlowTableKSyncEntry *ksync_entry =
               Agent::GetInstance()->ksync()->flowtable_ksync_obj()->Find(flow);
            flow->UpdateKSync(ksync_entry, false);
        }
    }
}

void FlowTable::ResyncRouteFlows(RouteFlowKey &key, SecurityGroupList &sg_l)
{
    RouteFlowTree::iterator rf_it;
    rf_it = route_flow_tree_.find(key);
    if (rf_it == route_flow_tree_.end()) {
        return;
    }
    FlowEntryTree fet = rf_it->second->fet;
    FlowEntryTree::iterator fet_it, it;
    it = fet.begin();
    while (it != fet.end()) {
        fet_it = it++;
        FlowEntry *fe = (*fet_it).get();
        DeleteFlowInfo(fe);
        MatchPolicy policy;
        fe->GetPolicy(fe->data.vn_entry.get(), &policy);
        fe->GetSgList(fe->data.intf_entry.get(), &policy);
        if (key.FlowSrcMatch(fe)) {
            fe->data.source_sg_id_l = sg_l;
        } else if (key.FlowDestMatch(fe)) {
            fe->data.dest_sg_id_l = sg_l;
        } else {
            FLOW_TRACE(Err, fe->flow_handle, 
                       "Not found route key, vrf:"
                       + integerToString(key.vrf) 
                       + " ip:"
                       + Ip4Address(key.ip.ipv4).to_string());
        }
        ResyncAFlow(fe, policy, false);
        AddFlowInfo(fe);
        FlowInfo flow_info;
        fe->FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Evaluate Route Flows", flow_info);
    }
}

void FlowTable::ResyncVmPortFlows(const VmInterface *intf) {
    IntfFlowTree::iterator intf_it;
    intf_it = intf_flow_tree_.find(intf);
    if (intf_it == intf_flow_tree_.end()) {
        return;
    }

    FlowEntryTree fet = intf_it->second->fet;
    FlowEntryTree::iterator fet_it, it;
    it = fet.begin();
    while (it != fet.end()) {
        fet_it = it++;
        FlowEntry *fe = (*fet_it).get();
        DeleteFlowInfo(fe);
        MatchPolicy policy;
        fe->GetPolicy(intf->vn(), &policy);
        fe->GetSgList(fe->data.intf_entry.get(), &policy);
        ResyncAFlow(fe, policy, false);
        AddFlowInfo(fe);
        FlowInfo flow_info;
        fe->FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Evaluate VmPort Flows", flow_info);
    }
}


void FlowTable::DeleteRouteFlows(const RouteFlowKey &key)
{
    RouteFlowTree::iterator rf_it;
    rf_it = route_flow_tree_.find(key);
    if (rf_it == route_flow_tree_.end()) {
        return;
    }
    FLOW_TRACE(ModuleInfo, "Delete Route flows");
    FlowEntryTree fet = rf_it->second->fet;
    FlowEntryTree::iterator fet_it, it;
    it = fet.begin();
    while (it != fet.end()) {
        fet_it = it++;
        FlowEntry *fe = (*fet_it).get();
        DeleteNatFlow(fe->key, true);
    }
}

void FlowTable::DeleteFlowInfo(FlowEntry *fe) 
{
    FlowUve::GetInstance()->DeleteFlow(fe);
    // Remove from AclFlowTree
    // Go to all matched ACL list and remove from all acls
    std::list<MatchAclParams>::iterator acl_it;
    for (acl_it = fe->data.match_p.m_acl_l.begin(); acl_it != fe->data.match_p.m_acl_l.end();
         ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }
    for (acl_it = fe->data.match_p.m_sg_acl_l.begin(); 
         acl_it != fe->data.match_p.m_sg_acl_l.end();
         ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }
    for (acl_it = fe->data.match_p.m_mirror_acl_l.begin(); 
         acl_it != fe->data.match_p.m_mirror_acl_l.end();
         ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }


    for (acl_it = fe->data.match_p.m_out_acl_l.begin();
         acl_it != fe->data.match_p.m_out_acl_l.end(); ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }
    for (acl_it = fe->data.match_p.m_out_sg_acl_l.begin(); 
         acl_it != fe->data.match_p.m_out_sg_acl_l.end();
         ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }
    for (acl_it = fe->data.match_p.m_out_mirror_acl_l.begin(); 
         acl_it != fe->data.match_p.m_out_mirror_acl_l.end();
         ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }


    // Remove from IntfFlowTree
    DeleteIntfFlowInfo(fe);    
    // Remove from VnFlowTree
    DeleteVnFlowInfo(fe);
    // Remove from VmFlowTree
    // DeleteVmFlowInfo(fe);
    // Remove from RouteFlowTree
    DeleteRouteFlowInfo(fe);
}

void FlowTable::DeleteVnFlowInfo(FlowEntry *fe)
{
    VnFlowTree::iterator vn_it;
    if (fe->data.vn_entry) {
        vn_it = vn_flow_tree_.find(fe->data.vn_entry.get());
        if (vn_it != vn_flow_tree_.end()) {
            VnFlowInfo *vn_flow_info = vn_it->second;
            int count = vn_flow_info->fet.erase(fe);
            if (count > 0) {
                DecrVnFlowCounter(vn_flow_info, fe);
            }
            if (vn_flow_info->fet.empty()) {
                delete vn_flow_info;
                vn_flow_tree_.erase(vn_it);
            }
        }
    }
}

void FlowTable::DeleteAclFlowInfo(const AclDBEntry *acl, FlowEntry* flow, AclEntryIDList &id_list)
{
    AclFlowTree::iterator acl_it;
    acl_it = acl_flow_tree_.find(acl);
    if (acl_it == acl_flow_tree_.end()) {
        return;
    }

    // Delete flow entry from the Flow entry list
    AclFlowInfo *af_info = acl_it->second;
    AclEntryIDList::iterator id_it;
    for (id_it = id_list.begin(); id_it != id_list.end(); ++id_it) {
        af_info->aceid_cnt_map[*id_it] -= 1;
    }
    af_info->fet.erase(flow);
    if (af_info->fet.empty()) {
        delete af_info;
        acl_flow_tree_.erase(acl_it);
    }
}

void FlowTable::DeleteIntfFlowInfo(FlowEntry *fe)
{
    IntfFlowTree::iterator intf_it;
    if (fe->data.intf_entry) {
        intf_it = intf_flow_tree_.find(fe->data.intf_entry.get());
        if (intf_it != intf_flow_tree_.end()) {
            IntfFlowInfo *intf_flow_info = intf_it->second;
            intf_flow_info->fet.erase(fe);
            if (intf_flow_info->fet.empty()) {
                delete intf_flow_info;
                intf_flow_tree_.erase(intf_it);
            }
        }
    }
}

void FlowTable::DeleteVmFlowInfo(FlowEntry *fe)
{
    VmFlowTree::iterator vm_it;
    if (fe->data.vm_entry) {
        vm_it = vm_flow_tree_.find(fe->data.vm_entry.get());
        if (vm_it != vm_flow_tree_.end()) {
            VmFlowInfo *vm_flow_info = vm_it->second;
            vm_flow_info->fet.erase(fe);
            if (vm_flow_info->fet.empty()) {
                delete vm_flow_info;
                vm_flow_tree_.erase(vm_it);
            }
        }
    }
}

void FlowTable::DeleteRouteFlowInfo (FlowEntry *fe)
{
    RouteFlowTree::iterator rf_it;
    RouteFlowKey skey(fe->data.flow_source_vrf, fe->key.src.ipv4, 
                      fe->data.source_plen);
    rf_it = route_flow_tree_.find(skey);
    RouteFlowInfo *route_flow_info;
    if (rf_it != route_flow_tree_.end()) {
        route_flow_info = rf_it->second;
        route_flow_info->fet.erase(fe);
        if (route_flow_info->fet.empty()) {
            delete route_flow_info;
            route_flow_tree_.erase(rf_it);
        }
    }
   
    RouteFlowKey dkey(fe->data.flow_dest_vrf, fe->key.dst.ipv4,
                      fe->data.dest_plen);
    rf_it = route_flow_tree_.find(dkey);
    if (rf_it != route_flow_tree_.end()) {
        route_flow_info = rf_it->second;
        route_flow_info->fet.erase(fe);
        if (route_flow_info->fet.empty()) {
            delete route_flow_info;
            route_flow_tree_.erase(rf_it);
        }
    }
}

void FlowTable::AddFlowInfo(FlowEntry *fe)
{
    FlowUve::GetInstance()->NewFlow(fe);
    // Add AclFlowTree
    AddAclFlowInfo(fe);
    // Add IntfFlowTree
    AddIntfFlowInfo(fe);
    // Add VnFlowTree
    AddVnFlowInfo(fe);
    // Add VmFlowTree
    // AddVmFlowInfo(fe);
    // Add RouteFlowTree;
    AddRouteFlowInfo(fe);
}

void FlowTable::AddAclFlowInfo (FlowEntry *fe) 
{
    std::list<MatchAclParams>::iterator it;
    for (it = fe->data.match_p.m_acl_l.begin();
         it != fe->data.match_p.m_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }
    for (it = fe->data.match_p.m_sg_acl_l.begin();
         it != fe->data.match_p.m_sg_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }
    for (it = fe->data.match_p.m_mirror_acl_l.begin();
         it != fe->data.match_p.m_mirror_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }


    for (it = fe->data.match_p.m_out_acl_l.begin();
         it != fe->data.match_p.m_out_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }
    for (it = fe->data.match_p.m_out_sg_acl_l.begin();
         it != fe->data.match_p.m_out_sg_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }
    for (it = fe->data.match_p.m_out_mirror_acl_l.begin();
         it != fe->data.match_p.m_out_mirror_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }
}

void FlowTable::UpdateAclFlow(const AclDBEntry *acl, FlowEntry* flow,
                              AclEntryIDList &id_list)
{
    AclFlowTree::iterator it;
    pair<set<FlowEntryPtr>::iterator,bool> ret;

    it = acl_flow_tree_.find(acl);

    AclFlowInfo *af_info;
    if (it == acl_flow_tree_.end()) {
        af_info = new AclFlowInfo;
        af_info->acl_entry = acl;
        ret = af_info->fet.insert(flow);
        acl_flow_tree_.insert(AclFlowPair(acl, af_info));
    } else {
        af_info = it->second;
        /* flow can already exist. In that case it won't be inserted */
        ret = af_info->fet.insert(flow);
    }
    
    if (id_list.size()) {
        AclEntryIDList::iterator id_it;
        for (id_it = id_list.begin(); id_it != id_list.end(); ++id_it) {
            af_info->aceid_cnt_map[*id_it] += 1;
        }        
    } else {
        af_info->flow_miss++;
    }
}

void FlowTable::AddIntfFlowInfo (FlowEntry *fe)
{
    if (!fe->data.intf_entry) {
        return;
    }
    IntfFlowTree::iterator it;
    it = intf_flow_tree_.find(fe->data.intf_entry.get());
    IntfFlowInfo *intf_flow_info;
    if (it == intf_flow_tree_.end()) {
        intf_flow_info = new IntfFlowInfo();
        intf_flow_info->intf_entry = fe->data.intf_entry;
        intf_flow_info->fet.insert(fe);
        intf_flow_tree_.insert(IntfFlowPair(fe->data.intf_entry.get(), intf_flow_info));
    } else {
        intf_flow_info = it->second;
        /* fe can already exist. In that case it won't be inserted */
        intf_flow_info->fet.insert(fe);
    }
}

void FlowTable::AddVmFlowInfo (FlowEntry *fe)
{
    if (!fe->data.vm_entry) {
        return;
    }
    VmFlowTree::iterator it;
    it = vm_flow_tree_.find(fe->data.vm_entry.get());
    VmFlowInfo *vm_flow_info;
    if (it == vm_flow_tree_.end()) {
        vm_flow_info = new VmFlowInfo();
        vm_flow_info->vm_entry = fe->data.vm_entry;
        vm_flow_info->fet.insert(fe);
        vm_flow_tree_.insert(VmFlowPair(fe->data.vm_entry.get(), vm_flow_info));
    } else {
        vm_flow_info = it->second;
        /* fe can already exist. In that case it won't be inserted */
        vm_flow_info->fet.insert(fe);
    }
}

void FlowTable::IncrVnFlowCounter(VnFlowInfo *vn_flow_info, 
                                  const FlowEntry *fe) {
    if (fe->local_flow) {
        vn_flow_info->ingress_flow_count++;
        vn_flow_info->egress_flow_count++;
    } else {
        if (fe->data.ingress) {
            vn_flow_info->ingress_flow_count++;
        } else {
            vn_flow_info->egress_flow_count++;
        }
    }
}

void FlowTable::DecrVnFlowCounter(VnFlowInfo *vn_flow_info, 
                                  const FlowEntry *fe) {
    if (fe->local_flow) {
        vn_flow_info->ingress_flow_count--;
        vn_flow_info->egress_flow_count--;
    } else {
        if (fe->data.ingress) {
            vn_flow_info->ingress_flow_count--;
        } else {
            vn_flow_info->egress_flow_count--;
        }
    }
}

void FlowTable::AddVnFlowInfo (FlowEntry *fe)
{
    if (!fe->data.vn_entry) {
        return;
    }    
    VnFlowTree::iterator it;
    it = vn_flow_tree_.find(fe->data.vn_entry.get());
    VnFlowInfo *vn_flow_info;
    if (it == vn_flow_tree_.end()) {
        vn_flow_info = new VnFlowInfo();
        vn_flow_info->vn_entry = fe->data.vn_entry;
        vn_flow_info->fet.insert(fe);
        IncrVnFlowCounter(vn_flow_info, fe);
        vn_flow_tree_.insert(VnFlowPair(fe->data.vn_entry.get(), vn_flow_info));
    } else {
        vn_flow_info = it->second;
        /* fe can already exist. In that case it won't be inserted */
        pair<FlowTable::FlowEntryTree::iterator, bool> ret = 
                                            vn_flow_info->fet.insert(fe);
        if (ret.second) {
            IncrVnFlowCounter(vn_flow_info, fe);
        }
    }
}

void FlowTable::VnFlowCounters(const VnEntry *vn, uint32_t *in_count, 
                               uint32_t *out_count) {
    VnFlowTree::iterator it;
    it = vn_flow_tree_.find(vn);
    if (it == vn_flow_tree_.end()) {
        *in_count = 0;
        *out_count = 0;
        return;
    }
    VnFlowInfo *vn_flow_info = it->second;
    *in_count = vn_flow_info->ingress_flow_count;
    *out_count = vn_flow_info->egress_flow_count;
}

void FlowTable::AddRouteFlowInfo (FlowEntry *fe)
{
    RouteFlowTree::iterator it;
    RouteFlowInfo *route_flow_info;
    if (fe->data.flow_source_vrf != VrfEntry::kInvalidIndex) {
        RouteFlowKey skey(fe->data.flow_source_vrf, fe->key.src.ipv4,
                          fe->data.source_plen);
        it = route_flow_tree_.find(skey);
        if (it == route_flow_tree_.end()) {
            route_flow_info = new RouteFlowInfo();
            route_flow_info->fet.insert(fe);
            route_flow_tree_.insert(RouteFlowPair(skey, route_flow_info));
        } else {
            route_flow_info = it->second;
            route_flow_info->fet.insert(fe);
        }
    }

    if (fe->data.flow_dest_vrf != VrfEntry::kInvalidIndex) {
        RouteFlowKey dkey(fe->data.flow_dest_vrf, fe->key.dst.ipv4, 
                          fe->data.dest_plen);
        it = route_flow_tree_.find(dkey);
        if (it == route_flow_tree_.end()) {
            route_flow_info = new RouteFlowInfo();
            route_flow_info->fet.insert(fe);
            route_flow_tree_.insert(RouteFlowPair(dkey, route_flow_info));
        } else {
            route_flow_info = it->second;
            route_flow_info->fet.insert(fe);
        }
    }
}

void FlowTable::ResyncAFlow(FlowEntry *fe, MatchPolicy &policy, bool create) {
    PacketHeader hdr;
    hdr.vrf = fe->key.vrf; hdr.src_ip = fe->key.src.ipv4;
    hdr.dst_ip = fe->key.dst.ipv4;
    hdr.protocol = fe->key.protocol;
    if (hdr.protocol == IPPROTO_UDP || hdr.protocol == IPPROTO_TCP) {
        hdr.src_port = fe->key.src_port;
        hdr.dst_port = fe->key.dst_port;
    } else {
        hdr.src_port = 0;
        hdr.dst_port = 0;
    }
    hdr.src_policy_id = &(fe->data.source_vn);
    hdr.dst_policy_id = &(fe->data.dest_vn);
    hdr.src_sg_id_l = &(fe->data.source_sg_id_l);
    hdr.dst_sg_id_l = &(fe->data.dest_sg_id_l);

    fe->DoPolicy(hdr, &policy, fe->data.ingress);
    fe->CompareAndModify(policy, create);

    // If this is forward flow, update the SG action for reflexive entry
    if (fe->is_reverse_flow) {
        return;
    }

    FlowEntry *rflow = fe->data.reverse_flow.get();
    if (rflow == NULL) {
        return;
    }

    rflow->data.match_p.sg_action = ReflexiveAction(fe->data.match_p.sg_action);
    rflow->data.match_p.out_sg_action =
        ReflexiveAction(fe->data.match_p.out_sg_action);
    // Check if there is change in action for reverse flow
    rflow->ActionRecompute(&rflow->data.match_p);

    FlowTableKSyncEntry *entry = 
        Agent::GetInstance()->ksync()->flowtable_ksync_obj()->Find(rflow);
    if (entry) {
        rflow->UpdateKSync(entry, false);
    }
}

void FlowTable::DeleteVnFlows(const VnEntry *vn)
{
    VnFlowTree::iterator vn_it;
    vn_it = vn_flow_tree_.find(vn);
    if (vn_it == vn_flow_tree_.end()) {
        return;
    }
    FLOW_TRACE(ModuleInfo, "Delete Vn Flows");
    FlowEntryTree fet = vn_it->second->fet;
    FlowEntryTree::iterator fet_it;
    for (fet_it = fet.begin(); fet_it != fet.end(); ++fet_it) {
        DeleteNatFlow((*fet_it)->key, true);
    }
}

void FlowTable::DeleteVmFlows(const VmEntry *vm)
{
    VmFlowTree::iterator vm_it;
    vm_it = vm_flow_tree_.find(vm);
    if (vm_it == vm_flow_tree_.end()) {
        return;
    }
    FLOW_TRACE(ModuleInfo, "Delete VM flows");
    FlowEntryTree fet = vm_it->second->fet;
    FlowEntryTree::iterator fet_it;
    for (fet_it = fet.begin(); fet_it != fet.end(); ++fet_it) {
        DeleteNatFlow((*fet_it)->key, true);
    }
}

void FlowTable::DeleteVmIntfFlows(const Interface *intf)
{
    IntfFlowTree::iterator intf_it;
    intf_it = intf_flow_tree_.find(intf);
    if (intf_it == intf_flow_tree_.end()) {
        return;
    }
    FLOW_TRACE(ModuleInfo, "Delete Interface Flows");
    FlowEntryTree fet = intf_it->second->fet;
    FlowEntryTree::iterator fet_it;
    for (fet_it = fet.begin(); fet_it != fet.end(); ++fet_it) {
        DeleteNatFlow((*fet_it)->key, true);
    }
}

DBTableBase::ListenerId FlowTable::nh_listener_id() {
    return nh_listener_->id();
}

void SetActionStr(const FlowAction &action_info, std::vector<ActionStr> &action_str_l)
{
    std::bitset<32> bs(action_info.action);
    for (unsigned int i = 0; i <= bs.size(); i++) {
        if (bs[i]) {
            ActionStr astr;
            astr.action = TrafficAction::ActionToString((TrafficAction::Action)i);
            action_str_l.push_back(astr);
            if ((TrafficAction::Action)i == TrafficAction::MIRROR) {
                std::vector<MirrorActionSpec>::const_iterator m_it;
                for (m_it = action_info.mirror_l.begin();
                     m_it != action_info.mirror_l.end();
                     ++m_it) {
                    ActionStr mstr;
                    mstr.action += (*m_it).ip.to_string();
                    mstr.action += " ";
                    mstr.action += integerToString((*m_it).port);
                    mstr.action += " ";
                    mstr.action += (*m_it).vrf_name;
                    mstr.action += " ";
                    mstr.action += (*m_it).encap;
                    action_str_l.push_back(mstr);
                }
            }
        }
    }
}

static void SetAclListAclAction(const std::list<MatchAclParams> &acl_l, std::vector<AclAction> &acl_action_l,
                         std::string &acl_type) {
    std::list<MatchAclParams>::const_iterator it;
    for(it = acl_l.begin(); it != acl_l.end(); ++it) {
        AclAction acl_action;
        acl_action.set_acl_id(UuidToString((*it).acl->GetUuid()));
        acl_action.set_acl_type(acl_type);
        std::vector<ActionStr> action_str_l;
        SetActionStr((*it).action_info, action_str_l);
        acl_action.set_action_l(action_str_l);
        acl_action_l.push_back(acl_action);
    }
}

static void SetAclAction(const FlowEntry &fe, std::vector<AclAction> &acl_action_l)
{
    const std::list<MatchAclParams> &acl_l = fe.data.match_p.m_acl_l;
    std::string acl_type("nw policy");
    SetAclListAclAction(acl_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &sg_acl_l = fe.data.match_p.m_sg_acl_l;
    acl_type = "sg";
    SetAclListAclAction(sg_acl_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &m_acl_l = fe.data.match_p.m_mirror_acl_l;
    acl_type = "dynamic";
    SetAclListAclAction(m_acl_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &out_acl_l = fe.data.match_p.m_out_acl_l;
    acl_type = "o nw policy";
    SetAclListAclAction(out_acl_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &out_sg_acl_l = fe.data.match_p.m_out_sg_acl_l;
    acl_type = "o sg";
    SetAclListAclAction(out_sg_acl_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &out_m_acl_l = fe.data.match_p.m_out_mirror_acl_l;
    acl_type = "o dynamic";
    SetAclListAclAction(out_m_acl_l, acl_action_l, acl_type);
}

string FlowTable::GetAceSandeshDataKey(const AclDBEntry *acl, int ace_id) {
    string uuid_str = UuidToString(acl->GetUuid());
    stringstream ss;
    ss << uuid_str << ":";
    ss << ace_id;
    return ss.str();
}

void FlowTable::SetAceSandeshData(const AclDBEntry *acl, AclFlowCountResp &data, int ace_id)
{
    AclFlowTree::iterator it;
    it = acl_flow_tree_.find(acl);
    if (it == acl_flow_tree_.end()) {
        return;
    }
    int count = 0;
    bool key_set = false;
    AclFlowInfo *af_info = it->second;

    AceIdFlowCntMap *aceid_cnt = &(af_info->aceid_cnt_map);
    FlowTable::AceIdFlowCntMap::iterator aceid_it = aceid_cnt->upper_bound(ace_id);
    std::vector<AceIdFlowCnt> id_cnt_l;
    while (aceid_it != aceid_cnt->end()) {
        AceIdFlowCnt id_cnt_s;
        id_cnt_s.ace_id = aceid_it->first;
        id_cnt_s.flow_cnt = aceid_it->second;
        id_cnt_l.push_back(id_cnt_s);
        count++;
        ++aceid_it;
        if (count == MaxResponses && aceid_it != aceid_cnt->end()) {
            data.set_iteration_key(GetAceSandeshDataKey(acl, id_cnt_s.ace_id));
            key_set = true;
            break;
        }
    }
    data.set_aceid_cnt_list(id_cnt_l);
    
    FlowEntryTree *fe_tree = &(af_info->fet);    
    data.set_flow_count(fe_tree->size());
    data.set_flow_miss(af_info->flow_miss);

    if (!key_set) {
        data.set_iteration_key(GetAceSandeshDataKey(acl, 0));
    }
}

string FlowTable::GetAclFlowSandeshDataKey(const AclDBEntry *acl, const int last_count) {
    string uuid_str = UuidToString(acl->GetUuid());
    stringstream ss;
    ss << uuid_str << ":";
    ss << last_count;
    return ss.str();
}

static void SetAclListAceId(const AclDBEntry *acl, const std::list<MatchAclParams> &acl_l,
                            std::vector<AceId> &ace_l) {
    std::list<MatchAclParams>::const_iterator ma_it;
    for (ma_it = acl_l.begin();
         ma_it != acl_l.end();
         ++ma_it) {
        if ((*ma_it).acl != acl) {
            continue;
        }
        AclEntryIDList::const_iterator ait;
        for (ait = (*ma_it).ace_id_list.begin(); 
             ait != (*ma_it).ace_id_list.end(); ++ ait) {
            AceId ace_id;
            ace_id.id = *ait;
            ace_l.push_back(ace_id);
        }
    }
}

void FlowTable::SetAclFlowSandeshData(const AclDBEntry *acl, AclFlowResp &data, 
                                      const int last_count)
{
    AclFlowTree::iterator it;
    it = acl_flow_tree_.find(acl);
    if (it == acl_flow_tree_.end()) {
        return;
    }
    AclFlowInfo *af_info = it->second;
   
    int count = 0; 
    bool key_set = false;
    FlowEntryTree *fe_tree = &(af_info->fet);    
    FlowEntryTree::iterator fe_tree_it = fe_tree->begin();
    while (fe_tree_it != fe_tree->end() && (count + 1) < last_count) {
        fe_tree_it++;
        count++;
    }
    data.set_flow_count(fe_tree->size());
    data.set_flow_miss(af_info->flow_miss);
    std::vector<FlowSandeshData> flow_entries_l;
    while(fe_tree_it != fe_tree->end()) {
        const FlowEntry &fe = *(*fe_tree_it);
        FlowSandeshData fe_sandesh_data;
        fe_sandesh_data.set_vrf(integerToString(fe.key.vrf));
        fe_sandesh_data.set_src(Ip4Address(fe.key.src.ipv4).to_string());
        fe_sandesh_data.set_dst(Ip4Address(fe.key.dst.ipv4).to_string());
        fe_sandesh_data.set_src_port(fe.key.src_port);
        fe_sandesh_data.set_dst_port(fe.key.dst_port);
        fe_sandesh_data.set_protocol(fe.key.protocol);
        fe_sandesh_data.set_ingress(fe.data.ingress);
        std::vector<ActionStr> action_str_l;
        SetActionStr(fe.data.match_p.action_info, action_str_l);
        fe_sandesh_data.set_action_l(action_str_l);
        
        std::vector<AclAction> acl_action_l;
        SetAclAction(fe, acl_action_l);
        fe_sandesh_data.set_acl_action_l(acl_action_l);

        fe_sandesh_data.set_flow_uuid(UuidToString(fe.flow_uuid));
        fe_sandesh_data.set_flow_handle(integerToString(fe.flow_handle));
        fe_sandesh_data.set_source_vn(fe.data.source_vn);
        fe_sandesh_data.set_dest_vn(fe.data.dest_vn);
        std::vector<uint32_t> v;
        SecurityGroupList::const_iterator it;
        for (it = fe.data.source_sg_id_l.begin(); 
             it != fe.data.source_sg_id_l.end(); it++) {
            v.push_back(*it);
        }
        fe_sandesh_data.set_source_sg_id_l(v);
        v.clear();
        for (it = fe.data.dest_sg_id_l.begin(); 
             it != fe.data.dest_sg_id_l.end(); it++) {
            v.push_back(*it);
        }
        fe_sandesh_data.set_dest_sg_id_l(v);
        fe_sandesh_data.set_bytes(integerToString(fe.data.bytes));
        fe_sandesh_data.set_packets(integerToString(fe.data.packets));
        fe_sandesh_data.set_setup_time(
                            integerToString(UTCUsecToPTime(fe.setup_time)));
        fe_sandesh_data.set_setup_time_utc(fe.setup_time);
        if (fe.teardown_time) {
            fe_sandesh_data.set_teardown_time(
                integerToString(UTCUsecToPTime(fe.teardown_time)));
        } else {
            fe_sandesh_data.set_teardown_time("");
        }
        fe_sandesh_data.set_current_time(integerToString(
                                         UTCUsecToPTime(UTCTimestampUsec())));
        
        SetAclListAceId(acl, fe.data.match_p.m_acl_l, fe_sandesh_data.ace_l);
        SetAclListAceId(acl, fe.data.match_p.m_sg_acl_l, fe_sandesh_data.ace_l);
        SetAclListAceId(acl, fe.data.match_p.m_mirror_acl_l, fe_sandesh_data.ace_l);
        SetAclListAceId(acl, fe.data.match_p.m_out_acl_l, fe_sandesh_data.ace_l);
        SetAclListAceId(acl, fe.data.match_p.m_out_sg_acl_l, fe_sandesh_data.ace_l);
        SetAclListAceId(acl, fe.data.match_p.m_out_mirror_acl_l, fe_sandesh_data.ace_l);

        if (fe.data.reverse_flow.get()) {
            fe_sandesh_data.set_reverse_flow("yes");
        } else {
            fe_sandesh_data.set_reverse_flow("no");
        }
        if (fe.nat) {
            fe_sandesh_data.set_nat("yes");
        } else {
            fe_sandesh_data.set_nat("no");
        }
        if (fe.ImplicitDenyFlow()) {
            fe_sandesh_data.set_implicit_deny("yes");
        } else {
            fe_sandesh_data.set_implicit_deny("no");
        }
        if (fe.ShortFlow()) {
            fe_sandesh_data.set_short_flow("yes");
        } else {
            fe_sandesh_data.set_short_flow("no");
        }
    
        flow_entries_l.push_back(fe_sandesh_data);
        count++;
        ++fe_tree_it;
        if (count == (MaxResponses + last_count) && fe_tree_it != fe_tree->end()) {
            data.set_iteration_key(GetAclFlowSandeshDataKey(acl, count));
            key_set = true;
            break;
        }
    }
    data.set_flow_entries(flow_entries_l);
    if (!key_set) {
        data.set_iteration_key(GetAclFlowSandeshDataKey(acl, 0));
    }
}

FlowTable::~FlowTable() {
    Agent::GetInstance()->GetAclTable()->Unregister(acl_listener_id_);
    Agent::GetInstance()->GetInterfaceTable()->Unregister(intf_listener_id_);
    Agent::GetInstance()->GetVnTable()->Unregister(vn_listener_id_);
    Agent::GetInstance()->GetVmTable()->Unregister(vm_listener_id_);
    Agent::GetInstance()->GetVrfTable()->Unregister(vrf_listener_id_);
    delete nh_listener_;
}

