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
#include "uve/agent_uve.h"
#include "uve/vm_uve_table.h"
#include "uve/vn_uve_table.h"
#include "uve/vrouter_uve_entry.h"

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

FlowEntry::~FlowEntry() {
    if (is_flags_set(FlowEntry::LinkLocalBindLocalSrcPort) &&
        (linklocal_src_port_fd_ == PktFlowInfo::kLinkLocalInvalidFd ||
         !linklocal_src_port_)) {
        LOG(DEBUG, "Linklocal Flow Inconsistency fd = " <<
            linklocal_src_port_fd_ << " port = " << linklocal_src_port_ <<
            " flow index = " << flow_handle_ << " source = " <<
            key_.src_addr.to_string() << " dest = " <<
            key_.dst_addr.to_string() << " protocol = " << key_.protocol <<
            " sport = " << key_.src_port << " dport = " << key_.dst_port);
    }
    if (linklocal_src_port_fd_ != PktFlowInfo::kLinkLocalInvalidFd) {
        close(linklocal_src_port_fd_);
        Agent::GetInstance()->pkt()->flow_table()->
            DelLinkLocalFlowInfo(linklocal_src_port_fd_);
    }
    alloc_count_.fetch_and_decrement();
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
        //If VRF assign was evaluated and the vrf translate
        //action is not present in latest evaluation mark the
        //flow as short flow
        if (data_.vrf_assign_evaluated &&
            data_.match_p.action_info.vrf_translate_action_.vrf_name()
            != Agent::NullString()) {
            MakeShortFlow(SHORT_VRF_CHANGE);
        }
        data_.vrf_assign_evaluated = true;
        data_.acl_assigned_vrf_index_ = VrfEntry::kInvalidIndex;
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
    set_acl_assigned_vrf_index();
    if (acl_assigned_vrf_index() == VrfEntry::kInvalidIndex) {
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
    const AclDBEntry* acl = NULL;
    if (is_flags_set(FlowEntry::NatFlow) == false) {
        acl = intf->vrf_assign_acl();
    }

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

void FlowEntry::set_acl_assigned_vrf_index() {
    VrfKey vrf_key(data_.match_p.action_info.vrf_translate_action_.vrf_name());
    const VrfEntry *vrf = static_cast<const VrfEntry *>(
            Agent::GetInstance()->vrf_table()->FindActiveEntry(&vrf_key));
    if (vrf) {
        data_.acl_assigned_vrf_index_ = vrf->vrf_id();
        return;
    }
    data_.acl_assigned_vrf_index_ = VrfEntry::kInvalidIndex;
}

uint32_t FlowEntry::acl_assigned_vrf_index() const {
    return data_.acl_assigned_vrf_index_;
}

void FlowEntry::UpdateKSync(FlowTable* table) {
    FlowInfo flow_info;
    FillFlowInfo(flow_info);
    if (stats_.last_modified_time != stats_.setup_time) {
        /*
         * Do not export stats on flow creation, it will be exported
         * while updating stats
         */
        table->FlowExport(this, 0, 0);
    }
    FlowTableKSyncObject *ksync_obj = 
        Agent::GetInstance()->ksync()->flowtable_ksync_obj();

    if (ksync_entry_ == NULL) {
        FLOW_TRACE(Trace, "Add", flow_info);
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

void FlowTable::AddLinkLocalFlowInfo(int fd, uint32_t index, const FlowKey &key,
                                     const uint64_t timestamp) {
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
    // Decrement the linklocal flow count here and not in DeleteVmFlowInfo,
    // to avoid taking more local ports between then and the flow entry delete.
    linklocal_flow_count_--;
    linklocal_flow_info_map_.erase(fd);
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

    if (rflow && rflow->is_flags_set(FlowEntry::EcmpFlow)) {
        rflow->UpdateKSync(this);
    }

    if (flow->is_flags_set(FlowEntry::EcmpFlow)) {
        flow->UpdateKSync(this);
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
    info.set_short_flow_reason(short_flow_reason_);
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

//Given a NH take reference on the NH and set the RPF
bool FlowEntry::SetRpfNHState(FlowTable *ft, const NextHop *nh) {
    Agent *agent = ft->agent();
    const NhState *nh_state = NULL;

    if (nh) {
        nh_state = static_cast<const NhState *>(
                nh->GetState(agent->nexthop_table(),
                             ft->nh_listener_id()));
        // With encap change nexthop can change for route. Route change
        // can come before nh change and it may skip using NH if nhstate is
        // not set. This may result in inconsistent flow-NH map.
        // So add new state if active nexthop in route does not have it.
        if (!nh->IsDeleted() && !nh_state) {
            DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
            NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
            NextHop * new_nh = static_cast<NextHop *>(agent->
                                    nexthop_table()->FindActiveEntry(nh_key));

            NhState *new_nh_state = new NhState(new_nh);
            new_nh->SetState(agent->nexthop_table(),
                             ft->nh_listener_id(),
                             new_nh_state);
            nh_state = new_nh_state;
        }
    }

    if (data_.nh_state_ != nh_state) {
        data_.nh_state_ = nh_state;
        return true;
    }
    return false;
}

bool FlowEntry::SetRpfNH(FlowTable *ft, const AgentRoute *rt) {
    bool ret = false;

    if (data().ecmp_rpf_nh_ != 0) {
        //Set RPF NH based on reverese flow route
        return ret;
    }

    if (!rt) {
        return SetRpfNHState(ft, NULL);
    }

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
        if (ip_rt &&
                ip_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE) {
            //L2 flow cant point to composite NH, set RPF NH based on
            //layer 2 route irrespective prefix lenght of layer 3 route,
            //this is to avoid packet drop in scenario where transition
            //happened from non-ecmp to ECMP.
        } else if (is_flags_set(FlowEntry::IngressDir) ||
                (ip_rt && ip_rt->IsHostRoute())) {
            rt = ip_rt;
            if (rt) {
                data_.l2_rpf_plen = rt->plen();
            }
        }
    }

    const NextHop *nh = NULL;
    if (rt && rt->GetActiveNextHop()) {
        nh = rt->GetActiveNextHop();
    }

    if (key_.family != Address::INET) {
        //TODO:IPV6
        return false;
    }

    return SetRpfNHState(ft, nh);
}

bool FlowEntry::SetEcmpRpfNH(FlowTable *ft, uint32_t nh_id) {
    if (!nh_id) {
        return SetRpfNHState(ft, NULL);
    }

    const NextHop *nh = ft->agent()->nexthop_table()->FindNextHop(nh_id);
    return SetRpfNHState(ft, nh);
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
    data_.ecmp_rpf_nh_ = 0;
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

    if (info->linklocal_bind_local_port) {
        linklocal_src_port_ = info->nat_sport;
        linklocal_src_port_fd_ = info->linklocal_src_port_fd;
        Agent::GetInstance()->pkt()->flow_table()->AddLinkLocalFlowInfo(
            linklocal_src_port_fd_, flow_handle_, key_, stats_.setup_time);
        set_flags(FlowEntry::LinkLocalBindLocalSrcPort);
    } else {
        reset_flags(FlowEntry::LinkLocalBindLocalSrcPort);
    }

    if (InitFlowCmn(info, ctrl, rev_ctrl) == false) {
        return;
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
        agent_->stats()->UpdateFlowAddMinMaxStats(flow->stats_.setup_time);
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

RouteFlowInfo *FlowTable::RouteFlowInfoFind(RouteFlowKey &key) {
    RouteFlowInfo rt_key(key);
    return route_flow_tree_.Find(&rt_key);
}

void FlowTable::DeleteInternal(FlowEntryMap::iterator &it, uint64_t time)
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
    agent_->stats()->UpdateFlowDelMinMaxStats(time);
}

bool FlowTable::Delete(const FlowKey &key, bool del_reverse_flow)
{
    FlowEntryMap::iterator it;
    FlowEntry *fe;
    uint64_t time = 0;

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
    if (!fe->deleted() || (reverse_flow && !reverse_flow->deleted())) {
        time = UTCTimestampUsec();
        SendFlows(fe, reverse_flow, time);
    }
    /* Delete the forward flow */
    DeleteInternal(it, time);

    if (!reverse_flow) {
        return true;
    }

    it = flow_entry_map_.find(reverse_flow->key());
    if (it != flow_entry_map_.end()) {
        DeleteInternal(it, time);
        return true;
    }
    return false;
}

void FlowTable::SendFlowInternal(FlowEntry *fe, uint64_t time)
{
    FlowStatsCollector *fec = agent_->flow_stats_collector();
    uint64_t diff_bytes, diff_packets;
    fec->UpdateFlowStats(fe, diff_bytes, diff_packets);

    fe->stats_.teardown_time = time;
    FlowExport(fe, diff_bytes, diff_packets);
    /* Reset stats and teardown_time after these information is exported during
     * flow delete so that if the flow entry is reused they point to right
     * values */
    fe->ResetStats();
    fe->stats_.teardown_time = 0;
}

void FlowTable::SendFlows(FlowEntry *flow, FlowEntry *rflow, uint64_t time)
{
    SendFlowInternal(flow, time);
    if (rflow) {
        SendFlowInternal(rflow, time);
    }
}

void FlowTable::DeleteAll()
{
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
        const FlowKey &fekey = (*fe_tree_it)->key();
        ++fe_tree_it;
        Delete(fekey, true);
    }
}

SandeshTraceBufferPtr FlowTraceBuf(SandeshTraceBufferCreate("Flow", 5000));

void FlowTable::Init() {

    FlowEntry::alloc_count_ = 0;

    acl_listener_id_ = agent_->acl_table()->Register
        (boost::bind(&FlowTable::AclNotify, this, _1, _2));

    intf_listener_id_ = agent_->interface_table()->Register
        (boost::bind(&FlowTable::IntfNotify, this, _1, _2));

    vn_listener_id_ = agent_->vn_table()->Register
        (boost::bind(&FlowTable::VnNotify, this, _1, _2));

    vrf_listener_id_ = agent_->vrf_table()->Register
            (boost::bind(&FlowTable::VrfNotify, this, _1, _2));

    nh_listener_ = new NhListener();

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
        state->vrf_assign_acl_ = vm_port->vrf_assign_acl();
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
        if (state->vrf_assign_acl_.get() != vm_port->vrf_assign_acl()) {
            changed = true;
            state->vrf_assign_acl_ = vm_port->vrf_assign_acl();
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
    bool enable_rpf = true;
    bool flood_unknown_unicast = false;

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
        enable_rpf = state->enable_rpf_;
        flood_unknown_unicast = state->flood_unknown_unicast_;
    }

    const AclDBEntry *new_acl = vn->GetAcl();
    const AclDBEntry *new_macl = vn->GetMirrorAcl();
    const AclDBEntry *new_mcacl = vn->GetMirrorCfgAcl();
    bool new_enable_rpf = vn->enable_rpf();
    bool new_flood_unknown_unicast = vn->flood_unknown_unicast();
    
    if (state == NULL) {
        state = new VnFlowHandlerState(new_acl, new_macl, new_mcacl,
                                       new_enable_rpf,
                                       new_flood_unknown_unicast);
        e->SetState(part->parent(), vn_listener_id_, state);
    }

    if (acl != new_acl || macl != new_macl || mcacl !=new_mcacl ||
        enable_rpf != new_enable_rpf ||
        flood_unknown_unicast != new_flood_unknown_unicast) {
        state->acl_ = new_acl;
        state->macl_ = new_macl;
        state->mcacl_ = new_mcacl;
        state->enable_rpf_ = new_enable_rpf;
        state->flood_unknown_unicast_ = new_flood_unknown_unicast;
        if (flood_unknown_unicast != new_flood_unknown_unicast) {
            std::cout << "Flood flag changed\n";
        }
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

void FlowTable::DeleteFlow(const FlowEntry *flow) {
    /* We need not reset bitmaps on flow deletion. We will have to
     * provide introspect to reset this */
}

RouteFlowInfo *FlowTable::FindRouteFlowInfo(RouteFlowInfo *key) {
    return route_flow_tree_.LPMFind(key);
}

////////////////////////////////////////////////////////////////////////////
// RouteFlowKey methods
////////////////////////////////////////////////////////////////////////////
bool RouteFlowKey::FlowSrcMatch(const FlowEntry *flow, bool rpf_check) const {
    if (flow->data().flow_source_vrf != vrf)
        return false;

    if (flow->l3_flow() == false) {
        if (flow->data().smac == mac) {
            return true;
        }
        if (rpf_check == false) {
            return false;
        }
    }

    uint8_t flow_plen = flow->data().source_plen;
    if (rpf_check && flow->l3_flow() == false) {
        //Prefix of route used in rpf
        //for layer 2 flow would be stored in
        //l2  rpf prefix len variable
        flow_plen = flow->data().l2_rpf_plen;
    }

    if (flow_plen != plen ||
        flow->key().family != family)
        return false;

    if (flow->key().family == Address::INET) {
        return (Address::GetIp4SubnetAddress(flow->key().src_addr.to_v4(),
                                             flow_plen) == ip);
    } else if (flow->key().family == Address::INET6) {
        return (Address::GetIp6SubnetAddress(flow->key().src_addr.to_v6(),
                                             flow_plen) == ip);
    } else {
        assert(0);
    }
    return false;
}

bool RouteFlowKey::FlowDestMatch(const FlowEntry *flow) const {
    if (flow->data().flow_dest_vrf != vrf)
        return false;

    if (flow->l3_flow() == false) {
        return (flow->data().dmac == mac);
    }

    if (flow->data().dest_plen != plen ||
        flow->key().family != family)
        return false;

    if (flow->key().family == Address::INET) {
        return (Address::GetIp4SubnetAddress(flow->key().dst_addr.to_v4(),
                                             flow->data().dest_plen) == ip);
    } else if (flow->key().family == Address::INET6) {
        return (Address::GetIp6SubnetAddress(flow->key().dst_addr.to_v6(),
                                             flow->data().dest_plen) == ip);
    } else {
        assert(0);
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////
// RouteFlowUpdate class responsible to keep flow in-sync with route
// add/delete/change
////////////////////////////////////////////////////////////////////////////
RouteFlowUpdate::RouteFlowUpdate(AgentRouteTable *table):
    id_(DBTableBase::kInvalidId),
    rt_table_(table), rt_table_deleted_(false),
    table_delete_ref_(this, rt_table_->deleter()), walk_id_(-1) {
}

RouteFlowUpdate::~RouteFlowUpdate() {
    table_delete_ref_.Reset(NULL);
}

void RouteFlowUpdate::ManagedDelete() {
    rt_table_deleted_ = true;
}

bool RouteFlowUpdate::DeleteState(DBTablePartBase *partition,
                                  DBEntryBase *entry, RouteFlowUpdate *info) {
    State *state = static_cast<State *>
        (entry->GetState(partition->parent(), info->id_));
    if (state) {
        entry->ClearState(partition->parent(), info->id_);
        delete state;
    }
    return true;
}

void RouteFlowUpdate::WalkDone(DBTableBase *partition, RouteFlowUpdate *info) {
    LOG(DEBUG, "ROUTE-FLOW-UPDATE: WalkDone : Entry <" << info
        << "> Table : " << info->rt_table_->name()
        << " Walk : " << info->walk_id());
    assert(info->rt_table_);
    info->rt_table_->Unregister(info->id_);
    info->id_ = DBTableBase::kInvalidId;
    info->walk_id_ = -1;
    delete info;
}

void RouteFlowUpdate::Notify(DBTablePartBase *partition, DBEntryBase *e) {
    AgentRoute *route = static_cast<AgentRoute *>(e);
    if (route->is_multicast()) {
        return;
    }

    const AgentPath *path = route->GetActivePath();
    // Get new sg-list. Sort, the sg-list to aid in comparison
    SecurityGroupList new_sg_l;
    if (path) {
        new_sg_l = route->GetActivePath()->sg_list();
        sort(new_sg_l.begin(), new_sg_l.end());
    }

    TraceMsg(route, path, new_sg_l);

    if (rt_table_deleted_ || route->IsDeleted()) {
        // Route deleted
        RouteDel(route);
        DeleteState(partition, e, this);
        return;
    }

    State *state = static_cast<State *>(e->GetState(partition->parent(), id_));
    if (state == NULL) {
        state  = new State();
        route->SetState(partition->parent(), id_, state);
        RouteAdd(route);
    }

    // Handle SG change
    if (state->sg_l_ != new_sg_l) {
        SgChange(route, new_sg_l);
        state->sg_l_ = new_sg_l;
    }

    //Trigger RPF NH sync, if active nexthop changes
    const NextHop *active_nh = route->GetActiveNextHop();
    const NextHop *local_nh = NULL;
    if (active_nh->GetType() == NextHop::COMPOSITE) {
        InetUnicastRouteEntry *inet_route =
            dynamic_cast<InetUnicastRouteEntry *>(route);
        assert(inet_route);
        //If destination is ecmp, all remote flow would
        //have RPF NH set to that local component NH
        local_nh = inet_route->GetLocalNextHop();
    }

    if ((state->active_nh_ != active_nh) || (state->local_nh_ != local_nh)) {
        NhChange(route, active_nh, local_nh);
        state->active_nh_ = active_nh;
        state->local_nh_ = local_nh;
    }
}

////////////////////////////////////////////////////////////////////////////
// InetRouteFlowUpdate class responsible to keep flow in-sync with Inet route
// add/delete/change
////////////////////////////////////////////////////////////////////////////
void InetRouteFlowUpdate::TraceMsg(AgentRoute *entry, const AgentPath *path,
                                   SecurityGroupList &sg_list) {
    InetUnicastRouteEntry *route = static_cast<InetUnicastRouteEntry *>(entry);
    FLOW_TRACE(RouteUpdate,
               route->vrf()->GetName(),
               route->addr().to_string(),
               route->plen(),
               path ? path->dest_vn_name() : "",
               route->IsDeleted(),
               rt_table_deleted_,
               sg_list.size(),
               sg_list);
}

void InetRouteFlowUpdate::RouteAdd(AgentRoute *entry) {
    Agent *agent = static_cast<AgentRouteTable *>(entry->get_table())->agent();
    InetUnicastRouteEntry *route = static_cast<InetUnicastRouteEntry *>(entry);
    // Find the RouteFlowInfo for the covering route and trigger flow
    // re-compute to use more specific route. use (prefix_len -1) in LPM
    // to get covering route.
    // Skip default route, as there will be no covering route for it.
    if (route->plen() != 0) {
        RouteFlowInfo rt_key(route->vrf()->vrf_id(), route->addr(),
                             route->plen() - 1);
        RouteFlowInfo *rt_info =
            agent->pkt()->flow_table()->FindRouteFlowInfo(&rt_key);
        agent->pkt()->flow_table()->FlowRecompute(rt_info, &rt_key.key);
    }
}

void InetRouteFlowUpdate::RouteDel(AgentRoute *entry) {
    FLOW_TRACE(ModuleInfo, "Delete Route flows");
    Agent *agent = static_cast<AgentRouteTable *>(entry->get_table())->agent();
    InetUnicastRouteEntry *route = static_cast<InetUnicastRouteEntry *>(entry);

    RouteFlowInfo rt_key(RouteFlowKey(route->vrf()->vrf_id(), route->addr(),
                                      route->plen()));
    FlowTable *table = agent->pkt()->flow_table();
    RouteFlowInfo *rt_info = table->route_flow_tree_.Find(&rt_key);
    agent->pkt()->flow_table()->FlowRecompute(rt_info, NULL);
}

bool InetRouteFlowUpdate::SgUpdate(FlowEntry *fe, FlowTable *table,
                                   RouteFlowKey &key,
                                   const SecurityGroupList &sg_list) {
    if (fe->l3_flow() == false) {
        return true;
    }

    fe->GetPolicyInfo();

    // Update SG-ID List
    if (key.FlowSrcMatch(fe)) {
        fe->set_source_sg_id_l(sg_list);
    } else if (key.FlowDestMatch(fe)) {
        fe->set_dest_sg_id_l(sg_list);
    } else {
        FLOW_TRACE(Err, fe->flow_handle(), "Not found route key, vrf :"
                   + integerToString(key.vrf) + " ip:" + key.ip.to_string());
    }

    // Update SG id for reverse flow
    // So that SG match rules can be applied on the
    // latest sg id of forward and reverse flow
    FlowEntry *rev_flow = fe->reverse_flow_entry();
    if (rev_flow && key.FlowSrcMatch(rev_flow)) {
        rev_flow->set_source_sg_id_l(sg_list);
    } else if (rev_flow && key.FlowDestMatch(rev_flow)) {
        rev_flow->set_dest_sg_id_l(sg_list);
    }

    // SG id for a reverse flow is updated
    // Reevaluate forward flow
    if (fe->is_flags_set(FlowEntry::ReverseFlow)) {
        FlowEntry *fwd_flow = fe->reverse_flow_entry();
        if (fwd_flow) {
            table->ResyncAFlow(fwd_flow);
            table->AddFlowInfo(fwd_flow);
        }
    }

    return true;
}

void InetRouteFlowUpdate::SgChange(AgentRoute *entry,
                                   SecurityGroupList &sg_list) {
    Agent *agent = static_cast<AgentRouteTable *>(entry->get_table())->agent();
    InetUnicastRouteEntry *route = static_cast<InetUnicastRouteEntry *>(entry);
    RouteFlowKey key(route->vrf()->vrf_id(), route->addr(), route->plen());
    FlowTable *table = agent->pkt()->flow_table();
    table->IterateFlowInfoEntries(key, boost::bind(&InetRouteFlowUpdate::SgUpdate,
                                                 this, _1, table, key,
                                                 sg_list));
}

void InetRouteFlowUpdate::NhChange(AgentRoute *entry, const NextHop *active_nh,
                                   const NextHop *local_nh) {
    Agent *agent = static_cast<AgentRouteTable *>(entry->get_table())->agent();
    InetUnicastRouteEntry *route = static_cast<InetUnicastRouteEntry *>(entry);
    RouteFlowKey key(route->vrf()->vrf_id(), route->addr(), route->plen());
    agent->pkt()->flow_table()->ResyncEcmpInfo(key, route);
    agent->pkt()->flow_table()->ResyncRpfNH(key, route);
}

////////////////////////////////////////////////////////////////////////////
// BridgeEntryFlowUpdate class responsible to keep flow in-sync with Inet route
// add/delete/change
////////////////////////////////////////////////////////////////////////////
void BridgeEntryFlowUpdate::TraceMsg(AgentRoute *entry, const AgentPath *path,
                                   SecurityGroupList &sg_list) {
    BridgeRouteEntry *route = static_cast<BridgeRouteEntry *>(entry);
    FLOW_TRACE(RouteUpdate,
               route->vrf()->GetName(),
               route->mac().ToString(),
               route->plen(),
               path ? path->dest_vn_name() : "",
               route->IsDeleted(),
               rt_table_deleted_,
               sg_list.size(),
               sg_list);
}

void BridgeEntryFlowUpdate::RouteAdd(AgentRoute *entry) {
    Agent *agent = static_cast<AgentRouteTable *>(entry->get_table())->agent();
    BridgeRouteEntry *route = static_cast< BridgeRouteEntry*>(entry);
    FlowTable *table = agent->pkt()->flow_table();
    RouteFlowKey key(route->vrf()->vrf_id(), route->mac());
    RouteFlowInfo rt_key(key);
    RouteFlowInfo *rt_info =
        table->route_flow_tree_.Find(&rt_key);
    if (rt_info == NULL) {
        return;
    }
    agent->pkt()->flow_table()->FlowL2Recompute(rt_info);
}

bool BridgeEntryFlowUpdate::DelEntry(FlowEntry *fe, FlowTable *table,
                                   RouteFlowKey &key) {
    table->Delete(fe->key(), true);
    return false;
}

void BridgeEntryFlowUpdate::RouteDel(AgentRoute *entry) {
    FLOW_TRACE(ModuleInfo, "Delete Route flows");
    Agent *agent = static_cast<AgentRouteTable *>(entry->get_table())->agent();
    BridgeRouteEntry *route = static_cast<BridgeRouteEntry *>(entry);
    RouteFlowKey key(route->vrf()->vrf_id(), route->mac());
    FlowTable *table = agent->pkt()->flow_table();
    table->IterateFlowInfoEntries(key,
                                  boost::bind(&BridgeEntryFlowUpdate::DelEntry,
                                              this, _1, table, key));
    RouteFlowInfo rt_key(key);
    RouteFlowInfo *rt_info = table->route_flow_tree_.Find(&rt_key);
    if (rt_info == NULL) {
        return;
    }
    table->route_flow_tree_.Remove(rt_info);
}

bool BridgeEntryFlowUpdate::SgUpdate(FlowEntry *fe, FlowTable *table,
                                   RouteFlowKey &key,
                                   const SecurityGroupList &sg_list) {
    if (fe->l3_flow() == true) {
        return true;
    }
    fe->GetPolicyInfo();

    // Update SG-ID List
    if (key.FlowSrcMatch(fe)) {
        fe->set_source_sg_id_l(sg_list);
    } else if (key.FlowDestMatch(fe)) {
        fe->set_dest_sg_id_l(sg_list);
    } else {
        FLOW_TRACE(Err, fe->flow_handle(), "Not found route key, vrf :"
                   + integerToString(key.vrf) + " ip:" + key.ip.to_string());
    }

    // Update SG id for reverse flow
    // So that SG match rules can be applied on the
    // latest sg id of forward and reverse flow
    FlowEntry *rev_flow = fe->reverse_flow_entry();
    if (rev_flow && key.FlowSrcMatch(rev_flow)) {
        rev_flow->set_source_sg_id_l(sg_list);
    } else if (rev_flow && key.FlowDestMatch(rev_flow)) {
        rev_flow->set_dest_sg_id_l(sg_list);
    }

    // SG id for a reverse flow is updated
    // Reevaluate forward flow
    if (fe->is_flags_set(FlowEntry::ReverseFlow)) {
        FlowEntry *fwd_flow = fe->reverse_flow_entry();
        if (fwd_flow) {
            table->ResyncAFlow(fwd_flow);
            table->AddFlowInfo(fwd_flow);
        }
    }

    return true;
}

void BridgeEntryFlowUpdate::SgChange(AgentRoute *entry,
                                   SecurityGroupList &sg_list) {
    Agent *agent = static_cast<AgentRouteTable *>(entry->get_table())->agent();
    FlowTable *table = agent->pkt()->flow_table();
    BridgeRouteEntry *route = static_cast<BridgeRouteEntry *>(entry);
    RouteFlowKey key(route->vrf()->vrf_id(), route->mac());
    table->IterateFlowInfoEntries(key,
                                  boost::bind(&BridgeEntryFlowUpdate::SgUpdate,
                                              this, _1, table, key, sg_list));
}

void BridgeEntryFlowUpdate::NhChange(AgentRoute *entry, const NextHop *active_nh,
                                   const NextHop *local_nh) {
    Agent *agent = static_cast<AgentRouteTable *>(entry->get_table())->agent();
    BridgeRouteEntry *route = static_cast<BridgeRouteEntry *>(entry);
    RouteFlowKey key(route->vrf()->vrf_id(), route->mac());
    agent->pkt()->flow_table()->ResyncRpfNH(key, route);
}

////////////////////////////////////////////////////////////////////////////
// VRF Notification handlers
////////////////////////////////////////////////////////////////////////////
// Register to the route tables
void FlowTable::VrfFlowHandlerState::Register(VrfEntry *vrf) {
    // Register to the Inet4 Unicast Table
    InetUnicastAgentRouteTable *inet_table =
        static_cast<InetUnicastAgentRouteTable *>
        (vrf->GetInet4UnicastRouteTable());
    inet4_unicast_update_ = new InetRouteFlowUpdate(inet_table);
    inet4_unicast_update_->set_dblistener_id
        (inet_table->Register(boost::bind(&RouteFlowUpdate::Notify,
                                         inet4_unicast_update_, _1, _2)));

    // Register to the Bridge Unicast Table
    BridgeAgentRouteTable *bridge_table =
        static_cast<BridgeAgentRouteTable *>
        (vrf->GetBridgeRouteTable());
    bridge_update_ = new BridgeEntryFlowUpdate(bridge_table);
    bridge_update_->set_dblistener_id
        (bridge_table->Register(boost::bind(&RouteFlowUpdate::Notify,
                                          bridge_update_, _1, _2)));
    LOG(DEBUG, "ROUTE-FLOW-UPDATE"
        << " Inet : " << inet4_unicast_update_
        << " Listener : " << inet4_unicast_update_->dblistener_id()
        << " Bridge : " << bridge_update_
        << " Listener : " << bridge_update_->dblistener_id());
}

// VRF being deleted. Do the cleanup
void FlowTable::VrfFlowHandlerState::Unregister(VrfEntry *vrf) {
    Agent *agent = static_cast<VrfTable *>(vrf->get_table())->agent();

    // TODO : Is this really needed? Routes will anyway be deleted
    // VRF is deleted. Delete DBState for all the route entries
    DBTableWalker *walker = agent->db()->GetWalker();
    DBTableWalker::WalkId id;
    id = walker->WalkTable(vrf->GetInet4UnicastRouteTable(), NULL,
                      boost::bind(&RouteFlowUpdate::DeleteState,
                                  _1, _2, inet4_unicast_update_),
                      boost::bind(&RouteFlowUpdate::WalkDone, _1,
                                  inet4_unicast_update_));
    inet4_unicast_update_->set_walk_id(id);

    DBTableWalker *bridge_walker = agent->db()->GetWalker();
    id = bridge_walker->WalkTable(vrf->GetBridgeRouteTable(), NULL,
                           boost::bind(&RouteFlowUpdate::DeleteState,
                                       _1, _2, bridge_update_),
                           boost::bind(&RouteFlowUpdate::WalkDone, _1,
                                       bridge_update_));
    bridge_update_->set_walk_id(id);
    LOG(DEBUG, "ROUTE-FLOW-UPDATE: Walk started for"
        << " INET : <" << inet4_unicast_update_
        << " Wak-Id : " << inet4_unicast_update_->walk_id()
        << " Bridge : " << bridge_update_
        << " Walk-Id : " << bridge_update_->walk_id());
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
        state->Unregister(vrf);
        e->ClearState(part->parent(), vrf_listener_id_);
        delete state;
        return;
    }
    if (state == NULL) {
        state = new VrfFlowHandlerState();
        state->Register(vrf);
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
        //Mark the flow as short if flood unknown
        //unicast flag is reset
        if (vn->flood_unknown_unicast() == false &&
            fe->is_flags_set(FlowEntry::UnknownUnicastFlood)) {
            fe->MakeShortFlow(FlowEntry::SHORT_NO_DST_ROUTE);
            fe->GetPolicyInfo(vn);
            ResyncAFlow(fe);
            continue;
        }
        fe->GetPolicyInfo(vn);
        ResyncAFlow(fe);
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
        fe->GetPolicyInfo();
        ResyncAFlow(fe);
        AddFlowInfo(fe);
        FlowInfo flow_info;
        fe->FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Evaluate Acl Flows", flow_info);
    }
}

void FlowTable::ResyncEcmpInfo(const RouteFlowKey &key, const AgentRoute *rt) {
    RouteFlowInfo *rt_info;
    RouteFlowInfo rt_key(key);
    rt_info = route_flow_tree_.Find(&rt_key);
    if (rt_info == NULL) {
        return;
    }
    FlowEntryTree fet = rt_info->fet;
    FlowEntryTree::iterator fet_it, it;
    it = fet.begin();
    while (it != fet.end()) {
        fet_it = it++;
        FlowEntry *flow = (*fet_it).get();
        UpdateEcmpInfo(flow);
        FlowEntry *rflow = flow->reverse_flow_entry();
        if (rflow) {
            UpdateEcmpInfo(rflow);
        }
    }
}

void FlowTable::ResyncRpfNH(const RouteFlowKey &key, const AgentRoute *rt) {
    RouteFlowInfo *rt_info;
    RouteFlowInfo rt_key(key);
    rt_info = route_flow_tree_.Find(&rt_key);
    if (rt_info == NULL) {
        return;
    }
    FlowEntryTree fet = rt_info->fet;
    FlowEntryTree::iterator fet_it, it;
    it = fet.begin();
    while (it != fet.end()) {
        fet_it = it++;
        FlowEntry *flow = (*fet_it).get();
        if (key.FlowSrcMatch(flow, true) == false) {
            continue;
        }

        if (flow->SetRpfNH(this, rt) == true) {
            flow->UpdateKSync(this);
            FlowInfo flow_info;
            flow->FillFlowInfo(flow_info);
            FLOW_TRACE(Trace, "Resync RPF NH", flow_info);
        }
    }
}

bool FlowTable::NeedsReCompute(const FlowEntry *flow, const RouteFlowKey *key) {
    if (key == NULL)
        return true;

    if (key->Match(flow->key().src_addr)) {
        return true;
    }

    if (key->Match(flow->key().dst_addr)) {
        return true;
    }

    const FlowEntry *rflow = flow->reverse_flow_entry();
    if (rflow == NULL)
        return true;

    if (key->Match(rflow->key().src_addr)) {
        return true;
    }

    if (key->Match(rflow->key().dst_addr)) {
        return true;
    }

    return false;
}

void FlowTable::FlowRecompute(RouteFlowInfo *rt_info,
                              const RouteFlowKey *rt_key) {
    if (rt_info == NULL) {
        return;
    }
    FlowEntryTree fet = rt_info->fet;
    FlowEntryTree::iterator it;
    it = fet.begin();
    for (;it != fet.end(); ++it) {
        FlowEntry *fe = (*it).get();
        if (fe->is_flags_set(FlowEntry::ShortFlow)) {
            continue;
        }
        if (NeedsReCompute(fe, rt_key) == false) {
            continue;
        }
        if (fe->is_flags_set(FlowEntry::ReverseFlow)) {
            /* for reverse flow trigger a re-eval on its forward flow */
            fe = fe->reverse_flow_entry();
        }
        if (fe->l3_flow() == false) {
            continue;
        }
        if (fe->set_pending_recompute(true)) {
            agent_->pkt()->pkt_handler()->SendMessage(PktHandler::FLOW,
                    new FlowTaskMsg(fe));
        }
    }
}

void FlowTable::FlowL2Recompute(RouteFlowInfo *rt_info) {
    if (rt_info == NULL) {
        return;
    }
    FlowEntryTree fet = rt_info->fet;
    FlowEntryTree::iterator it;
    it = fet.begin();
    for (;it != fet.end(); ++it) {
        FlowEntry *fe = (*it).get();
        if (fe->is_flags_set(FlowEntry::ShortFlow)) {
            continue;
        }
        if (fe->is_flags_set(FlowEntry::UnknownUnicastFlood) == false) {
            continue;
        }
        if (fe->is_flags_set(FlowEntry::ReverseFlow)) {
            /* for reverse flow trigger a re-eval on its forward flow */
            fe = fe->reverse_flow_entry();
        }
        if (fe->set_pending_recompute(true)) {
            agent_->pkt()->pkt_handler()->SendMessage(PktHandler::FLOW,
                    new FlowTaskMsg(fe));
        }
    }
}


// Iterate thru Inet FlowEntryTree and invoke call back for each flow entry
void FlowTable::IterateFlowInfoEntries(const RouteFlowKey &key, FlowEntryCb cb) {
    RouteFlowInfo rt_key(key);
    RouteFlowInfo *rt_info = route_flow_tree_.Find(&rt_key);
    if (rt_info == NULL) {
        return;
    }

    FlowEntryTree fet = rt_info->fet;
    FlowEntryTree::iterator fet_it, it;
    it = fet.begin();
    while (it != fet.end()) {
        fet_it = it++;
        FlowEntry *fe = (*fet_it).get();
        FlowInfo flow_info;
        fe->FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Evaluate Route Flows", flow_info);
        DeleteFlowInfo(fe);
        if (cb(fe) == false) {
            fet.erase(fet_it);
        } else {
            ResyncAFlow(fe);
            AddFlowInfo(fe);
        }
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
        // Local flow needs to evaluate fwd flow then reverse flow
        if (fe->is_flags_set(FlowEntry::LocalFlow) && 
            fe->is_flags_set(FlowEntry::ReverseFlow)) {
            FlowEntry *fwd_flow = fe->reverse_flow_entry();
            if (fwd_flow) {
                DeleteFlowInfo(fwd_flow);
                fwd_flow->GetPolicyInfo();
                ResyncAFlow(fwd_flow);
                AddFlowInfo(fwd_flow);
                FlowInfo flow_info;
                fwd_flow->FillFlowInfo(flow_info);
                FLOW_TRACE(Trace, "Evaluate VmPort Flows", flow_info);
            }
        }
        DeleteFlowInfo(fe);
        fe->GetPolicyInfo(intf->vn());
        ResyncAFlow(fe);
        AddFlowInfo(fe);
        FlowInfo flow_info;
        fe->FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Evaluate VmPort Flows", flow_info);
    }
}


void FlowTable::DeleteFlowInfo(FlowEntry *fe) 
{
    DeleteFlow(fe);
    // Remove from AclFlowTree
    // Go to all matched ACL list and remove from all acls
    std::list<MatchAclParams>::const_iterator acl_it;
    for (acl_it = fe->match_p().m_acl_l.begin(); acl_it != fe->match_p().m_acl_l.end();
         ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }
    for (acl_it = fe->match_p().m_sg_acl_l.begin(); 
         acl_it != fe->match_p().m_sg_acl_l.end();
         ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }

    for (acl_it = fe->match_p().m_out_acl_l.begin();
         acl_it != fe->match_p().m_out_acl_l.end(); ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }
    for (acl_it = fe->match_p().m_out_sg_acl_l.begin(); 
         acl_it != fe->match_p().m_out_sg_acl_l.end();
         ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }

    for (acl_it = fe->match_p().m_reverse_sg_acl_l.begin();
         acl_it != fe->match_p().m_reverse_sg_acl_l.end(); ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }
    for (acl_it = fe->match_p().m_reverse_out_sg_acl_l.begin();
         acl_it != fe->match_p().m_reverse_out_sg_acl_l.end();
         ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }

    for (acl_it = fe->match_p().m_mirror_acl_l.begin(); 
         acl_it != fe->match_p().m_mirror_acl_l.end();
         ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }

    for (acl_it = fe->match_p().m_out_mirror_acl_l.begin(); 
         acl_it != fe->match_p().m_out_mirror_acl_l.end();
         ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }
    for (acl_it = fe->match_p().m_vrf_assign_acl_l.begin();
         acl_it != fe->match_p().m_vrf_assign_acl_l.end();
         ++acl_it) {
        DeleteAclFlowInfo((*acl_it).acl.get(), fe, (*acl_it).ace_id_list);
    }

    // Remove from IntfFlowTree
    DeleteIntfFlowInfo(fe);    
    // Remove from VnFlowTree
    DeleteVnFlowInfo(fe);
    // Remove from VmFlowTree
    DeleteVmFlowInfo(fe);
    // Remove from RouteFlowTree
    DeleteRouteFlowInfo(fe);
}

void FlowTable::DeleteVnFlowInfo(FlowEntry *fe)
{
    VnFlowTree::iterator vn_it;
    if (fe->vn_entry()) {
        vn_it = vn_flow_tree_.find(fe->vn_entry());
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

void FlowTable::DeleteAclFlowInfo(const AclDBEntry *acl, FlowEntry* flow,
        const AclEntryIDList &id_list)
{
    AclFlowTree::iterator acl_it;
    acl_it = acl_flow_tree_.find(acl);
    if (acl_it == acl_flow_tree_.end()) {
        return;
    }

    // Delete flow entry from the Flow entry list
    AclFlowInfo *af_info = acl_it->second;
    AclEntryIDList::const_iterator id_it;
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
    if (fe->intf_entry()) {
        intf_it = intf_flow_tree_.find(fe->intf_entry());
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

void FlowTable::DeleteVmFlowInfo(FlowEntry *fe) {
    if (fe->in_vm_entry()) {
        DeleteVmFlowInfo(fe, fe->in_vm_entry());
    }
    if (fe->out_vm_entry()) {
        DeleteVmFlowInfo(fe, fe->out_vm_entry());
    }
}

void FlowTable::DeleteVmFlowInfo(FlowEntry *fe, const VmEntry *vm) {
    VmFlowTree::iterator vm_it = vm_flow_tree_.find(vm);
    if (vm_it != vm_flow_tree_.end()) {
        VmFlowInfo *vm_flow_info = vm_it->second;
        if (vm_flow_info->fet.erase(fe)) {
            if (fe->linklocal_src_port()) {
                vm_flow_info->linklocal_flow_count--;
            }
            if (vm_flow_info->fet.empty()) {
                delete vm_flow_info;
                vm_flow_tree_.erase(vm_it);
            }
        }
    }
}

void FlowTable::DeleteInetRouteFlowInfoInternal(FlowEntry *fe,
                                                RouteFlowKey &key) {
    RouteFlowInfo *rt_info;
    RouteFlowInfo rt_key(key);
    rt_info = route_flow_tree_.Find(&rt_key);
    if (rt_info != NULL) {
        rt_info->fet.erase(fe);
        if (rt_info->fet.empty()) {
            route_flow_tree_.Remove(rt_info);
            delete rt_info;
        }
    }
}

void FlowTable::DeleteInetRouteFlowInfo(FlowEntry *fe) {
    RouteFlowKey skey(fe->data().flow_source_vrf,
                      fe->key().src_addr, fe->data().source_plen);
    DeleteInetRouteFlowInfoInternal(fe, skey);
    std::map<int, int>::iterator it;
    for (it = fe->data().flow_source_plen_map.begin();
         it != fe->data().flow_source_plen_map.end(); ++it) {
        RouteFlowKey skey_dep(it->first, fe->key().src_addr, it->second);
        DeleteInetRouteFlowInfoInternal(fe, skey_dep);
    }
   
    RouteFlowKey dkey(fe->data().flow_dest_vrf, fe->key().dst_addr,
                      fe->data().dest_plen);
    DeleteInetRouteFlowInfoInternal(fe, dkey);
    for (it = fe->data().flow_dest_plen_map.begin();
         it != fe->data().flow_dest_plen_map.end(); ++it) {
        RouteFlowKey dkey_dep(it->first, fe->key().dst_addr, it->second);
        DeleteInetRouteFlowInfoInternal(fe, dkey_dep);
    }

    if (fe->acl_assigned_vrf_index() != VrfEntry::kInvalidIndex) {
        RouteFlowKey skey(fe->acl_assigned_vrf_index(),
                          fe->key().src_addr,
                          fe->data().source_plen);
        DeleteInetRouteFlowInfoInternal(fe, skey);
        RouteFlowKey dkey(fe->acl_assigned_vrf_index(),
                          fe->key().dst_addr,
                          fe->data().dest_plen);
        DeleteInetRouteFlowInfoInternal(fe, dkey);
    }

    if (fe->l3_flow() == false) {
        if (fe->data().flow_source_vrf != VrfEntry::kInvalidIndex) {
            RouteFlowKey skey(fe->data().flow_source_vrf, fe->key().src_addr,
                              fe->data().l2_rpf_plen);
            DeleteInetRouteFlowInfoInternal(fe, skey);
        }
    }
}

void FlowTable::DeleteL2RouteFlowInfo(FlowEntry *fe) {
    RouteFlowKey skey(fe->data().flow_source_vrf, fe->data().smac);
    DeleteInetRouteFlowInfoInternal(fe, skey);
    RouteFlowKey dkey(fe->data().flow_dest_vrf, fe->data().dmac);
    DeleteInetRouteFlowInfoInternal(fe, dkey);
}

void FlowTable::DeleteRouteFlowInfo (FlowEntry *fe) {
    if (fe->l3_flow()) {
        DeleteInetRouteFlowInfo(fe);
    } else {
        DeleteInetRouteFlowInfo(fe);
        DeleteL2RouteFlowInfo(fe);
    }
}

void FlowTable::AddFlowInfo(FlowEntry *fe)
{
    NewFlow(fe);
    // Add AclFlowTree
    AddAclFlowInfo(fe);
    // Add IntfFlowTree
    AddIntfFlowInfo(fe);
    // Add VnFlowTree
    AddVnFlowInfo(fe);
    // Add VmFlowTree
    AddVmFlowInfo(fe);
    // Add RouteFlowTree;
    AddRouteFlowInfo(fe);
}

void FlowTable::AddAclFlowInfo (FlowEntry *fe) 
{
    std::list<MatchAclParams>::const_iterator it;
    for (it = fe->match_p().m_acl_l.begin();
         it != fe->match_p().m_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }
    for (it = fe->match_p().m_sg_acl_l.begin();
         it != fe->match_p().m_sg_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }

    for (it = fe->match_p().m_out_acl_l.begin();
         it != fe->match_p().m_out_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }
    for (it = fe->match_p().m_out_sg_acl_l.begin();
         it != fe->match_p().m_out_sg_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }

    for (it = fe->match_p().m_reverse_sg_acl_l.begin();
         it != fe->match_p().m_reverse_sg_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }
    for (it = fe->match_p().m_reverse_out_sg_acl_l.begin();
         it != fe->match_p().m_reverse_out_sg_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }

    for (it = fe->match_p().m_mirror_acl_l.begin();
         it != fe->match_p().m_mirror_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }
    for (it = fe->match_p().m_out_mirror_acl_l.begin();
         it != fe->match_p().m_out_mirror_acl_l.end();
         ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }
    for (it = fe->match_p().m_vrf_assign_acl_l.begin();
            it != fe->match_p().m_vrf_assign_acl_l.end();
            ++it) {
        UpdateAclFlow(it->acl.get(), fe, it->ace_id_list);
    }
}

void FlowTable::UpdateAclFlow(const AclDBEntry *acl, FlowEntry* flow,
                              const AclEntryIDList &id_list)
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
        AclEntryIDList::const_iterator id_it;
        for (id_it = id_list.begin(); id_it != id_list.end(); ++id_it) {
            af_info->aceid_cnt_map[*id_it] += 1;
        }        
    } else {
        af_info->flow_miss++;
    }
}

void FlowTable::AddIntfFlowInfo(FlowEntry *fe)
{
    if (!fe->intf_entry()) {
        return;
    }
    IntfFlowTree::iterator it;
    it = intf_flow_tree_.find(fe->intf_entry());
    IntfFlowInfo *intf_flow_info;
    if (it == intf_flow_tree_.end()) {
        intf_flow_info = new IntfFlowInfo();
        intf_flow_info->intf_entry = fe->intf_entry();
        intf_flow_info->fet.insert(fe);
        intf_flow_tree_.insert(IntfFlowPair(fe->intf_entry(), intf_flow_info));
    } else {
        intf_flow_info = it->second;
        /* fe can already exist. In that case it won't be inserted */
        intf_flow_info->fet.insert(fe);
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
        }
    }
}

void FlowTable::IncrVnFlowCounter(VnFlowInfo *vn_flow_info, 
                                  const FlowEntry *fe) {
    if (fe->is_flags_set(FlowEntry::LocalFlow)) {
        vn_flow_info->ingress_flow_count++;
        vn_flow_info->egress_flow_count++;
    } else {
        if (fe->is_flags_set(FlowEntry::IngressDir)) {
            vn_flow_info->ingress_flow_count++;
        } else {
            vn_flow_info->egress_flow_count++;
        }
    }
}

void FlowTable::DecrVnFlowCounter(VnFlowInfo *vn_flow_info, 
                                  const FlowEntry *fe) {
    if (fe->is_flags_set(FlowEntry::LocalFlow)) {
        vn_flow_info->ingress_flow_count--;
        vn_flow_info->egress_flow_count--;
    } else {
        if (fe->is_flags_set(FlowEntry::IngressDir)) {
            vn_flow_info->ingress_flow_count--;
        } else {
            vn_flow_info->egress_flow_count--;
        }
    }
}

void FlowTable::AddVnFlowInfo (FlowEntry *fe)
{
    if (!fe->vn_entry()) {
        return;
    }    
    VnFlowTree::iterator it;
    it = vn_flow_tree_.find(fe->vn_entry());
    VnFlowInfo *vn_flow_info;
    if (it == vn_flow_tree_.end()) {
        vn_flow_info = new VnFlowInfo();
        vn_flow_info->vn_entry = fe->vn_entry();
        vn_flow_info->fet.insert(fe);
        IncrVnFlowCounter(vn_flow_info, fe);
        vn_flow_tree_.insert(VnFlowPair(fe->vn_entry(), vn_flow_info));
    } else {
        vn_flow_info = it->second;
        /* fe can already exist. In that case it won't be inserted */
        pair<FlowEntryTree::iterator, bool> ret =
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

void FlowTable::AddInetRouteFlowInfoInternal (FlowEntry *fe, RouteFlowKey &key) {
    RouteFlowInfo *rt_info;
    RouteFlowInfo rt_key(key);
    rt_info = route_flow_tree_.Find(&rt_key);
    if (rt_info == NULL) {
        rt_info = new RouteFlowInfo(key);
        rt_info->fet.insert(fe);
        route_flow_tree_.Insert(rt_info);
    } else {
        rt_info->fet.insert(fe);
    }
}

void FlowTable::AddInetRouteFlowInfo (FlowEntry *fe) {
    if (fe->data().flow_source_vrf != VrfEntry::kInvalidIndex) {
        RouteFlowKey skey(fe->data().flow_source_vrf, fe->key().src_addr,
                          fe->data().source_plen);
        AddInetRouteFlowInfoInternal(fe, skey);
    }

    if (fe->acl_assigned_vrf_index() != VrfEntry::kInvalidIndex) {
        RouteFlowKey skey(fe->acl_assigned_vrf_index(),
                          fe->key().src_addr,
                          fe->data().source_plen);
        AddInetRouteFlowInfoInternal(fe, skey);
    }

    std::map<int, int>::iterator it;
    for (it = fe->data().flow_source_plen_map.begin();
         it != fe->data().flow_source_plen_map.end(); ++it) {
        RouteFlowKey skey_dep(it->first, fe->key().src_addr, it->second);
        AddInetRouteFlowInfoInternal(fe, skey_dep);
    }

    if (fe->data().flow_dest_vrf != VrfEntry::kInvalidIndex) {
        RouteFlowKey dkey(fe->data().flow_dest_vrf, fe->key().dst_addr,
                          fe->data().dest_plen);
        AddInetRouteFlowInfoInternal(fe, dkey);
    }

    if (fe->acl_assigned_vrf_index() != VrfEntry::kInvalidIndex) {
        RouteFlowKey skey(fe->acl_assigned_vrf_index(),
                          fe->key().dst_addr,
                          fe->data().dest_plen);
        AddInetRouteFlowInfoInternal(fe, skey);
    }

    for (it = fe->data().flow_dest_plen_map.begin();
         it != fe->data().flow_dest_plen_map.end(); ++it) {
        RouteFlowKey dkey_dep(it->first, fe->key().dst_addr, it->second);
        AddInetRouteFlowInfoInternal(fe, dkey_dep);
    }

    if (fe->l3_flow() == false) {
        if (fe->data().flow_source_vrf != VrfEntry::kInvalidIndex) {
            RouteFlowKey skey(fe->data().flow_source_vrf, fe->key().src_addr,
                              fe->data().l2_rpf_plen);
            AddInetRouteFlowInfoInternal(fe, skey);
        }
    }
}

void FlowTable::AddL2RouteFlowInfo (FlowEntry *fe) {
    if (fe->data().flow_source_vrf != VrfEntry::kInvalidIndex) {
        RouteFlowKey skey(fe->data().flow_source_vrf, fe->data().smac);
        AddInetRouteFlowInfoInternal(fe, skey);
    }

    if (fe->data().flow_dest_vrf != VrfEntry::kInvalidIndex) {
        RouteFlowKey dkey(fe->data().flow_dest_vrf, fe->data().dmac);
        AddInetRouteFlowInfoInternal(fe, dkey);
    }
}

void FlowTable::AddRouteFlowInfo (FlowEntry *fe) {
    if (fe->l3_flow()) {
        AddInetRouteFlowInfo(fe);
    } else {
        //For layer 2 flow, we add flow entry in both
        //inet and layer 2 tree entry. This
        //get s used to resync rpf nexthop for a layer2 flow
        //since its calculated based on layer3 route entry
        AddInetRouteFlowInfo(fe);
        AddL2RouteFlowInfo(fe);
    }
}

void FlowTable::ResyncAFlow(FlowEntry *fe) {
    fe->UpdateRpf();
    fe->DoPolicy();
    UpdateEcmpInfo(fe);
    if (fe->ksync_entry() || fe->is_flags_set(FlowEntry::EcmpFlow) == false) {
        fe->UpdateKSync(this);
    }

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

    if (rflow->ksync_entry() || rflow->is_flags_set(FlowEntry::EcmpFlow) == false) {
        rflow->UpdateKSync(this);
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
        Delete((*fet_it)->key(), true);
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
        Delete((*fet_it)->key(), true);
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
        Delete((*fet_it)->key(), true);
    }
}

DBTableBase::ListenerId FlowTable::nh_listener_id() {
    return nh_listener_->id();
}

// Find L2 Route for the MAC address.
AgentRoute *FlowTable::GetL2Route(const VrfEntry *vrf,
                                  const MacAddress &mac) {
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (vrf->GetBridgeRouteTable());
    return table->FindRoute(mac);
}

//Find EVPN route
AgentRoute* FlowTable::GetEvpnRoute(const VrfEntry *vrf,
                                    const MacAddress &mac,
                                    const IpAddress &ip,
                                    uint32_t ethernet_tag) {
    EvpnAgentRouteTable *table = static_cast<EvpnAgentRouteTable *>(
            vrf->GetEvpnRouteTable());
    return table->FindRoute(mac, ip, ethernet_tag);
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

void FlowEntry::SetAclAction(std::vector<AclAction> &acl_action_l) const
{
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

void FlowEntry::set_ecmp_rpf_nh(FlowTable *table, uint32_t id) {
    if (data_.ecmp_rpf_nh_ == id) {
        return;
    }

    data_.ecmp_rpf_nh_ = id;
    bool update_ksync = false;

    if (!id) {
        const VrfEntry *vrf = table->agent()->vrf_table()->
                                   FindVrfFromId(data().flow_source_vrf);
        if (vrf) {
            //Flow transitioned from ECMP to non ecmp
            InetUnicastRouteEntry *ip_rt = static_cast<InetUnicastRouteEntry *>(
                    table->GetUcRoute(vrf, key().src_addr));
            update_ksync = SetRpfNH(table, ip_rt);
        }
    } else {
        update_ksync = SetEcmpRpfNH(table, id);
    }

    if (ksync_entry() && update_ksync) {
        UpdateKSync(table);
    }
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
        fe.SetAclFlowSandeshData(acl, fe_sandesh_data);

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

FlowTable::FlowTable(Agent *agent) : 
    agent_(agent), flow_entry_map_(), acl_flow_tree_(),
    linklocal_flow_count_(0), acl_listener_id_(),
    intf_listener_id_(), vn_listener_id_(), vm_listener_id_(),
    vrf_listener_id_(), nh_listener_(NULL),
    inet4_route_key_(NULL, Ip4Address(), 32, false),
    inet6_route_key_(NULL, Ip6Address(), 128, false) {
    max_vm_flows_ = (uint32_t)
        (agent->ksync()->flowtable_ksync_obj()->flow_table_entries_count() *
         agent->params()->max_vm_flows()) / 100;
}

FlowTable::~FlowTable() {
}

void FlowTable::Shutdown() {
    agent_->acl_table()->Unregister(acl_listener_id_);
    agent_->interface_table()->Unregister(intf_listener_id_);
    agent_->vn_table()->Unregister(vn_listener_id_);
    agent_->vm_table()->Unregister(vm_listener_id_);
    agent_->vrf_table()->Unregister(vrf_listener_id_);
    delete nh_listener_;
}

bool FlowTable::SetUnderlayPort(FlowEntry *flow, FlowDataIpv4 &s_flow) {
    uint16_t underlay_src_port = 0;
    bool exported = false;
    if (flow->is_flags_set(FlowEntry::LocalFlow)) {
        /* Set source_port as 0 for local flows. Source port is calculated by
         * vrouter irrespective of whether flow is local or not. So for local
         * flows we need to ignore port given by vrouter
         */
        s_flow.set_underlay_source_port(0);
        exported = true;
    } else {
        if (flow->tunnel_type().GetType() != TunnelType::MPLS_GRE) {
            underlay_src_port = flow->underlay_source_port();
            if (underlay_src_port) {
                exported = true;
            }
        } else {
            exported = true;
        }
        s_flow.set_underlay_source_port(underlay_src_port);
    }
    flow->set_underlay_sport_exported(exported);
    return exported;
}

void FlowTable::SetUnderlayInfo(FlowEntry *flow, FlowDataIpv4 &s_flow) {
    string rid = agent_->router_id().to_string();
    uint16_t underlay_src_port = 0;
    if (flow->is_flags_set(FlowEntry::LocalFlow)) {
        s_flow.set_vrouter_ip(rid);
        s_flow.set_other_vrouter_ip(rid);
        /* Set source_port as 0 for local flows. Source port is calculated by
         * vrouter irrespective of whether flow is local or not. So for local
         * flows we need to ignore port given by vrouter
         */
        s_flow.set_underlay_source_port(0);
        flow->set_underlay_sport_exported(true);
    } else {
        s_flow.set_vrouter_ip(rid);
        s_flow.set_other_vrouter_ip(flow->peer_vrouter());
        if (flow->tunnel_type().GetType() != TunnelType::MPLS_GRE) {
            underlay_src_port = flow->underlay_source_port();
            if (underlay_src_port) {
                flow->set_underlay_sport_exported(true);
            }
        } else {
            flow->set_underlay_sport_exported(true);
        }
        s_flow.set_underlay_source_port(underlay_src_port);
    }
    s_flow.set_underlay_proto(flow->tunnel_type().GetType());
}

/* For ingress flows, change the SIP as Nat-IP instead of Native IP */
void FlowTable::SourceIpOverride(FlowEntry *flow, FlowDataIpv4 &s_flow) {
    FlowEntry *rev_flow = flow->reverse_flow_entry();
    if (flow->is_flags_set(FlowEntry::NatFlow) && s_flow.get_direction_ing() &&
        rev_flow) {
        const FlowKey *nat_key = &rev_flow->key();
        if (flow->key().src_addr != nat_key->dst_addr) {
            // TODO: IPV6
            if (flow->key().family == Address::INET) {
                s_flow.set_sourceip(nat_key->dst_addr.to_v4().to_ulong());
            } else {
                s_flow.set_sourceip(0);
            }
        }
    }
}

void FlowTable::FlowExport(FlowEntry *flow, uint64_t diff_bytes,
                           uint64_t diff_pkts) {
    if (agent_->params()->disable_flow_collection())
        return;

    FlowDataIpv4   s_flow;
    SandeshLevel::type level = SandeshLevel::SYS_DEBUG;
    FlowStats &stats = flow->stats_;

    s_flow.set_flowuuid(to_string(flow->flow_uuid()));
    s_flow.set_bytes(stats.bytes);
    s_flow.set_packets(stats.packets);
    s_flow.set_diff_bytes(diff_bytes);
    s_flow.set_diff_packets(diff_pkts);

    // TODO: IPV6
    if (flow->key().family == Address::INET) {
        s_flow.set_sourceip(flow->key().src_addr.to_v4().to_ulong());
        s_flow.set_destip(flow->key().dst_addr.to_v4().to_ulong());
    } else {
        s_flow.set_sourceip(0);
        s_flow.set_destip(0);
    }
    s_flow.set_protocol(flow->key().protocol);
    s_flow.set_sport(flow->key().src_port);
    s_flow.set_dport(flow->key().dst_port);
    s_flow.set_sourcevn(flow->data().source_vn);
    s_flow.set_destvn(flow->data().dest_vn);

    if (stats.intf_in != Interface::kInvalidIndex) {
        Interface *intf = InterfaceTable::GetInstance()->FindInterface(stats.intf_in);
        if (intf && intf->type() == Interface::VM_INTERFACE) {
            VmInterface *vm_port = static_cast<VmInterface *>(intf);
            const VmEntry *vm = vm_port->vm();
            if (vm) {
                s_flow.set_vm(vm->GetCfgName());
            }
        }
    }
    s_flow.set_sg_rule_uuid(flow->sg_rule_uuid());
    s_flow.set_nw_ace_uuid(flow->nw_ace_uuid());

    FlowEntry *rev_flow = flow->reverse_flow_entry();
    if (rev_flow) {
        s_flow.set_reverse_uuid(to_string(rev_flow->flow_uuid()));
    }

    // Flow setup(first) and teardown(last) messages are sent with higher
    // priority.
    if (!stats.exported) {
        s_flow.set_setup_time(stats.setup_time);
        // Set flow action
        std::string action_str;
        GetFlowSandeshActionParams(flow->match_p().action_info,
            action_str);
        s_flow.set_action(action_str);
        stats.exported = true;
        level = SandeshLevel::SYS_ERR;
        SetUnderlayInfo(flow, s_flow);
    } else {
        /* When the flow is being exported for first time, underlay port
         * info is set as part of SetUnderlayInfo. At this point it is possible
         * that port is not yet populated to flow-entry because of either
         * (i) flow-entry has not got chance to be evaluated by
         *     flow-stats-collector
         * (ii) there is no flow entry in vrouter yet
         * (iii) the flow entry in vrouter does not have underlay source port
         *       populated yet
         */
        if (!flow->underlay_sport_exported()) {
            if (SetUnderlayPort(flow, s_flow)) {
                level = SandeshLevel::SYS_ERR;
            }
        }
    }

    if (stats.teardown_time) {
        s_flow.set_teardown_time(stats.teardown_time);
        //Teardown time will be set in flow only when flow is deleted.
        //We need to reset the exported flag when flow is getting deleted to
        //handle flow entry reuse case (Flow add request coming for flows
        //marked as deleted)
        stats.exported = false;
        flow->set_underlay_sport_exported(false);
        level = SandeshLevel::SYS_ERR;
    }

    if (flow->is_flags_set(FlowEntry::LocalFlow)) {
        /* For local flows we need to send two flow log messages.
         * 1. With direction as ingress
         * 2. With direction as egress
         * For local flows we have already sent flow log above with
         * direction as ingress. We are sending flow log below with
         * direction as egress.
         */
        s_flow.set_direction_ing(1);
        SourceIpOverride(flow, s_flow);
        DispatchFlowMsg(level, s_flow);
        s_flow.set_direction_ing(0);
        //Export local flow of egress direction with a different UUID even when
        //the flow is same. Required for analytics module to query flows
        //irrespective of direction.
        s_flow.set_flowuuid(to_string(flow->egress_uuid()));
        DispatchFlowMsg(level, s_flow);
    } else {
        if (flow->is_flags_set(FlowEntry::IngressDir)) {
            s_flow.set_direction_ing(1);
            SourceIpOverride(flow, s_flow);
        } else {
            s_flow.set_direction_ing(0);
        }
        DispatchFlowMsg(level, s_flow);
    }

}

void FlowTable::DispatchFlowMsg(SandeshLevel::type level, FlowDataIpv4 &flow) {
    FLOW_DATA_IPV4_OBJECT_LOG("", level, flow);
}

void FlowTable::SetComponentIndex(FlowEntry *fe, const NextHopKey *nh_key,
                                  uint32_t label, bool mpls_path_select) {
    const VrfEntry *vrf = NULL;
    if (fe->match_p().action_info.action &
            (1 << TrafficAction::VRF_TRANSLATE)) {
        vrf =
            agent()->vrf_table()->FindVrfFromId(fe->acl_assigned_vrf_index());
    } else if (fe->is_flags_set(FlowEntry::NatFlow)) {
        vrf = agent()->vrf_table()->FindVrfFromId(fe->data().dest_vrf);
    } else {
        vrf = agent()->vrf_table()->FindVrfFromId(fe->data().vrf);
    }

    if (vrf == NULL) {
        FlowInfo flow_info;
        fe->FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Invalid reverse while setting ECMP index", flow_info);
        return;
    }

    FlowEntry *rflow = fe->reverse_flow_entry();
    const IpAddress dip = rflow->key().src_addr;
    InetUnicastRouteEntry *rt =
        static_cast<InetUnicastRouteEntry *>(GetUcRoute(vrf, dip));
    if (!rt) {
        rflow->set_ecmp_rpf_nh(this, 0);
        return;
    }

    const NextHop *nh = rt->GetActiveNextHop();
    //Set composite NH based on local mpls label flow
    if (mpls_path_select) {
        nh = rt->GetLocalNextHop();
    }

    if (!nh) {
        rflow->set_ecmp_rpf_nh(this, 0);
        return;
    }

    if (nh->GetType() != NextHop::COMPOSITE) {
        rflow->set_ecmp_rpf_nh(this, nh->id());
        return;
    }

    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    //If remote destination is TUNNEL nexthop frame the key by
    //getting the reverse path mpls label that would be used to
    //send the packet back
    if (nh_key->GetType() == NextHop::TUNNEL) {
        const TunnelNHKey *tun_nh = static_cast<const TunnelNHKey *>(nh_key);
        label = comp_nh->GetRemoteLabel(tun_nh->dip());
    }

    const NextHop *component_nh_ptr = static_cast<NextHop *>(
        agent()->nexthop_table()->FindActiveEntry(nh_key));
    ComponentNH component_nh(label, component_nh_ptr);

    uint32_t idx = 0;
    if (comp_nh->GetIndex(component_nh, idx)) {
        if (fe->data_.component_nh_idx != idx) {
            fe->data_.component_nh_idx = idx;
            if (fe->ksync_entry()) {
                fe->UpdateKSync(this);
            }
        }
        //Update the reverse flow source RPF check based on this
        //composite NH
        rflow->set_ecmp_rpf_nh(this, nh->id());
    } else {
        FlowInfo flow_info;
        fe->FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Invalid reverse while setting ECMP index", flow_info);
    }
}

void FlowTable::SetLocalFlowEcmpIndex(FlowEntry *fe) {
    //There are 2 scenarios possible when the destination
    //interface is a VM interface
    //1> Flow is local flow
    //   In this scenario component NH Index has to be set based
    //   on that of BGP path since packets are getting routed
    //   based on route table
    //2> Flow is remote i.e packet came with mpls label from a tunnel
    //   In this sceanrio if mpls label points to composite NH
    //   pick component index from there, if mpls label points to
    //   interface NH there is no need to set component index
    uint32_t label;
    FlowEntry *rflow = fe->reverse_flow_entry();

    const VmInterface *vm_port = NULL;
    if (rflow->data().intf_entry->type() == Interface::VM_INTERFACE) {
        vm_port =
            static_cast<const VmInterface *>(rflow->data().intf_entry.get());
        label = vm_port->label();
    } else {
        const InetInterface *inet_intf =
            static_cast<const InetInterface*>(rflow->data().intf_entry.get());
        label = inet_intf->label();
    }

    //Find the source NH
    const NextHop *nh = agent()->nexthop_table()->FindNextHop(rflow->key().nh);
    if (nh == NULL) {
        return;
    }
    DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());

    //All component nexthop are policy disabled
    nh_key->SetPolicy(false);
    if (nh->GetType() == NextHop::VLAN) {
        const VlanNH *vlan_nh = static_cast<const VlanNH *>(nh);
        label = vm_port->GetServiceVlanLabel(vlan_nh->GetVrf());
    }

    bool mpls_path = false;
    if (!fe->is_flags_set(FlowEntry::LocalFlow)) {
        mpls_path = true;
    }

    SetComponentIndex(fe, nh_key, label, mpls_path);
}

void FlowTable::SetRemoteFlowEcmpIndex(FlowEntry *fe) {
    uint32_t label;

    //Get tunnel info from reverse flow
    label = 0;
    boost::system::error_code ec;
    Ip4Address dest_ip = Ip4Address::from_string(fe->peer_vrouter_, ec);
    if (ec.value() != 0) {
        return;
    }

    boost::scoped_ptr<NextHopKey> nh_key(
            new TunnelNHKey(agent()->fabric_vrf_name(), agent()->router_id(),
                            dest_ip, false, fe->tunnel_type()));
    SetComponentIndex(fe, nh_key.get(), label, false);
}

void FlowTable::UpdateEcmpInfo(FlowEntry *fe) {
    FlowEntry *rflow = fe->reverse_flow_entry();

    if (fe->is_flags_set(FlowEntry::EcmpFlow) == false ||
        fe->is_flags_set(FlowEntry::ShortFlow) ||
        fe->l3_flow() == false) {
        return;
    }

    if (rflow == NULL) {
        FlowInfo flow_info;
        fe->FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Invalid reverse flow for setting ECMP index", flow_info);
        return;
    }

    bool local_flow = false;

    if (fe->is_flags_set(FlowEntry::LocalFlow) ||
        !fe->is_flags_set(FlowEntry::IngressDir)) {
        local_flow = true;
    }

    if (local_flow) {
        SetLocalFlowEcmpIndex(fe);
    } else {
        SetRemoteFlowEcmpIndex(fe);
    }
}
