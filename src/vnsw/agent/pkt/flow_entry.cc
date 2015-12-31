/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <bitset>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <base/os.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/unordered_map.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>
#include <net/address_util.h>
#include <pkt/flow_table.h>
#include <vrouter/flow_stats/flow_stats_collector.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/ksync/ksync_flow_index_manager.h>

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
#include <pkt/flow_entry.h>
#include <uve/agent_uve.h>
#include <uve/vm_uve_table.h>
#include <uve/vn_uve_table.h>
#include <uve/vrouter_uve_entry.h>

const std::map<FlowEntry::FlowPolicyState, const char*>
    FlowEntry::FlowPolicyStateStr = boost::assign::map_list_of
        (NOT_EVALUATED,            "00000000-0000-0000-0000-000000000000")
        (IMPLICIT_ALLOW,           "00000000-0000-0000-0000-000000000001")
        (IMPLICIT_DENY,            "00000000-0000-0000-0000-000000000002")
        (DEFAULT_GW_ICMP_OR_DNS,   "00000000-0000-0000-0000-000000000003")
        (LINKLOCAL_FLOW,           "00000000-0000-0000-0000-000000000004")
        (MULTICAST_FLOW,           "00000000-0000-0000-0000-000000000005")
        (NON_IP_FLOW,              "00000000-0000-0000-0000-000000000006");

const std::map<uint16_t, const char*>
    FlowEntry::FlowDropReasonStr = boost::assign::map_list_of
        ((uint16_t)DROP_UNKNOWN,                 "UNKNOWN")
        ((uint16_t)SHORT_UNAVIALABLE_INTERFACE,
         "SHORT_UNAVIALABLE_INTERFACE")
        ((uint16_t)SHORT_IPV4_FWD_DIS,       "SHORT_IPV4_FWD_DIS")
        ((uint16_t)SHORT_UNAVIALABLE_VRF,
         "SHORT_UNAVIALABLE_VRF")
        ((uint16_t)SHORT_NO_SRC_ROUTE,       "SHORT_NO_SRC_ROUTE")
        ((uint16_t)SHORT_NO_DST_ROUTE,       "SHORT_NO_DST_ROUTE")
        ((uint16_t)SHORT_AUDIT_ENTRY,        "SHORT_AUDIT_ENTRY")
        ((uint16_t)SHORT_VRF_CHANGE,         "SHORT_VRF_CHANGE")
        ((uint16_t)SHORT_NO_REVERSE_FLOW,    "SHORT_NO_REVERSE_FLOW")
        ((uint16_t)SHORT_REVERSE_FLOW_CHANGE,
         "SHORT_REVERSE_FLOW_CHANGE")
        ((uint16_t)SHORT_NAT_CHANGE,         "SHORT_NAT_CHANGE")
        ((uint16_t)SHORT_FLOW_LIMIT,         "SHORT_FLOW_LIMIT")
        ((uint16_t)SHORT_LINKLOCAL_SRC_NAT,
         "SHORT_LINKLOCAL_SRC_NAT")
        ((uint16_t)SHORT_FAILED_VROUTER_INSTALL,
         "SHORT_FAILED_VROUTER_INST")
        ((uint16_t)DROP_POLICY,              "DROP_POLICY")
        ((uint16_t)DROP_OUT_POLICY,          "DROP_OUT_POLICY")
        ((uint16_t)DROP_SG,                  "DROP_SG")
        ((uint16_t)DROP_OUT_SG,              "DROP_OUT_SG")
        ((uint16_t)DROP_REVERSE_SG,          "DROP_REVERSE_SG")
        ((uint16_t)DROP_REVERSE_OUT_SG,      "DROP_REVERSE_OUT_SG");

tbb::atomic<int> FlowEntry::alloc_count_;
InetUnicastRouteEntry FlowEntry::inet4_route_key_(NULL, Ip4Address(), 32,
                                                  false);
InetUnicastRouteEntry FlowEntry::inet6_route_key_(NULL, Ip6Address(), 128,
                                                  false);
SecurityGroupList FlowEntry::default_sg_list_;

/////////////////////////////////////////////////////////////////////////////
// FlowData constructor/destructor
/////////////////////////////////////////////////////////////////////////////
FlowData::FlowData() {
    Reset();
}

FlowData::~FlowData() {
}

void FlowData::Reset() {
    smac = MacAddress();
    dmac = MacAddress();
    source_vn = "";
    dest_vn = "";
    source_sg_id_l.clear();
    dest_sg_id_l.clear();
    flow_source_vrf = VrfEntry::kInvalidIndex;
    flow_dest_vrf = VrfEntry::kInvalidIndex;
    match_p.Reset();
    vn_entry.reset(NULL);
    intf_entry.reset(NULL);
    in_vm_entry.reset(NULL);
    out_vm_entry.reset(NULL);
    nh.reset(NULL);
    vrf = VrfEntry::kInvalidIndex;
    mirror_vrf = VrfEntry::kInvalidIndex;
    dest_vrf = 0;
    component_nh_idx = (uint32_t)CompositeNH::kInvalidComponentNHIdx;
    source_plen = 0;
    dest_plen = 0;
    drop_reason = 0;
    vrf_assign_evaluated = false;
    pending_recompute = false;
    if_index_info = 0;
    tunnel_info.Reset();
    flow_source_plen_map.clear();
    flow_dest_plen_map.clear();
    enable_rpf = true;
    l2_rpf_plen = Address::kMaxV4PrefixLen;
    vm_cfg_name = "";
}

/////////////////////////////////////////////////////////////////////////////
// MatchPolicy constructor/destructor
/////////////////////////////////////////////////////////////////////////////
MatchPolicy::MatchPolicy() {
    Reset();
}

MatchPolicy::~MatchPolicy() {
}

void MatchPolicy::Reset() {
    m_acl_l.clear();
    policy_action = 0;
    m_out_acl_l.clear();
    out_policy_action = 0;
    m_out_sg_acl_l.clear();
    out_sg_rule_present = false;
    out_sg_action = 0;
    m_sg_acl_l.clear();
    sg_rule_present = false;
    sg_action = 0;
    m_reverse_sg_acl_l.clear();
    reverse_sg_rule_present = false;
    reverse_sg_action = 0;
    m_reverse_out_sg_acl_l.clear();
    reverse_out_sg_rule_present = false;
    reverse_out_sg_action = 0;
    m_mirror_acl_l.clear();
    mirror_action = 0;
    m_out_mirror_acl_l.clear();
    out_mirror_action = 0;
    m_vrf_assign_acl_l.clear();
    vrf_assign_acl_action = 0;
    sg_action_summary = 0;
    action_info.Clear();
}

/////////////////////////////////////////////////////////////////////////////
// FlowEntry constructor/destructor
/////////////////////////////////////////////////////////////////////////////
FlowEntry::FlowEntry(FlowTable *flow_table) :
    flow_table_(flow_table), flags_(0),
    linklocal_src_port_fd_(PktFlowInfo::kLinkLocalInvalidFd),
    tunnel_type_(TunnelType::INVALID),
    fip_vmi_(AgentKey::ADD_DEL_CHANGE, nil_uuid(), ""), fsc_(NULL) {
    Reset();
    alloc_count_.fetch_and_increment();
}

FlowEntry::~FlowEntry() {
    assert(refcount_ == 0);
    Reset();
    alloc_count_.fetch_and_decrement();
}

void FlowEntry::Reset() {
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
        flow_table_->DelLinkLocalFlowInfo(linklocal_src_port_fd_);
    }
    data_.Reset();
    l3_flow_ = true;
    flow_handle_ = kInvalidFlowHandle;
    deleted_ = false;
    flags_ = 0;
    short_flow_reason_ = SHORT_UNKNOWN;
    linklocal_src_port_ = 0;
    linklocal_src_port_fd_ = PktFlowInfo::kLinkLocalInvalidFd;
    peer_vrouter_ = "";
    tunnel_type_ = TunnelType::INVALID;
    on_tree_ = false;
    fip_ = 0;
    fip_vmi_ = VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, nil_uuid(), "");
    refcount_ = 0;
    nw_ace_uuid_ = FlowPolicyStateStr.at(NOT_EVALUATED);
    sg_rule_uuid_= FlowPolicyStateStr.at(NOT_EVALUATED);
    ksync_index_entry_ = std::auto_ptr<KSyncFlowIndexEntry>
        (new KSyncFlowIndexEntry());
}

void FlowEntry::Reset(const FlowKey &k) {
    Reset();
    key_ = k;
}

void FlowEntry::Init() {
    alloc_count_ = 0;
}

FlowEntry *FlowEntry::Allocate(const FlowKey &key, FlowTable *flow_table) {
    // flow_table will be NULL for some UT cases
    if (flow_table == NULL) {
        FlowEntry *flow = new FlowEntry(flow_table);
        flow->Reset(key);
        return flow;
    }

    return flow_table->free_list()->Allocate(key);
}

// selectively copy fields from RHS
void FlowEntry::Copy(const FlowEntry *rhs) {
    data_ = rhs->data_;
    flags_ = rhs->flags_;
    short_flow_reason_ = rhs->short_flow_reason_;
    sg_rule_uuid_ = rhs->sg_rule_uuid_;
    nw_ace_uuid_ = rhs->nw_ace_uuid_;
    peer_vrouter_ = rhs->peer_vrouter_;
    tunnel_type_ = rhs->tunnel_type_;
    fip_ = rhs->fip_;
    fip_vmi_ = rhs->fip_vmi_;
    flow_handle_ = rhs->flow_handle_;
}

/////////////////////////////////////////////////////////////////////////////
// Routines to initialize FlowEntry from PktControlInfo
/////////////////////////////////////////////////////////////////////////////
void intrusive_ptr_add_ref(FlowEntry *fe) {
    fe->refcount_.fetch_and_increment();
}

void intrusive_ptr_release(FlowEntry *fe) {
    FlowTable *flow_table = fe->flow_table();
    int prev = fe->refcount_.fetch_and_decrement();
    if (prev == 1) {
        if (fe->on_tree()) {
            FlowTable::FlowEntryMap::iterator it =
                flow_table->flow_entry_map_.find(fe->key());
            assert(it != flow_table->flow_entry_map_.end());
            flow_table->flow_entry_map_.erase(it);
        }
        flow_table->free_list()->Free(fe);
    }
}

bool FlowEntry::InitFlowCmn(const PktFlowInfo *info, const PktControlInfo *ctrl,
                            const PktControlInfo *rev_ctrl,
                            FlowEntry *rflow) {
    reverse_flow_entry_ = rflow;
    reset_flags(FlowEntry::ReverseFlow);
    peer_vrouter_ = info->peer_vrouter;
    tunnel_type_ = info->tunnel_type;

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
                            const PktControlInfo *rev_ctrl,
                            FlowEntry *rflow, Agent *agent) {
    flow_handle_ = pkt->GetAgentHdr().cmd_param;
    if (InitFlowCmn(info, ctrl, rev_ctrl, rflow) == false) {
        return;
    }
    if (info->linklocal_bind_local_port) {
        linklocal_src_port_ = info->nat_sport;
        linklocal_src_port_fd_ = info->linklocal_src_port_fd;
        flow_table_->AddLinkLocalFlowInfo(linklocal_src_port_fd_, flow_handle_,
                                          key_, UTCTimestampUsec());
        set_flags(FlowEntry::LinkLocalBindLocalSrcPort);
    } else {
        reset_flags(FlowEntry::LinkLocalBindLocalSrcPort);
    }
    uint32_t intf_in = pkt->GetAgentHdr().ifindex;
    data_.vm_cfg_name = InterfaceIdToVmCfgName(agent, intf_in);

    if (info->ingress) {
        set_flags(FlowEntry::IngressDir);
    } else {
        reset_flags(FlowEntry::IngressDir);
    }
    if (ctrl->rt_ != NULL) {
        SetRpfNH(flow_table_, ctrl->rt_);
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
                            const PktControlInfo *rev_ctrl,
                            FlowEntry *rflow, Agent *agent) {
    uint32_t intf_in;
    if (InitFlowCmn(info, ctrl, rev_ctrl, rflow) == false) {
        return;
    }
    set_flags(FlowEntry::ReverseFlow);
    if (ctrl->intf_) {
        intf_in = ctrl->intf_->id();
    } else {
        intf_in = Interface::kInvalidIndex;
    }
    data_.vm_cfg_name = InterfaceIdToVmCfgName(agent, intf_in);

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
        SetRpfNH(flow_table_, ctrl->rt_);
    }

    data_.flow_source_vrf = info->flow_dest_vrf;
    data_.flow_dest_vrf = info->flow_source_vrf;
    data_.flow_source_plen_map = info->flow_dest_plen_map;
    data_.flow_dest_plen_map = info->flow_source_plen_map;
    data_.vrf = info->dest_vrf;
    
    if (!info->nat_done) {
        data_.dest_vrf = info->flow_source_vrf;
    } else {
        data_.dest_vrf = info->nat_dest_vrf;
    }
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
    data_.source_sg_id_l = default_sg_list();
    data_.dest_sg_id_l = default_sg_list();
}

/////////////////////////////////////////////////////////////////////////////
// Utility routines
/////////////////////////////////////////////////////////////////////////////
// Find L2 Route for the MAC address.
AgentRoute *FlowEntry::GetL2Route(const VrfEntry *vrf,
                                  const MacAddress &mac) {
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (vrf->GetBridgeRouteTable());
    return table->FindRouteNoLock(mac);
}

AgentRoute *FlowEntry::GetUcRoute(const VrfEntry *entry,
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

uint32_t FlowEntry::reverse_flow_fip() const {
    FlowEntry *rflow = reverse_flow_entry_.get();
    if (rflow) {
        return rflow->fip();
    }
    return 0;
}

VmInterfaceKey FlowEntry::reverse_flow_vmi() const {
    FlowEntry *rflow = reverse_flow_entry_.get();
    if (rflow) {
        return rflow->fip_vmi();
    }
    return VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, nil_uuid(), "");
}

void FlowEntry::UpdateFipStatsInfo(uint32_t fip, uint32_t id, Agent *agent) {
    fip_ = fip;
    fip_vmi_ = InterfaceIdToKey(agent, id);
}

bool FlowEntry::set_pending_recompute(bool value) {
    if (data_.pending_recompute != value) {
        data_.pending_recompute = value;
        return true;
    }

    return false;
}

void FlowEntry::set_flow_handle(uint32_t flow_handle) {
    if (flow_handle_ != flow_handle) {
        assert(flow_handle_ == kInvalidFlowHandle);
        flow_handle_ = flow_handle;
    }
}

const std::string& FlowEntry::acl_assigned_vrf() const {
    return data_.match_p.action_info.vrf_translate_action_.vrf_name();
}

uint32_t FlowEntry::acl_assigned_vrf_index() const {
    VrfKey vrf_key(data_.match_p.action_info.vrf_translate_action_.vrf_name());
    const VrfEntry *vrf = static_cast<const VrfEntry *>(
            flow_table_->agent()->vrf_table()->FindActiveEntry(&vrf_key));
    if (vrf) {
        return vrf->vrf_id();
    }
    return 0;
}

static bool ShouldDrop(uint32_t action) {
    if (action & TrafficAction::DROP_FLAGS)
        return true;

    if (action & TrafficAction::IMPLICIT_DENY_FLAGS)
        return true;

    return false;
}

/////////////////////////////////////////////////////////////////////////////
// Flow entry fileds updation routines
/////////////////////////////////////////////////////////////////////////////

// Get src-vn/sg-id/plen from route
// src-vn and sg-id are used for policy lookup
// plen is used to track the routes to use by flow_mgmt module
void FlowEntry::GetSourceRouteInfo(const AgentRoute *rt) {
    const AgentPath *path = NULL;
    if (rt) {
        path = rt->GetActivePath();
    }
    if (path == NULL) {
        data_.source_vn = FlowHandler::UnknownVn();
        data_.source_sg_id_l = default_sg_list();
        data_.source_plen = 0;
    } else {
        data_.source_vn = path->dest_vn_name();
        data_.source_sg_id_l = path->sg_list();
        data_.source_plen = rt->plen();
    }
}

// Get dst-vn/sg-id/plen from route
// dst-vn and sg-id are used for policy lookup
// plen is used to track the routes to use by flow_mgmt module
void FlowEntry::GetDestRouteInfo(const AgentRoute *rt) {
    const AgentPath *path = NULL;
    if (rt) {
        path = rt->GetActivePath();
    }

    if (path == NULL) {
        data_.dest_vn = FlowHandler::UnknownVn();
        data_.dest_sg_id_l = default_sg_list();
        data_.dest_plen = 0;
    } else {
        data_.dest_vn = path->dest_vn_name();
        data_.dest_sg_id_l = path->sg_list();
        data_.dest_plen = rt->plen();
    }
}

void FlowEntry::UpdateRpf() {
    if (data_.vn_entry) {
        data_.enable_rpf = data_.vn_entry->enable_rpf();
    } else {
        data_.enable_rpf = true;
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
        InetUnicastRouteEntry *ip_rt = static_cast<InetUnicastRouteEntry *>
            (FlowEntry::GetUcRoute(rt->vrf(), key().src_addr));
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

VmInterfaceKey FlowEntry::InterfaceIdToKey(Agent *agent, uint32_t id) {
    if (id != Interface::kInvalidIndex) {
        Interface *itf = agent->interface_table()->FindInterface(id);
        if (itf && (itf->type() == Interface::VM_INTERFACE)) {
            return VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, itf->GetUuid(),
                                  itf->name());
        }
    }
    return VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, nil_uuid(), "");
}

uint32_t FlowEntry::InterfaceKeyToId(Agent *agent, const VmInterfaceKey &key) {
    const Interface *intf = dynamic_cast<const Interface*>
        (agent->interface_table()->FindActiveEntry(&key));
    if (intf) {
        return intf->id();
    }
    return Interface::kInvalidIndex;
}

const std::string FlowEntry::InterfaceIdToVmCfgName(Agent *agent, uint32_t id) {
    if (id != Interface::kInvalidIndex) {
        const VmInterface *itf = dynamic_cast <const VmInterface *>
            (agent->interface_table()->FindInterface(id));
        if (itf) {
            const VmEntry *vm = itf->vm();
            if (vm) {
                return vm->GetCfgName();
            }
        }
    }
    return "";
}
/////////////////////////////////////////////////////////////////////////////
// Routines to compute ACL to be applied (including network-policy, SG and
// VRF-Assign Rules
/////////////////////////////////////////////////////////////////////////////
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

// Rebuild all the policy rules to be applied
void FlowEntry::GetPolicyInfo(const VnEntry *vn, const FlowEntry *rflow) {
    // Reset old values first
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
    GetPolicy(vn, rflow);

    // Get Sg list
    GetSgList(data_.intf_entry.get());

    //Get VRF translate ACL
    GetVrfAssignAcl();
}

void FlowEntry::GetPolicyInfo() {
    GetPolicyInfo(data_.vn_entry.get(), reverse_flow_entry());
}

void FlowEntry::GetPolicyInfo(const VnEntry *vn) {
    GetPolicyInfo(vn, reverse_flow_entry());
}

void FlowEntry::GetPolicyInfo(const FlowEntry *rflow) {
    GetPolicyInfo(data_.vn_entry.get(), rflow);
}

void FlowEntry::GetPolicy(const VnEntry *vn, const FlowEntry *rflow) {
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

/////////////////////////////////////////////////////////////////////////////
// Flow policy processing routines
/////////////////////////////////////////////////////////////////////////////
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
        bool ignore_acl =
            acl_it->action_info.vrf_translate_action_.ignore_acl();
        data_.match_p.action_info.vrf_translate_action_.set_ignore_acl
            (ignore_acl);
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
        action = (1 << TrafficAction::DENY) |
            (1 << TrafficAction::IMPLICIT_DENY);
        if (info) {
            info->uuid = FlowPolicyStateStr.at(IMPLICIT_DENY);
            info->drop = true;
        }
    }

    return action;
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
                    MatchAcl(out_hdr, data_.match_p.m_reverse_out_sg_acl_l,
                             true, !data_.match_p.reverse_out_sg_rule_present,
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
        //                      (reverse_sg_action & reverse_out_sg_action))
        //                      ALLOW
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
                data_.match_p.sg_action_summary = (1 << TrafficAction::DENY);
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

/////////////////////////////////////////////////////////////////////////////
// Flow policy action compute routines
/////////////////////////////////////////////////////////////////////////////
void FlowEntry::ResyncFlow() {
    UpdateRpf();
    DoPolicy();

    // If this is forward flow, update the SG action for reflexive entry
    FlowEntry *rflow = (is_flags_set(FlowEntry::ReverseFlow) == false) ?
        reverse_flow_entry() : NULL;
    if (rflow) {
        // Update action for reverse flow
        rflow->UpdateReflexiveAction();
        rflow->ActionRecompute();
    }
}

// Recompute FlowEntry action based on ACLs already set in the flow
bool FlowEntry::ActionRecompute() {
    uint32_t action = 0;
    uint16_t drop_reason = DROP_UNKNOWN;
    bool ret = false;

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
        //network ACL. Match condition on the interface would have ignore acl
        //flag set to avoid applying two ACL for vrf translation
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
        action |= (1 << TrafficAction::DENY);
    }

    // check for conflicting actions and remove allowed action
    if (ShouldDrop(action)) {
        action = (action & ~TrafficAction::DROP_FLAGS &
                  ~TrafficAction::PASS_FLAGS);
        action |= (1 << TrafficAction::DENY);
        if (is_flags_set(FlowEntry::ShortFlow)) {
            drop_reason = short_flow_reason_;
        } else if (ShouldDrop(data_.match_p.policy_action)) {
            drop_reason = DROP_POLICY;
        } else if (ShouldDrop(data_.match_p.out_policy_action)){
            drop_reason = DROP_OUT_POLICY;
        } else if (ShouldDrop(data_.match_p.sg_action)){
            drop_reason = DROP_SG;
        } else if (ShouldDrop(data_.match_p.out_sg_action)){
            drop_reason = DROP_OUT_SG;
        } else if (ShouldDrop(data_.match_p.reverse_sg_action)){
            drop_reason = DROP_REVERSE_SG;
        } else if (ShouldDrop(data_.match_p.reverse_out_sg_action)){
            drop_reason = DROP_REVERSE_OUT_SG;
        } else {
            drop_reason = DROP_UNKNOWN;
        }
    }

    if (action & (1 << TrafficAction::TRAP)) {
        action = (1 << TrafficAction::TRAP);
    }

    if (action != data_.match_p.action_info.action) {
        data_.match_p.action_info.action = action;
        ret = true;
    }
    if (drop_reason != data_.drop_reason) {
        data_.drop_reason = drop_reason;
        ret = true;
    }
    return ret;
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

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
void SetActionStr(const FlowAction &action_info,
                  std::vector<ActionStr> &action_str_l) {
    std::bitset<32> bs(action_info.action);
    for (unsigned int i = 0; i <= bs.size(); i++) {
        if (bs[i]) {
            ActionStr astr;
            astr.action =
                TrafficAction::ActionToString((TrafficAction::Action)i);
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

static void SetAclListAclAction(const std::list<MatchAclParams> &acl_l,
                                std::vector<AclAction> &acl_action_l,
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

    const std::list<MatchAclParams> &out_sg_acl_l =
        data_.match_p.m_out_sg_acl_l;
    acl_type = "o sg";
    SetAclListAclAction(out_sg_acl_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &out_m_acl_l =
        data_.match_p.m_out_mirror_acl_l;
    acl_type = "o dynamic";
    SetAclListAclAction(out_m_acl_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &r_sg_l = data_.match_p.m_reverse_sg_acl_l;
    acl_type = "r sg";
    SetAclListAclAction(r_sg_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &r_out_sg_l =
        data_.match_p.m_reverse_out_sg_acl_l;
    acl_type = "r o sg";
    SetAclListAclAction(r_out_sg_l, acl_action_l, acl_type);

    const std::list<MatchAclParams> &vrf_assign_acl_l =
        data_.match_p.m_vrf_assign_acl_l;
    acl_type = "vrf assign";
    SetAclListAclAction(vrf_assign_acl_l, acl_action_l, acl_type);
}

void FlowEntry::FillFlowInfo(FlowInfo &info) {
    info.set_flow_index(flow_handle_);
    if (key_.family == Address::INET) {
        info.set_source_ip(key_.src_addr.to_v4().to_ulong());
        info.set_destination_ip(key_.dst_addr.to_v4().to_ulong());
    } else {
        uint64_t sip[2], dip[2];
        Ip6AddressToU64Array(key_.src_addr.to_v6(), sip, 2);
        Ip6AddressToU64Array(key_.dst_addr.to_v6(), dip, 2);
        info.set_sip_upper(sip[0]);
        info.set_sip_lower(sip[1]);
        info.set_dip_upper(dip[0]);
        info.set_dip_lower(dip[1]);
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
    for (it = data_.dest_sg_id_l.begin(); it != data_.dest_sg_id_l.end();
         it++) {
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
                    info.set_nat_destination_ip
                        (nat_flow->key().src_addr.to_v4().to_ulong());
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

static void SetAclListAceId(const AclDBEntry *acl,
                            const MatchAclParamsList &acl_l,
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

void FlowEntry::SetAclFlowSandeshData(const AclDBEntry *acl,
        FlowSandeshData &fe_sandesh_data, Agent *agent) const {
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

    fe_sandesh_data.set_flow_handle(integerToString(flow_handle_));
    fe_sandesh_data.set_source_vn(data_.source_vn);
    fe_sandesh_data.set_source_vn(data_.source_vn);
    fe_sandesh_data.set_dest_vn(data_.dest_vn);
    std::vector<uint32_t> v;
    if (!fsc_) {
        return;
    }
    const FlowExportInfo *info = fsc_->FindFlowExportInfo(key_);
    if (!info) {
        return;
    }
    SecurityGroupList::const_iterator it;
    for (it = data_.source_sg_id_l.begin(); 
            it != data_.source_sg_id_l.end(); it++) {
        v.push_back(*it);
    }
    fe_sandesh_data.set_source_sg_id_l(v);
    v.clear();
    for (it = data_.dest_sg_id_l.begin(); it != data_.dest_sg_id_l.end();
         it++) {
        v.push_back(*it);
    }
    fe_sandesh_data.set_dest_sg_id_l(v);
    if (info) {
        fe_sandesh_data.set_flow_uuid(UuidToString(info->flow_uuid()));
        fe_sandesh_data.set_bytes(integerToString(info->bytes()));
        fe_sandesh_data.set_packets(integerToString(info->packets()));
        fe_sandesh_data.set_setup_time(
            integerToString(UTCUsecToPTime(info->setup_time())));
        fe_sandesh_data.set_setup_time_utc(info->setup_time());
        if (info->teardown_time()) {
            fe_sandesh_data.set_teardown_time(
                integerToString(UTCUsecToPTime(info->teardown_time())));
        } else {
            fe_sandesh_data.set_teardown_time("");
        }
    }
    fe_sandesh_data.set_current_time(integerToString(
                UTCUsecToPTime(UTCTimestampUsec())));

    SetAclListAceId(acl, data_.match_p.m_acl_l, fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_sg_acl_l, fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_mirror_acl_l, fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_out_acl_l, fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_reverse_sg_acl_l,
                    fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_reverse_out_sg_acl_l,
                    fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_out_sg_acl_l, fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_out_mirror_acl_l,
                    fe_sandesh_data.ace_l);
    SetAclListAceId(acl, data_.match_p.m_vrf_assign_acl_l,
                    fe_sandesh_data.ace_l);

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
