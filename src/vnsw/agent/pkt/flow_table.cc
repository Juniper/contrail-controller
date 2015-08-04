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
#include "base/os.h"

#include "route/route.h"
#include "cmn/agent_cmn.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"

#include "init/agent_param.h"
#include "cmn/agent_cmn.h"
#include "oper/route_common.h"
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
#include "pkt/pkt_sandesh_flow.h"
#include "pkt/agent_stats.h"
#include "pkt/flow_mgmt.h"
#include "pkt/flow_mgmt_response.h"
#include "uve/agent_uve.h"
#include "uve/vm_uve_table.h"
#include "uve/vn_uve_table.h"
#include "uve/vrouter_uve_entry.h"

const string FlowTable::kTaskName = "Agent::FlowHandler";
using boost::assign::map_list_of;
const std::map<FlowEntry::FlowPolicyState, const char*>
    FlowEntry::FlowPolicyStateStr = map_list_of
                            (NOT_EVALUATED, "00000000-0000-0000-0000-000000000000")
                            (IMPLICIT_ALLOW, "00000000-0000-0000-0000-000000000001")
                            (IMPLICIT_DENY, "00000000-0000-0000-0000-000000000002")
                            (DEFAULT_GW_ICMP_OR_DNS, "00000000-0000-0000-0000-000000000003")
                            (LINKLOCAL_FLOW, "00000000-0000-0000-0000-000000000004")
                            (MULTICAST_FLOW, "00000000-0000-0000-0000-000000000005")
                            (NON_IP_FLOW,    "00000000-0000-0000-0000-000000000006");

boost::uuids::random_generator FlowTable::rand_gen_ = boost::uuids::random_generator();
tbb::atomic<int> FlowEntry::alloc_count_;
SecurityGroupList FlowTable::default_sg_list_;

static bool ShouldDrop(uint32_t action) {
    if ((action & TrafficAction::DROP_FLAGS) || (action & TrafficAction::IMPLICIT_DENY_FLAGS))
        return true;

    return false;
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

FlowEntry::FlowEntry(const FlowKey &k) : 
    key_(k), data_(), stats_(), l3_flow_(true),
    flow_handle_(kInvalidFlowHandle),
    ksync_entry_(NULL), deleted_(false), flags_(0),
    short_flow_reason_(SHORT_UNKNOWN),
    linklocal_src_port_(),
    linklocal_src_port_fd_(PktFlowInfo::kLinkLocalInvalidFd),
    peer_vrouter_(), tunnel_type_(TunnelType::INVALID),
    underlay_source_port_(0), underlay_sport_exported_(false) {
    flow_uuid_ = FlowTable::rand_gen_(); 
    egress_uuid_ = FlowTable::rand_gen_(); 
    refcount_ = 0;
    nw_ace_uuid_ = FlowPolicyStateStr.at(NOT_EVALUATED);
    sg_rule_uuid_= FlowPolicyStateStr.at(NOT_EVALUATED);
    alloc_count_.fetch_and_increment();
}

void FlowEntry::GetSourceRouteInfo(const AgentRoute *rt) {
    const AgentPath *path = NULL;
    if (rt) {
        path = rt->GetActivePath();
    }
    if (path == NULL) {
        data_.source_vn = FlowHandler::UnknownVn();
        data_.source_sg_id_l = FlowTable::default_sg_list();
        data_.source_plen = 0;
    } else {
        data_.source_vn = path->dest_vn_name();
        data_.source_sg_id_l = path->sg_list();
        data_.source_plen = rt->plen();
    }
}

void FlowEntry::GetDestRouteInfo(const AgentRoute *rt) {
    const AgentPath *path = NULL;
    if (rt) {
        path = rt->GetActivePath();
    }

    if (path == NULL) {
        data_.dest_vn = FlowHandler::UnknownVn();
        data_.dest_sg_id_l = FlowTable::default_sg_list();
        data_.dest_plen = 0;
    } else {
        data_.dest_vn = path->dest_vn_name();
        data_.dest_sg_id_l = path->sg_list();
        data_.dest_plen = rt->plen();
    }
}

uint32_t FlowEntry::MatchAcl(const PacketHeader &hdr,
                             std::list<MatchAclParams> &acl,
                             bool add_implicit_deny, bool add_implicit_allow,
                             FlowPolicyInfo *info) {
    PktHandler *pkt_handler = Agent::GetInstance()->pkt()->pkt_handler();

    // If there are no ACL to match, make it pass
    if (acl.size() == 0 &&  add_implicit_allow) {
        if (info) {
            /* We are setting UUIDs for linklocal and multicast flows here,
             * because even if we move this to the place where acl association
             * is being skipped, we still need checks for linklocal and
             * multicast flows here to avoid its value being overwritten with
             * IMPLICIT_ALLOW
             */
            if (is_flags_set(FlowEntry::LinkLocalFlow)) {
                info->uuid = FlowPolicyStateStr.at(LINKLOCAL_FLOW);
            } else if (is_flags_set(FlowEntry::Multicast)) {
                info->uuid = FlowPolicyStateStr.at(MULTICAST_FLOW);
            } else {
                /* We need to make sure that info is not already populated
                 * before setting it to IMPLICIT_ALLOW. This is required
                 * because info could earlier be set by previous call to
                 * MatchAcl. We should note here that same 'info' var is passed
                 * for MatchAcl calls with in_acl and out_acl
                 */
                if (!info->terminal && !info->other) {
                    info->uuid = FlowPolicyStateStr.at(IMPLICIT_ALLOW);
                }
            }
        }
        return (1 << TrafficAction::PASS);
    }

    // PASS default GW traffic, if it is ICMP or DNS
    if ((hdr.protocol == IPPROTO_ICMP ||
         (hdr.protocol == IPPROTO_UDP && 
          (hdr.src_port == DNS_SERVER_PORT ||
           hdr.dst_port == DNS_SERVER_PORT))) &&
        (pkt_handler->IsGwPacket(data_.intf_entry.get(), hdr.dst_ip) ||
         pkt_handler->IsGwPacket(data_.intf_entry.get(), hdr.src_ip))) {
        if (info) {
            info->uuid = FlowPolicyStateStr.at(DEFAULT_GW_ICMP_OR_DNS);
        }
        return (1 << TrafficAction::PASS);
    }

    uint32_t action = 0;
    for (std::list<MatchAclParams>::iterator it = acl.begin();
         it != acl.end(); ++it) {
        if (it->acl.get() == NULL) {
            continue;
        }

        if (it->acl->PacketMatch(hdr, *it, info)) {
            action |= it->action_info.action;
            if (it->action_info.action & (1 << TrafficAction::MIRROR)) {
                data_.match_p.action_info.mirror_l.insert
                    (data_.match_p.action_info.mirror_l.end(),
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
        if (info) {
            info->uuid = FlowPolicyStateStr.at(IMPLICIT_DENY);
            info->drop = true;
        }
    }

    return action;
}

void FlowEntry::ResetStats() {
    stats_.bytes = 0;
    stats_.packets = 0;
}

// Recompute FlowEntry action
bool FlowEntry::ActionRecompute() {
    uint32_t action = 0;

    action = data_.match_p.policy_action | data_.match_p.out_policy_action |
        data_.match_p.sg_action_summary |
        data_.match_p.mirror_action | data_.match_p.out_mirror_action;

    //Only VRF assign acl, can specify action to
    //translate VRF. VRF translate action specified
    //by egress VN ACL or ingress VN ACL should be ignored
    action &= ~(1 << TrafficAction::VRF_TRANSLATE);
    action |= data_.match_p.vrf_assign_acl_action;

    if (action & (1 << TrafficAction::VRF_TRANSLATE) && 
        data_.match_p.action_info.vrf_translate_action_.ignore_acl() == true) {
        //In case of multi inline service chain, match condition generated on
        //each of service instance interface takes higher priority than
        //network ACL. Match condition on the interface would have ignore acl flag
        //set to avoid applying two ACL for vrf translation
        action = data_.match_p.vrf_assign_acl_action |
            data_.match_p.sg_action_summary | data_.match_p.mirror_action |
            data_.match_p.out_mirror_action;

        //Pick mirror action from network ACL
        if (data_.match_p.policy_action & (1 << TrafficAction::MIRROR) ||
            data_.match_p.out_policy_action & (1 << TrafficAction::MIRROR)) {
            action |= (1 << TrafficAction::MIRROR);
        }
    }

    // Force short flows to DROP
    if (is_flags_set(FlowEntry::ShortFlow)) {
        action |= (1 << TrafficAction::DROP);
    }

    // check for conflicting actions and remove allowed action
    if (ShouldDrop(action)) {
        action = (action & ~TrafficAction::DROP_FLAGS & ~TrafficAction::PASS_FLAGS);
        action |= (1 << TrafficAction::DROP);
        if (is_flags_set(FlowEntry::ShortFlow)) {
            data_.drop_reason = short_flow_reason_;
        } else if (ShouldDrop(data_.match_p.policy_action)) {
            data_.drop_reason = DROP_POLICY;
        } else if (ShouldDrop(data_.match_p.out_policy_action)){
            data_.drop_reason = DROP_OUT_POLICY;
        } else if (ShouldDrop(data_.match_p.sg_action)){
            data_.drop_reason = DROP_SG;
        } else if (ShouldDrop(data_.match_p.out_sg_action)){
            data_.drop_reason = DROP_OUT_SG;
        } else if (ShouldDrop(data_.match_p.reverse_sg_action)){
            data_.drop_reason = DROP_REVERSE_SG;
        } else if (ShouldDrop(data_.match_p.reverse_out_sg_action)){
            data_.drop_reason = DROP_REVERSE_OUT_SG;
        } else {
            data_.drop_reason = DROP_UNKNOWN;
        }
    }

    if (action & (1 << TrafficAction::TRAP)) {
        action = (1 << TrafficAction::TRAP);
    }

    if (action != data_.match_p.action_info.action) {
        data_.match_p.action_info.action = action;
        return true;
    }

    return false;
}

void FlowEntry::SetPacketHeader(PacketHeader *hdr) {
    hdr->vrf = data_.vrf;
    hdr->src_ip = key_.src_addr;
    hdr->dst_ip = key_.dst_addr;
    hdr->protocol = key_.protocol;
    if (hdr->protocol == IPPROTO_UDP || hdr->protocol == IPPROTO_TCP) {
        hdr->src_port = key_.src_port;
        hdr->dst_port = key_.dst_port;
    } else {
        hdr->src_port = 0;
        hdr->dst_port = 0;
    }
    hdr->src_policy_id = &(data_.source_vn);
    hdr->dst_policy_id = &(data_.dest_vn);
    hdr->src_sg_id_l = &(data_.source_sg_id_l);
    hdr->dst_sg_id_l = &(data_.dest_sg_id_l);
}


// In case of NAT flows, the key fields can change.
void FlowEntry::SetOutPacketHeader(PacketHeader *hdr) {
    FlowEntry *rflow = reverse_flow_entry();
    if (rflow == NULL)
        return;

    hdr->vrf = rflow->data().vrf;
    hdr->src_ip = rflow->key().dst_addr;
    hdr->dst_ip = rflow->key().src_addr;
    hdr->protocol = rflow->key().protocol;
    if (hdr->protocol == IPPROTO_UDP || hdr->protocol == IPPROTO_TCP) {
        hdr->src_port = rflow->key().dst_port;
        hdr->dst_port = rflow->key().src_port;
    } else {
        hdr->src_port = 0;
        hdr->dst_port = 0;
    }
    hdr->src_policy_id = &(rflow->data().dest_vn);
    hdr->dst_policy_id = &(rflow->data().source_vn);
    hdr->src_sg_id_l = &(rflow->data().dest_sg_id_l);
    hdr->dst_sg_id_l = &(rflow->data().source_sg_id_l);
}

void FlowEntry::UpdateRpf() {
    if (data_.vn_entry) {
        data_.enable_rpf = data_.vn_entry->enable_rpf();
    } else {
        data_.enable_rpf = true;
    }
}
// Apply Policy and SG rules for a flow.
//
// Special case of local flows:
//     For local-flows, both VM are on same compute and we need to apply SG from
//     both the ports. m_sg_acl_l will contain ACL for port in forward flow and
//     m_out_sg_acl_l will have ACL from other port
//
//     If forward flow goes thru NAT, the key for matching ACL in 
//     m_out_sg_acl_l can potentially change. The routine SetOutPacketHeader
//     takes care of forming header after NAT
//
// Rules applied are based on flow type
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
bool FlowEntry::DoPolicy() {
    data_.match_p.action_info.Clear();
    data_.match_p.policy_action = 0;
    data_.match_p.out_policy_action = 0;
    data_.match_p.sg_action = 0;
    data_.match_p.out_sg_action = 0;
    data_.match_p.reverse_sg_action = 0;
    data_.match_p.reverse_out_sg_action = 0;
    data_.match_p.mirror_action = 0;
    data_.match_p.out_mirror_action = 0;
    data_.match_p.sg_action_summary = 0;
    const string value = FlowPolicyStateStr.at(NOT_EVALUATED);
    FlowPolicyInfo nw_acl_info(value), sg_acl_info(value);
    FlowPolicyInfo rev_sg_acl_info(value);

    FlowEntry *rflow = reverse_flow_entry();
    PacketHeader hdr;
    SetPacketHeader(&hdr);

    //Calculate VRF assign entry, and ignore acl is set
    //skip network and SG acl action is set
    data_.match_p.vrf_assign_acl_action =
        MatchAcl(hdr, data_.match_p.m_vrf_assign_acl_l, false, true, NULL);

    // Mirror is valid even if packet is to be dropped. So, apply it first
    data_.match_p.mirror_action = MatchAcl(hdr, data_.match_p.m_mirror_acl_l,
                                           false, true, NULL);

    // Apply out-policy. Valid only for local-flow
    data_.match_p.out_mirror_action = MatchAcl(hdr,
                           data_.match_p.m_out_mirror_acl_l, false, true, NULL);

    // Apply network policy
    data_.match_p.policy_action = MatchAcl(hdr, data_.match_p.m_acl_l, true,
                                           true, &nw_acl_info);
    if (ShouldDrop(data_.match_p.policy_action)) {
        goto done;
    }
    data_.match_p.out_policy_action = MatchAcl(hdr, data_.match_p.m_out_acl_l,
                                               true, true, &nw_acl_info);
    if (ShouldDrop(data_.match_p.policy_action)) {
        goto done;
    }

    // Apply security-group
    if (!is_flags_set(FlowEntry::ReverseFlow)) {
        data_.match_p.sg_action = MatchAcl(hdr, data_.match_p.m_sg_acl_l, true,
                                           !data_.match_p.sg_rule_present,
                                           &sg_acl_info);

        PacketHeader out_hdr;
        if (ShouldDrop(data_.match_p.sg_action) == false && rflow) {
            // Key fields for lookup in out-acl can potentially change in case 
            // of NAT. Form ACL lookup based on post-NAT fields
            SetOutPacketHeader(&out_hdr);
            data_.match_p.out_sg_action =
                MatchAcl(out_hdr, data_.match_p.m_out_sg_acl_l, true,
                         !data_.match_p.out_sg_rule_present, &sg_acl_info);
        }

        // For TCP-ACK packet, we allow packet if either forward or reverse
        // flow says allow. So, continue matching reverse flow even if forward
        // flow says drop
        if (is_flags_set(FlowEntry::TcpAckFlow) && rflow) {
            rflow->SetPacketHeader(&hdr);
            data_.match_p.reverse_sg_action =
                MatchAcl(hdr, data_.match_p.m_reverse_sg_acl_l, true,
                         !data_.match_p.reverse_sg_rule_present,
                         &rev_sg_acl_info);
            if (ShouldDrop(data_.match_p.reverse_sg_action) == false) {
                // Key fields for lookup in out-acl can potentially change in
                // case of NAT. Form ACL lookup based on post-NAT fields
                rflow->SetOutPacketHeader(&out_hdr);
                data_.match_p.reverse_out_sg_action =
                    MatchAcl(out_hdr, data_.match_p.m_reverse_out_sg_acl_l, true,
                             !data_.match_p.reverse_out_sg_rule_present,
                             &rev_sg_acl_info);
            }
        }

        // Compute summary SG action.
        // For Non-TCP-ACK Flows
        //     DROP if any of sg_action, sg_out_action, reverse_sg_action or
        //     reverse_out_sg_action says DROP
        //     Only sg_acl_info which is derived from data_.match_p.m_sg_acl_l
        //     and data_.match_p.m_out_sg_acl_l will be populated. Pick the
        //     UUID specified by sg_acl_info for flow's SG rule UUID
        // For TCP-ACK flows
        //     ALLOW if either ((sg_action && sg_out_action) ||
        //                      (reverse_sg_action & reverse_out_sg_action)) ALLOW
        //     For flow's SG rule UUID use the following rules
        //     --If both sg_acl_info and rev_sg_acl_info has drop set, pick the
        //       UUID from sg_acl_info.
        //     --If either of sg_acl_info or rev_sg_acl_info does not have drop
        //       set, pick the UUID from the one which does not have drop set.
        //     --If both of them does not have drop set, pick it up from
        //       sg_acl_info
        //
        data_.match_p.sg_action_summary = 0;
        if (!is_flags_set(FlowEntry::TcpAckFlow)) {
            data_.match_p.sg_action_summary =
                data_.match_p.sg_action |
                data_.match_p.out_sg_action |
                data_.match_p.reverse_sg_action |
                data_.match_p.reverse_out_sg_action;
            sg_rule_uuid_ = sg_acl_info.uuid;
        } else {
            if (ShouldDrop(data_.match_p.sg_action |
                           data_.match_p.out_sg_action)
                &&
                ShouldDrop(data_.match_p.reverse_sg_action |
                           data_.match_p.reverse_out_sg_action)) {
                data_.match_p.sg_action_summary = (1 << TrafficAction::DROP);
                sg_rule_uuid_ = sg_acl_info.uuid;
            } else {
                data_.match_p.sg_action_summary = (1 << TrafficAction::PASS);
                if (!ShouldDrop(data_.match_p.sg_action |
                                data_.match_p.out_sg_action)) {
                    sg_rule_uuid_ = sg_acl_info.uuid;
                } else if (!ShouldDrop(data_.match_p.reverse_sg_action |
                                       data_.match_p.reverse_out_sg_action)) {
                    sg_rule_uuid_ = rev_sg_acl_info.uuid;
                }
            }
        }
    } else {
        // SG is reflexive ACL. For reverse-flow, copy SG action from
        // forward flow 
        UpdateReflexiveAction();
    }

done:
    nw_ace_uuid_ = nw_acl_info.uuid;
    // Set mirror vrf after evaluation of actions
    SetMirrorVrfFromAction();
    //Set VRF assign action
    SetVrfAssignEntry();
    // Summarize the actions based on lookups above
    ActionRecompute();
    return true;
}

void FlowEntry::SetVrfAssignEntry() {
    if (!(data_.match_p.vrf_assign_acl_action &
         (1 << TrafficAction::VRF_TRANSLATE))) {
        data_.vrf_assign_evaluated = true;
        return;
    }
    std::string vrf_assigned_name =
        data_.match_p.action_info.vrf_translate_action_.vrf_name();
    std::list<MatchAclParams>::const_iterator acl_it;
    for (acl_it = match_p().m_vrf_assign_acl_l.begin();
         acl_it != match_p().m_vrf_assign_acl_l.end();
         ++acl_it) {
        std::string vrf = acl_it->action_info.vrf_translate_action_.vrf_name();
        data_.match_p.action_info.vrf_translate_action_.set_vrf_name(vrf);
        //Check if VRF assign acl says, network ACL and SG action
        //to be ignored
        bool ignore_acl = acl_it->action_info.vrf_translate_action_.ignore_acl();
        data_.match_p.action_info.vrf_translate_action_.set_ignore_acl(ignore_acl);
    }
    if (data_.vrf_assign_evaluated && vrf_assigned_name !=
        data_.match_p.action_info.vrf_translate_action_.vrf_name()) {
        MakeShortFlow(SHORT_VRF_CHANGE);
    }
    if (acl_assigned_vrf_index() == 0) {
        MakeShortFlow(SHORT_VRF_CHANGE);
    }
    data_.vrf_assign_evaluated = true;
}

// SetMirrorVrfFromAction
// For this flow check for mirror action from dynamic ACLs or policy mirroring
// assign the vrf from its Virtual Nework that ACL is used
// If it is a local flow and out mirror action or policy is set
// assign the vrf of the reverse flow, since ACL came from the reverse flow
void FlowEntry::SetMirrorVrfFromAction() {
    if (data_.match_p.mirror_action & (1 << TrafficAction::MIRROR) ||
        data_.match_p.policy_action & (1 << TrafficAction::MIRROR)) {
        const VnEntry *vn = vn_entry();
        if (vn && vn->GetVrf()) {
            SetMirrorVrf(vn->GetVrf()->vrf_id());
        }
    }
    if (data_.match_p.out_mirror_action & (1 << TrafficAction::MIRROR) ||
        data_.match_p.out_policy_action & (1 << TrafficAction::MIRROR)) {
        FlowEntry *rflow = reverse_flow_entry_.get();
        if (rflow) {
            const VnEntry *rvn = rflow->vn_entry();
            if (rvn && rvn->GetVrf()) {
                SetMirrorVrf(rvn->GetVrf()->vrf_id());
            }
        }
    }
}

// Ingress-ACL/Egress-ACL in interface with VM as reference point.
//      Ingress : Packet to VM
//      Egress  : Packet from VM
// The direction stored in flow is defined with vrouter as reference point
//      Ingress : Packet to Vrouter from VM
//      Egress  : Packet from Vrouter to VM
// 
// Function takes care of copying right rules
static bool CopySgEntries(const VmInterface *vm_port, bool ingress_acl,
                          std::list<MatchAclParams> &list) {
    bool ret = false;
    for (VmInterface::SecurityGroupEntrySet::const_iterator it =
         vm_port->sg_list().list_.begin();
         it != vm_port->sg_list().list_.end(); ++it) {
        if (it->sg_ == NULL)
            continue;

        if (it->sg_->IsAclSet()) {
            ret = true;
        }
        MatchAclParams acl;
        // As per definition above, 
        //      get EgressACL if flow direction is Ingress
        //      get IngressACL if flow direction is Egress
        if (ingress_acl) {
            acl.acl = it->sg_->GetEgressAcl();
        } else {
            acl.acl = it->sg_->GetIngressAcl();
        }
        if (acl.acl)
            list.push_back(acl);
    }

    return ret;
}

void FlowEntry::GetLocalFlowSgList(const VmInterface *vm_port,
                                   const VmInterface *reverse_vm_port) {
    // Get SG-Rule for the forward flow
    data_.match_p.sg_rule_present = CopySgEntries(vm_port, true,
                                                  data_.match_p.m_sg_acl_l);
    // For local flow, we need to simulate SG lookup at both ends.
    // Assume packet is from VM-A to VM-B.
    // If we apply Ingress-ACL from VM-A, then apply Egress-ACL from VM-B
    // If we apply Egress-ACL from VM-A, then apply Ingress-ACL from VM-B
    if (reverse_vm_port) {
        data_.match_p.out_sg_rule_present =
            CopySgEntries(reverse_vm_port, false, data_.match_p.m_out_sg_acl_l);
    }

    if (!is_flags_set(FlowEntry::TcpAckFlow)) {
        return;
    }

    // TCP ACK workaround:
    // Ideally TCP State machine should be run to age TCP flows
    // Temporary workaound in place of state machine. For TCP ACK packets allow
    // the flow if either forward or reverse flow is allowed

    // Copy the SG rules to be applied for reverse flow
    data_.match_p.reverse_out_sg_rule_present =
        CopySgEntries(vm_port, false,
                      data_.match_p.m_reverse_out_sg_acl_l);

    if (reverse_vm_port) {
        data_.match_p.reverse_sg_rule_present =
            CopySgEntries(reverse_vm_port, true,
                          data_.match_p.m_reverse_sg_acl_l);
    }
}

void FlowEntry::GetNonLocalFlowSgList(const VmInterface *vm_port) {
    // Get SG-Rule for the forward flow
    bool ingress = is_flags_set(FlowEntry::IngressDir);
    data_.match_p.sg_rule_present = CopySgEntries(vm_port, ingress,
                                                  data_.match_p.m_sg_acl_l);
    data_.match_p.out_sg_rule_present = false;

    if (!is_flags_set(FlowEntry::TcpAckFlow)) {
        return;
    }

    // TCP ACK workaround:
    // Ideally TCP State machine should be run to age TCP flows
    // Temporary workaound in place of state machine. For TCP ACK packets allow
    // the flow if either forward or reverse flow is allowed

    // Copy the SG rules to be applied for reverse flow
    data_.match_p.reverse_out_sg_rule_present =
        CopySgEntries(vm_port, !ingress,
                      data_.match_p.m_reverse_out_sg_acl_l);
    data_.match_p.reverse_sg_rule_present = false;
}

void FlowEntry::GetSgList(const Interface *intf) {
    // Dont apply network-policy for linklocal and multicast flows
    if (is_flags_set(FlowEntry::LinkLocalFlow) ||
        is_flags_set(FlowEntry::Multicast)) {
        return;
    }

    // SG ACL's are reflexive. Skip SG for reverse flow
    if (is_flags_set(FlowEntry::ReverseFlow)) {
        return;
    }

    // Get virtual-machine port for forward flow
    const VmInterface *vm_port = NULL;
    if (intf != NULL) {
        if (intf->type() == Interface::VM_INTERFACE) {
            vm_port = static_cast<const VmInterface *>(intf);
         }
    }

    if (vm_port == NULL) {
        return;
    }

    // Get virtual-machine port for reverse flow
    FlowEntry *rflow = reverse_flow_entry();
    const VmInterface *reverse_vm_port = NULL;
    if (rflow != NULL) {
        if (rflow->data().intf_entry.get() != NULL) {
            if (rflow->data().intf_entry->type() == Interface::VM_INTERFACE) {
                reverse_vm_port = static_cast<const VmInterface *>
                    (rflow->data().intf_entry.get());
            }
        }
    }

    // Get SG-Rules
    if (is_flags_set(FlowEntry::LocalFlow)) {
        GetLocalFlowSgList(vm_port, reverse_vm_port);
    } else {
        GetNonLocalFlowSgList(vm_port);
    }
}

void FlowEntry::ResetPolicy() {
    /* Reset acl list*/
    data_.match_p.m_acl_l.clear();
    data_.match_p.m_out_acl_l.clear();
    data_.match_p.m_mirror_acl_l.clear();
    data_.match_p.m_out_mirror_acl_l.clear();
    /* Reset sg acl list*/
    data_.match_p.sg_rule_present = false;
    data_.match_p.m_sg_acl_l.clear();
    data_.match_p.out_sg_rule_present = false;
    data_.match_p.m_out_sg_acl_l.clear();

    data_.match_p.reverse_sg_rule_present = false;
    data_.match_p.m_reverse_sg_acl_l.clear();
    data_.match_p.reverse_out_sg_rule_present = false;
    data_.match_p.m_reverse_out_sg_acl_l.clear();
    data_.match_p.m_vrf_assign_acl_l.clear();
}

void FlowEntry::GetPolicy(const VnEntry *vn) {
    if (vn == NULL)
        return;

    MatchAclParams acl;

    // Get Mirror configuration first
    if (vn->GetMirrorAcl()) {
        acl.acl = vn->GetMirrorAcl();
        data_.match_p.m_mirror_acl_l.push_back(acl);
    }

    if (vn->GetMirrorCfgAcl()) {
        acl.acl = vn->GetMirrorCfgAcl();
        data_.match_p.m_mirror_acl_l.push_back(acl);
    }

    // Dont apply network-policy for linklocal and subnet broadcast flow
    if (is_flags_set(FlowEntry::LinkLocalFlow) ||
        is_flags_set(FlowEntry::Multicast)) {
        return;
    }

    if (vn->GetAcl()) {
        acl.acl = vn->GetAcl();
        data_.match_p.m_acl_l.push_back(acl);
    }

    const VnEntry *rvn = NULL;
    FlowEntry *rflow = reverse_flow_entry_.get();
    // For local flows, we have to apply NW Policy from out-vn also
    if (!is_flags_set(FlowEntry::LocalFlow) || rflow == NULL) {
        // Not local flow
        return;
    }

    rvn = rflow->vn_entry();
    if (rvn == NULL) {
        return;
    }

    if (rvn->GetAcl()) {
        acl.acl = rvn->GetAcl();
        data_.match_p.m_out_acl_l.push_back(acl);
    }

    if (rvn->GetMirrorAcl()) {
        acl.acl = rvn->GetMirrorAcl();
        data_.match_p.m_out_mirror_acl_l.push_back(acl);
    }

    if (rvn->GetMirrorCfgAcl()) {
        acl.acl = rvn->GetMirrorCfgAcl();
        data_.match_p.m_out_mirror_acl_l.push_back(acl);
    }
}

void FlowEntry::GetVrfAssignAcl() {
    if (data_.intf_entry == NULL) {
        return;
    }

    if  (data_.intf_entry->type() != Interface::VM_INTERFACE) {
        return;
    }

    if (is_flags_set(FlowEntry::LinkLocalFlow) ||
        is_flags_set(FlowEntry::Multicast)) {
        return;
    }

    const VmInterface *intf =
        static_cast<const VmInterface *>(data_.intf_entry.get());
    //If interface has a VRF assign rule, choose the acl and match the
    //packet, else get the acl attached to VN and try matching the packet to
    //network acl
    const AclDBEntry* acl = intf->vrf_assign_acl();
    if (acl == NULL) {
        acl = data_.vn_entry.get()->GetAcl();
    }
    if (!acl) {
        return;
    }

    MatchAclParams m_acl;
    m_acl.acl = acl;
    data_.match_p.m_vrf_assign_acl_l.push_back(m_acl);
}

const std::string& FlowEntry::acl_assigned_vrf() const {
    return data_.match_p.action_info.vrf_translate_action_.vrf_name();
}

uint32_t FlowEntry::acl_assigned_vrf_index() const {
    VrfKey vrf_key(data_.match_p.action_info.vrf_translate_action_.vrf_name());
    const VrfEntry *vrf = static_cast<const VrfEntry *>(
            Agent::GetInstance()->vrf_table()->FindActiveEntry(&vrf_key));
    if (vrf) {
        return vrf->vrf_id();
    }
    return 0;
}

void FlowEntry::UpdateKSync(FlowTable* table) {
    FlowTableKSyncObject *ksync_obj = 
        Agent::GetInstance()->ksync()->flowtable_ksync_obj();

    if (ksync_entry_ == NULL) {
        FlowTableKSyncEntry key(ksync_obj, this, flow_handle_);
        ksync_entry_ =
            static_cast<FlowTableKSyncEntry *>(ksync_obj->Create(&key));
        if (deleted_) {
            /*
             * Create and delete a KSync Entry when update ksync entry is
             * triggered for a deleted flow entry.
             * This happens when Reverse flow deleted  is deleted before
             * getting an ACK from vrouter.
             */
            ksync_obj->Delete(ksync_entry_);
            ksync_entry_ = NULL;
        }
    } else {
        if (flow_handle_ != ksync_entry_->hash_id()) {
            /*
             * if flow handle changes delete the previous record from
             * vrouter and install new
             */
            ksync_obj->Delete(ksync_entry_);
            FlowTableKSyncEntry key(ksync_obj, this, flow_handle_);
            ksync_entry_ =
                static_cast<FlowTableKSyncEntry *>(ksync_obj->Create(&key));
        } else {
            ksync_obj->Change(ksync_entry_);
        }
    }
}

void FlowEntry::MakeShortFlow(FlowShortReason reason) {
    if (!is_flags_set(FlowEntry::ShortFlow)) {
        set_flags(FlowEntry::ShortFlow);
        short_flow_reason_ = reason;
    }
    if (reverse_flow_entry_ &&
        !reverse_flow_entry_->is_flags_set(FlowEntry::ShortFlow)) {
        reverse_flow_entry_->set_flags(FlowEntry::ShortFlow);
        reverse_flow_entry_->short_flow_reason_ = reason;
    }
}

void FlowEntry::GetPolicyInfo(const VnEntry *vn) {
    // Default make it false
    ResetPolicy();

    // Short flows means there is some information missing for the flow. Skip 
    // getting policy information for short flow. When the information is
    // complete, GetPolicyInfo is called again
    if (is_flags_set(FlowEntry::ShortFlow)) {
        return;
    }

    // ACL supported on VMPORT interfaces only
    if (data_.intf_entry == NULL)
        return;

    if  (data_.intf_entry->type() != Interface::VM_INTERFACE)
        return;

    // Get Network policy/mirror cfg policy/mirror policies 
    GetPolicy(vn);

    // Get Sg list
    GetSgList(data_.intf_entry.get());

    //Get VRF translate ACL
    GetVrfAssignAcl();
}

void FlowEntry::GetPolicyInfo() {
    GetPolicyInfo(data_.vn_entry.get());
}

void FlowTable::Add(FlowEntry *flow, FlowEntry *rflow) {
    flow->reset_flags(FlowEntry::ReverseFlow);
    /* reverse flow may not be aviable always, eg: Flow Audit */
    if (rflow != NULL)
        rflow->set_flags(FlowEntry::ReverseFlow);
    UpdateReverseFlow(flow, rflow);

    flow->GetPolicyInfo();
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
        rflow->GetPolicyInfo();
        ResyncAFlow(rflow);
        AddFlowInfo(rflow);
    }


    ResyncAFlow(flow);
    AddFlowInfo(flow);
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

void FlowEntry::UpdateFipStatsInfo(uint32_t fip, uint32_t id) {
    stats_.fip = fip;
    stats_.fip_vm_port_id = id;
}

bool FlowEntry::set_pending_recompute(bool value) {
    if (data_.pending_recompute != value) {
        data_.pending_recompute = value;
        return true;
    }

    return false;
}

void FlowEntry::set_flow_handle(uint32_t flow_handle, FlowTable* table) {
    /* trigger update KSync on flow handle change */
    if (flow_handle_ != flow_handle) {
        flow_handle_ = flow_handle;
        UpdateKSync(table);
    }
}

void FlowEntry::UpdateReflexiveAction() {
    data_.match_p.sg_action = (1 << TrafficAction::PASS);
    data_.match_p.out_sg_action = (1 << TrafficAction::PASS);
    data_.match_p.reverse_sg_action = (1 << TrafficAction::PASS);;
    data_.match_p.reverse_out_sg_action = (1 << TrafficAction::PASS);
    data_.match_p.sg_action_summary = (1 << TrafficAction::PASS);

    FlowEntry *fwd_flow = reverse_flow_entry();
    if (fwd_flow) {
        data_.match_p.sg_action_summary =
            fwd_flow->data().match_p.sg_action_summary;
        // Since SG is reflexive ACL, copy sg_rule_uuid_ from forward flow
        sg_rule_uuid_ = fwd_flow->sg_rule_uuid();
    }
    // If forward flow is DROP, set action for reverse flow to
    // TRAP. If packet hits reverse flow, we will re-establish
    // the flows
    if (ShouldDrop(data_.match_p.sg_action_summary)) {
        data_.match_p.sg_action &= ~(TrafficAction::DROP_FLAGS);
        data_.match_p.sg_action |= (1 << TrafficAction::TRAP);
     }
}

bool FlowEntry::SetRpfNH(FlowTable *ft, const AgentRoute *rt) {
    bool ret = false;
    //If l2 flow has a ip route entry present in
    //layer 3 table, then use that for calculating
    //rpf nexthop, else use layer 2 route entry(baremetal
    //scenario where layer 3 route may not be present)
    bool is_baremetal = false;
    const VmInterface *vmi = dynamic_cast<const VmInterface *>(intf_entry());
    if (vmi && vmi->vmi_type() == VmInterface::BAREMETAL) {
        is_baremetal = true;
    }

    data_.l2_rpf_plen = Address::kMaxV4PrefixLen;
    if (l3_flow() == false && is_baremetal == false) {
        //For ingress flow, agent always sets
        //rpf NH from layer 3 route entry
        //In case of egress flow if route entry is present
        //and its a host route entry use it for RPF NH
        //For baremetal case since IP address may not be known
        //agent uses layer 2 route entry
        InetUnicastRouteEntry *ip_rt = static_cast<InetUnicastRouteEntry *>(
            ft->GetUcRoute(rt->vrf(), key().src_addr));
        if (is_flags_set(FlowEntry::IngressDir) ||
                (ip_rt && ip_rt->IsHostRoute())) {
            rt = ip_rt;
            if (rt) {
                data_.l2_rpf_plen = rt->plen();
            }
        }
    }

    if (!rt) {
        if (data_.nh.get() != NULL) {
            data_.nh = NULL;
            ret = true;
        }
        return ret;
    }

    const NextHop *nh = rt->GetActiveNextHop();
    if (key_.family != Address::INET) {
        //TODO:IPV6
        return false;
    }
    if (nh->GetType() == NextHop::COMPOSITE &&
        !is_flags_set(FlowEntry::LocalFlow) &&
        is_flags_set(FlowEntry::IngressDir)) {
        assert(l3_flow_ == true);
            //Logic for RPF check for ecmp
            //  Get reverse flow, and its corresponding ecmp index
            //  Check if source matches composite nh in reverse flow ecmp index,
            //  if not DP would trap packet for ECMP resolve.
            //  If there is only one instance of ECMP in compute node, then 
            //  RPF NH would only point to local interface NH.
            //  If there are multiple instances of ECMP in local server
            //  then RPF NH would point to local composite NH(containing 
            //  local members only)
        const InetUnicastRouteEntry *route =
            static_cast<const InetUnicastRouteEntry *>(rt);
        nh = route->GetLocalNextHop();
    }

    //If a transistion from non-ecmp to ecmp occurs trap forward flow
    //such that ecmp index of reverse flow is set.
    if (data_.nh.get() && nh) {
        if (data_.nh->GetType() != NextHop::COMPOSITE &&
            nh->GetType() == NextHop::COMPOSITE) {
            set_flags(FlowEntry::Trap);
        }
    }

    if (data_.nh.get() != nh) {
        data_.nh = nh;
        return true;
    }
    return false;
}

bool FlowEntry::InitFlowCmn(const PktFlowInfo *info, const PktControlInfo *ctrl,
                            const PktControlInfo *rev_ctrl) {
    peer_vrouter_ = info->peer_vrouter;
    tunnel_type_ = info->tunnel_type;
    if (stats_.last_modified_time) {
        if (is_flags_set(FlowEntry::NatFlow) != info->nat_done) {
            MakeShortFlow(SHORT_NAT_CHANGE);
            return false;
        }
        stats_.last_modified_time = UTCTimestampUsec();
    } else {
        /* For Flow Entry Create take last modified time same as setup time */
        stats_.last_modified_time = stats_.setup_time;
    }

    if (info->linklocal_flow) {
        set_flags(FlowEntry::LinkLocalFlow);
    } else {
        reset_flags(FlowEntry::LinkLocalFlow);
    }
    if (info->nat_done) {
        set_flags(FlowEntry::NatFlow);
    } else {
        reset_flags(FlowEntry::NatFlow);
    }
    if (info->short_flow) {
        set_flags(FlowEntry::ShortFlow);
        short_flow_reason_ = info->short_flow_reason;
    } else {
        reset_flags(FlowEntry::ShortFlow);
        short_flow_reason_ = SHORT_UNKNOWN;
    }
    if (info->local_flow) {
        set_flags(FlowEntry::LocalFlow);
    } else {
        reset_flags(FlowEntry::LocalFlow);
    }

    if (info->tcp_ack) {
        set_flags(FlowEntry::TcpAckFlow);
    } else {
        reset_flags(FlowEntry::TcpAckFlow);
    }

    data_.intf_entry = ctrl->intf_ ? ctrl->intf_ : rev_ctrl->intf_;
    data_.vn_entry = ctrl->vn_ ? ctrl->vn_ : rev_ctrl->vn_;
    data_.in_vm_entry = ctrl->vm_ ? ctrl->vm_ : NULL;
    data_.out_vm_entry = rev_ctrl->vm_ ? rev_ctrl->vm_ : NULL;
    l3_flow_ = info->l3_flow;

    return true;
}

void FlowEntry::InitFwdFlow(const PktFlowInfo *info, const PktInfo *pkt,
                            const PktControlInfo *ctrl,
                            const PktControlInfo *rev_ctrl) {
    if (flow_handle_ != pkt->GetAgentHdr().cmd_param) {
        if (flow_handle_ != FlowEntry::kInvalidFlowHandle) {
            LOG(DEBUG, "Flow index changed from " << flow_handle_ 
                << " to " << pkt->GetAgentHdr().cmd_param);
        }
        flow_handle_ = pkt->GetAgentHdr().cmd_param;
    }

    if (InitFlowCmn(info, ctrl, rev_ctrl) == false) {
        return;
    }
    if (info->linklocal_bind_local_port) {
        linklocal_src_port_ = info->nat_sport;
        linklocal_src_port_fd_ = info->linklocal_src_port_fd;
        set_flags(FlowEntry::LinkLocalBindLocalSrcPort);
    } else {
        reset_flags(FlowEntry::LinkLocalBindLocalSrcPort);
    }
    stats_.intf_in = pkt->GetAgentHdr().ifindex;

    if (info->ingress) {
        set_flags(FlowEntry::IngressDir);
    } else {
        reset_flags(FlowEntry::IngressDir);
    }
    if (ctrl->rt_ != NULL) {
        SetRpfNH(info->flow_table, ctrl->rt_);
    }

    data_.flow_source_vrf = info->flow_source_vrf;
    data_.flow_dest_vrf = info->flow_dest_vrf;
    data_.flow_source_plen_map = info->flow_source_plen_map;
    data_.flow_dest_plen_map = info->flow_dest_plen_map;
    data_.dest_vrf = info->dest_vrf;
    data_.vrf = pkt->vrf;
    data_.if_index_info = pkt->agent_hdr.ifindex;
    data_.tunnel_info = pkt->tunnel;

    if (info->ecmp) {
        set_flags(FlowEntry::EcmpFlow);
    } else {
        reset_flags(FlowEntry::EcmpFlow);
    }
    data_.component_nh_idx = info->out_component_nh_idx;
    reset_flags(FlowEntry::Trap);
    if (ctrl->rt_ && ctrl->rt_->is_multicast()) {
        set_flags(FlowEntry::Multicast);
    }
    if (rev_ctrl->rt_ && rev_ctrl->rt_->is_multicast()) {
        set_flags(FlowEntry::Multicast);
    }

    reset_flags(FlowEntry::UnknownUnicastFlood);
    if (info->flood_unknown_unicast) {
        set_flags(FlowEntry::UnknownUnicastFlood);
        if (info->ingress) {
            GetSourceRouteInfo(ctrl->rt_);
        } else {
            GetSourceRouteInfo(rev_ctrl->rt_);
        }
        data_.dest_vn = data_.source_vn;
    } else {
        GetSourceRouteInfo(ctrl->rt_);
        GetDestRouteInfo(rev_ctrl->rt_);
    }

    data_.smac = pkt->smac;
    data_.dmac = pkt->dmac;
}

void FlowEntry::InitRevFlow(const PktFlowInfo *info, const PktInfo *pkt,
                            const PktControlInfo *ctrl,
                            const PktControlInfo *rev_ctrl) {
    if (InitFlowCmn(info, ctrl, rev_ctrl) == false) {
        return;
    }
    if (ctrl->intf_) {
        stats_.intf_in = ctrl->intf_->id();
    } else {
        stats_.intf_in = Interface::kInvalidIndex;
    }

    // Compute reverse flow fields
    reset_flags(FlowEntry::IngressDir);
    if (ctrl->intf_) {
        if (info->ComputeDirection(ctrl->intf_)) {
            set_flags(FlowEntry::IngressDir);
        } else {
            reset_flags(FlowEntry::IngressDir);
        }
    }
    if (ctrl->rt_ != NULL) {
        SetRpfNH(info->flow_table, ctrl->rt_);
    }

    data_.flow_source_vrf = info->flow_dest_vrf;
    data_.flow_dest_vrf = info->flow_source_vrf;
    data_.flow_source_plen_map = info->flow_dest_plen_map;
    data_.flow_dest_plen_map = info->flow_source_plen_map;
    data_.dest_vrf = info->nat_dest_vrf;
    data_.vrf = info->dest_vrf;
    if (info->ecmp) {
        set_flags(FlowEntry::EcmpFlow);
    } else {
        reset_flags(FlowEntry::EcmpFlow);
    }
    data_.component_nh_idx = info->in_component_nh_idx;
    if (info->trap_rev_flow) {
        set_flags(FlowEntry::Trap);
    } else {
        reset_flags(FlowEntry::Trap);
    }

    reset_flags(FlowEntry::UnknownUnicastFlood);
    if (info->flood_unknown_unicast) {
        set_flags(FlowEntry::UnknownUnicastFlood);
        if (info->ingress) {
            GetSourceRouteInfo(rev_ctrl->rt_);
        } else {
            GetSourceRouteInfo(ctrl->rt_);
        }
        //Set source VN and dest VN to be same
        //since flooding happens only for layer2 routes
        //SG id would be left empty, user who wants
        //unknown unicast to happen should modify the
        //SG to allow such traffic
        data_.dest_vn = data_.source_vn;
    } else {
        GetSourceRouteInfo(ctrl->rt_);
        GetDestRouteInfo(rev_ctrl->rt_);
    }

    data_.smac = pkt->dmac;
    data_.dmac = pkt->smac;
}

void FlowEntry::InitAuditFlow(uint32_t flow_idx) {
    flow_handle_ = flow_idx;
    set_flags(FlowEntry::ShortFlow);
    short_flow_reason_ = SHORT_AUDIT_ENTRY;
    data_.source_vn = FlowHandler::UnknownVn();
    data_.dest_vn = FlowHandler::UnknownVn();
    data_.source_sg_id_l = FlowTable::default_sg_list();
    data_.dest_sg_id_l = FlowTable::default_sg_list();
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
        flow->stats_.setup_time = UTCTimestampUsec();
        agent_->stats()->incr_flow_created();
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

void FlowTable::ResyncAFlow(FlowEntry *fe) {
    fe->UpdateRpf();
    fe->DoPolicy();
    fe->UpdateKSync(this);

    // If this is forward flow, update the SG action for reflexive entry
    if (fe->is_flags_set(FlowEntry::ReverseFlow)) {
        return;
    }

    FlowEntry *rflow = fe->reverse_flow_entry();
    if (rflow == NULL) {
        return;
    }

    rflow->UpdateReflexiveAction();
    // Check if there is change in action for reverse flow
    rflow->ActionRecompute();

    rflow->UpdateKSync(this);
}

// Find L2 Route for the MAC address.
AgentRoute *FlowTable::GetL2Route(const VrfEntry *vrf,
                                  const MacAddress &mac) {
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (vrf->GetBridgeRouteTable());
    return table->FindRoute(mac);
}

AgentRoute *FlowTable::GetUcRoute(const VrfEntry *entry,
                                  const IpAddress &addr) {
    AgentRoute *rt = NULL;
    if (addr.is_v4()) {
        inet4_route_key_.set_addr(addr.to_v4());
        rt = entry->GetUcRoute(inet4_route_key_);
    } else {
        inet6_route_key_.set_addr(addr.to_v6());
        rt = entry->GetUcRoute(inet6_route_key_);
    }
    if (rt != NULL && rt->IsRPFInvalid()) {
        return NULL;
    }
    return rt;
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
// FlowTable constructor/destructor
/////////////////////////////////////////////////////////////////////////////
FlowTable::FlowTable(Agent *agent) : 
    agent_(agent),
    flow_entry_map_(), linklocal_flow_count_(),
    inet4_route_key_(NULL, Ip4Address(), 32, false),
    inet6_route_key_(NULL, Ip6Address(), 128, false) {
    max_vm_flows_ = (uint32_t)
        (agent->ksync()->flowtable_ksync_obj()->flow_table_entries_count() *
         agent->params()->max_vm_flows()) / 100;
}

FlowTable::~FlowTable() {
}

SandeshTraceBufferPtr FlowTraceBuf(SandeshTraceBufferCreate("Flow", 5000));

void FlowTable::Init() {

    FlowEntry::alloc_count_ = 0;
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
}

/////////////////////////////////////////////////////////////////////////////
// Flow revluation routines. Processing will vary based on DBEntry type
/////////////////////////////////////////////////////////////////////////////
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
        flow->UpdateKSync(this);
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
        assert(0);
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
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
void SetActionStr(const FlowAction &action_info, std::vector<ActionStr> &action_str_l) {
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
            if ((TrafficAction::Action)i == TrafficAction::VRF_TRANSLATE) {
                ActionStr vrf_action_str;
                vrf_action_str.action +=
                    action_info.vrf_translate_action_.vrf_name();
                action_str_l.push_back(vrf_action_str);
            }
        }
    }
}

void GetFlowSandeshActionParams(const FlowAction &action_info,
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

void FlowEntry::SetAclAction(std::vector<AclAction> &acl_action_l) const {
    const std::list<MatchAclParams> &acl_l = data_.match_p.m_acl_l;
    std::string acl_type("nw policy");
    SetAclListAclAction(acl_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &sg_acl_l = data_.match_p.m_sg_acl_l;
    acl_type = "sg";
    SetAclListAclAction(sg_acl_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &m_acl_l = data_.match_p.m_mirror_acl_l;
    acl_type = "dynamic";
    SetAclListAclAction(m_acl_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &out_acl_l = data_.match_p.m_out_acl_l;
    acl_type = "o nw policy";
    SetAclListAclAction(out_acl_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &out_sg_acl_l = data_.match_p.m_out_sg_acl_l;
    acl_type = "o sg";
    SetAclListAclAction(out_sg_acl_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &out_m_acl_l = data_.match_p.m_out_mirror_acl_l;
    acl_type = "o dynamic";
    SetAclListAclAction(out_m_acl_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &r_sg_l = data_.match_p.m_reverse_sg_acl_l;
    acl_type = "r sg";
    SetAclListAclAction(r_sg_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &r_out_sg_l = data_.match_p.m_reverse_out_sg_acl_l;
    acl_type = "r o sg";
    SetAclListAclAction(r_out_sg_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &vrf_assign_acl_l = data_.match_p.m_vrf_assign_acl_l;
    acl_type = "vrf assign";
    SetAclListAclAction(vrf_assign_acl_l, acl_action_l, acl_type);
}

uint32_t FlowEntry::reverse_flow_fip() const {
    FlowEntry *rflow = reverse_flow_entry_.get();
    if (rflow) {
        return rflow->stats().fip;
    }
    return 0;
}

uint32_t FlowEntry::reverse_flow_vmport_id() const {
    FlowEntry *rflow = reverse_flow_entry_.get();
    if (rflow) {
        return rflow->stats().fip_vm_port_id;
    }
    return Interface::kInvalidIndex;
}

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

void FlowTable::SetAceSandeshData(const AclDBEntry *acl, AclFlowCountResp &data, int ace_id) {
}

void FlowTable::SetAclFlowSandeshData(const AclDBEntry *acl, AclFlowResp &data, 
                                      const int last_count) {
}

void FlowEntry::FillFlowInfo(FlowInfo &info) {
    info.set_flow_index(flow_handle_);
    if (key_.family == Address::INET) {
        info.set_source_ip(key_.src_addr.to_v4().to_ulong());
        info.set_destination_ip(key_.dst_addr.to_v4().to_ulong());
    } else {
        // TODO : IPV6
        info.set_source_ip(0);
        info.set_destination_ip(0);
    }
    info.set_source_port(key_.src_port);
    info.set_destination_port(key_.dst_port);
    info.set_protocol(key_.protocol);
    info.set_nh_id(key_.nh);
    info.set_vrf(data_.vrf);
    info.set_source_vn(data_.source_vn);
    info.set_dest_vn(data_.dest_vn);
    std::vector<uint32_t> v;
    SecurityGroupList::const_iterator it;
    for (it = data_.source_sg_id_l.begin();
            it != data_.source_sg_id_l.end(); it++) {
        v.push_back(*it);
    }
    info.set_source_sg_id_l(v);
    v.clear();
    for (it = data_.dest_sg_id_l.begin(); it != data_.dest_sg_id_l.end(); it++) {
        v.push_back(*it);
    }
    info.set_dest_sg_id_l(v);

    uint32_t fe_action = data_.match_p.action_info.action;
    if (fe_action & (1 << TrafficAction::DENY)) {
        info.set_deny(true);
    } else if (fe_action & (1 << TrafficAction::PASS)) {
        info.set_allow(true);
    }

    if (is_flags_set(FlowEntry::NatFlow)) {
        info.set_nat(true);
        FlowEntry *nat_flow = reverse_flow_entry_.get();
        // TODO : IPv6
        if (nat_flow) {
            if (key_.src_addr != nat_flow->key().dst_addr) {
                if (key_.family == Address::INET) {
                    info.set_nat_source_ip
                        (nat_flow->key().dst_addr.to_v4().to_ulong());
                } else {
                    info.set_nat_source_ip(0);
                }
            }

            if (key_.dst_addr != nat_flow->key().src_addr) {
                if (key_.family == Address::INET) {
                    info.set_nat_destination_ip(nat_flow->key().src_addr.to_v4().to_ulong());
                } else {
                    info.set_nat_destination_ip(0);
                }
            }

            if (key_.src_port != nat_flow->key().dst_port)  {
                info.set_nat_source_port(nat_flow->key().dst_port);
            }

            if (key_.dst_port != nat_flow->key().src_port) {
                info.set_nat_destination_port(nat_flow->key().src_port);
            }
            info.set_nat_protocol(nat_flow->key().protocol);
            info.set_nat_vrf(data_.dest_vrf);
            info.set_reverse_index(nat_flow->flow_handle());
            info.set_nat_mirror_vrf(nat_flow->data().mirror_vrf);
        }
    }

    if (data_.match_p.action_info.action & (1 << TrafficAction::MIRROR)) {
        info.set_mirror(true);
        std::vector<MirrorActionSpec>::iterator it;
        std::vector<MirrorInfo> mirror_l;
        for (it = data_.match_p.action_info.mirror_l.begin();
             it != data_.match_p.action_info.mirror_l.end();
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
    info.set_mirror_vrf(data_.mirror_vrf);
    info.set_implicit_deny(ImplicitDenyFlow());
    info.set_short_flow(is_flags_set(FlowEntry::ShortFlow));
    if (is_flags_set(FlowEntry::EcmpFlow) && 
            data_.component_nh_idx != CompositeNH::kInvalidComponentNHIdx) {
        info.set_ecmp_index(data_.component_nh_idx);
    }
    if (is_flags_set(FlowEntry::Trap)) {
        info.set_trap(true);
    }
    info.set_vrf_assign(acl_assigned_vrf());
    info.set_l3_flow(l3_flow_);
    info.set_smac(data_.smac.ToString());
    info.set_dmac(data_.dmac.ToString());
}

void FlowEntry::SetAclFlowSandeshData(const AclDBEntry *acl,
        FlowSandeshData &fe_sandesh_data) const {
    fe_sandesh_data.set_vrf(integerToString(data_.vrf));
    fe_sandesh_data.set_src(key_.src_addr.to_string());
    fe_sandesh_data.set_dst(key_.dst_addr.to_string());
    fe_sandesh_data.set_src_port(key_.src_port);
    fe_sandesh_data.set_dst_port(key_.dst_port);
    fe_sandesh_data.set_protocol(key_.protocol);
    fe_sandesh_data.set_ingress(is_flags_set(FlowEntry::IngressDir));
    std::vector<ActionStr> action_str_l;
    SetActionStr(data_.match_p.action_info, action_str_l);
    fe_sandesh_data.set_action_l(action_str_l);

    std::vector<AclAction> acl_action_l;
    SetAclAction(acl_action_l);
    fe_sandesh_data.set_acl_action_l(acl_action_l);

    fe_sandesh_data.set_flow_uuid(UuidToString(flow_uuid_));
    fe_sandesh_data.set_flow_handle(integerToString(flow_handle_));
    fe_sandesh_data.set_source_vn(data_.source_vn);
    fe_sandesh_data.set_dest_vn(data_.dest_vn);
    std::vector<uint32_t> v;
    SecurityGroupList::const_iterator it;
    for (it = data_.source_sg_id_l.begin(); 
            it != data_.source_sg_id_l.end(); it++) {
        v.push_back(*it);
    }
    fe_sandesh_data.set_source_sg_id_l(v);
    v.clear();
    for (it = data_.dest_sg_id_l.begin(); it != data_.dest_sg_id_l.end(); it++) {
        v.push_back(*it);
    }
    fe_sandesh_data.set_dest_sg_id_l(v);
    fe_sandesh_data.set_bytes(integerToString(stats_.bytes));
    fe_sandesh_data.set_packets(integerToString(stats_.packets));
    fe_sandesh_data.set_setup_time(
            integerToString(UTCUsecToPTime(stats_.setup_time)));
    fe_sandesh_data.set_setup_time_utc(stats_.setup_time);
    if (stats_.teardown_time) {
        fe_sandesh_data.set_teardown_time(
                integerToString(UTCUsecToPTime(stats_.teardown_time)));
    } else {
        fe_sandesh_data.set_teardown_time("");
    }
    fe_sandesh_data.set_current_time(integerToString(
                UTCUsecToPTime(UTCTimestampUsec())));

    SetAclListAceId(acl, data_.match_p.m_acl_l, fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_sg_acl_l, fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_mirror_acl_l, fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_out_acl_l, fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_reverse_sg_acl_l, fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_reverse_out_sg_acl_l, fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_out_sg_acl_l, fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_out_mirror_acl_l, fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_vrf_assign_acl_l, fe_sandesh_data.ace_l);

    fe_sandesh_data.set_reverse_flow(is_flags_set(FlowEntry::ReverseFlow) ?
                                     "yes" : "no");
    fe_sandesh_data.set_nat(is_flags_set(FlowEntry::NatFlow) ? "yes" : "no");
    fe_sandesh_data.set_implicit_deny(ImplicitDenyFlow() ? "yes" : "no");
    fe_sandesh_data.set_short_flow(is_flags_set(FlowEntry::ShortFlow) ? 
                                   "yes" : "no");
    fe_sandesh_data.set_l3_flow(l3_flow_);
    fe_sandesh_data.set_smac(data_.smac.ToString());
    fe_sandesh_data.set_dmac(data_.dmac.ToString());
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
