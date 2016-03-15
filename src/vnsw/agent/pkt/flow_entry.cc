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

const std::map<FlowEntry::FlowPolicyState, const char*>
    FlowEntry::FlowPolicyStateStr = boost::assign::map_list_of
        (NOT_EVALUATED,            "00000000-0000-0000-0000-000000000000")
        (IMPLICIT_ALLOW,           "00000000-0000-0000-0000-000000000001")
        (IMPLICIT_DENY,            "00000000-0000-0000-0000-000000000002")
        (DEFAULT_GW_ICMP_OR_DNS,   "00000000-0000-0000-0000-000000000003")
        (LINKLOCAL_FLOW,           "00000000-0000-0000-0000-000000000004")
        (MULTICAST_FLOW,           "00000000-0000-0000-0000-000000000005")
        (NON_IP_FLOW,              "00000000-0000-0000-0000-000000000006")
        (BGPROUTERSERVICE_FLOW,    "00000000-0000-0000-0000-000000000007");

const std::map<uint16_t, const char*>
    FlowEntry::FlowDropReasonStr = boost::assign::map_list_of
        ((uint16_t)DROP_UNKNOWN,                 "UNKNOWN")
        ((uint16_t)SHORT_UNAVIALABLE_INTERFACE,
         "Short flow Interface unavialable")
        ((uint16_t)SHORT_IPV4_FWD_DIS,       "Short flow Ipv4 forwarding disabled")
        ((uint16_t)SHORT_UNAVIALABLE_VRF,
         "Short flow VRF unavailable")
        ((uint16_t)SHORT_NO_SRC_ROUTE,       "Short flow No Source route")
        ((uint16_t)SHORT_NO_DST_ROUTE,       "Short flow No Destination route")
        ((uint16_t)SHORT_AUDIT_ENTRY,        "Short flow Audit Entry")
        ((uint16_t)SHORT_VRF_CHANGE,         "Short flow VRF CHANGE")
        ((uint16_t)SHORT_NO_REVERSE_FLOW,    "Short flow No Reverse flow")
        ((uint16_t)SHORT_REVERSE_FLOW_CHANGE,
         "Short flow Reverse flow change")
        ((uint16_t)SHORT_NAT_CHANGE,         "Short flow NAT Changed")
        ((uint16_t)SHORT_FLOW_LIMIT,         "Short flow Flow Limit Reached")
        ((uint16_t)SHORT_LINKLOCAL_SRC_NAT,
         "Short flow Linklocal source NAT failed")
        ((uint16_t)SHORT_FAILED_VROUTER_INSTALL,
         "Short flow vrouter install failed")
        ((uint16_t)SHORT_INVALID_L2_FLOW,    "Short flow invalid L2 flow")
        ((uint16_t)DROP_POLICY,              "Flow drop Policy")
        ((uint16_t)DROP_OUT_POLICY,          "Flow drop Out Policy")
        ((uint16_t)DROP_SG,                  "Flow drop SG")
        ((uint16_t)DROP_OUT_SG,              "Flow drop OUT SG")
        ((uint16_t)DROP_REVERSE_SG,          "Flow drop REVERSE SG")
        ((uint16_t)DROP_REVERSE_OUT_SG,      "Flow drop REVERSE OUT SG");

tbb::atomic<int> FlowEntry::alloc_count_;
SecurityGroupList FlowEntry::default_sg_list_;

/////////////////////////////////////////////////////////////////////////////
// VmFlowRef
/////////////////////////////////////////////////////////////////////////////
const int VmFlowRef::kInvalidFd;
VmFlowRef::VmFlowRef() :
    vm_(NULL), fd_(kInvalidFd), port_(0), flow_(NULL) {
}

VmFlowRef::VmFlowRef(const VmFlowRef &rhs) {
    // UPDATE on linklocal flows is not supported. So, fd_ should be invalid
    assert(fd_ == VmFlowRef::kInvalidFd);
    assert(rhs.fd_ == VmFlowRef::kInvalidFd);
    SetVm(rhs.vm_.get());
}

VmFlowRef:: ~VmFlowRef() {
    Reset(true);
}

void VmFlowRef::Init(FlowEntry *flow) {
    flow_ = flow;
}

void VmFlowRef::operator=(const VmFlowRef &rhs) {
    assert(rhs.fd_ == VmFlowRef::kInvalidFd);
    assert(rhs.port_ == 0);
    // For linklocal flows, we should have called Move already. It would
    // reset vm_. Validate it
    if (fd_ != VmFlowRef::kInvalidFd)
        assert(rhs.vm_.get() == NULL);
}

// Move is called from Copy() routine when flow is evicted by vrouter and a
// new flow-add is received by agent. Use the fd_ and port_ from new flow
// since reverse flow will be setup based on these
void VmFlowRef::Move(VmFlowRef *rhs) {
    // Release the old values
    Reset(false);

    fd_ = rhs->fd_;
    port_ = rhs->port_;
    SetVm(rhs->vm_.get());

    // Ownership for fd_ is transferred. Reset RHS fields
    // Reset VM first before resetting fd_
    rhs->SetVm(NULL);
    rhs->fd_ = VmFlowRef::kInvalidFd;
    rhs->port_ = 0;
}

void VmFlowRef::Reset(bool reset_flow) {
    FreeRef();
    FreeFd();
    vm_.reset(NULL);
    if (reset_flow)
        flow_ = NULL;
}

void VmFlowRef::FreeRef() {
    if (vm_.get() == NULL)
        return;

    vm_->update_flow_count(-1);
    if (fd_ != kInvalidFd) {
        vm_->update_linklocal_flow_count(-1);
    }
}

void VmFlowRef::FreeFd() {
    if (fd_ == kInvalidFd) {
        assert(port_ == 0);
        return;
    }

    FlowProto *proto = flow_->flow_table()->agent()->pkt()->get_flow_proto();
    proto->update_linklocal_flow_count(-1);
    flow_->flow_table()->DelLinkLocalFlowInfo(fd_);
    close(fd_);
    fd_ = kInvalidFd;
    port_ = 0;
}

void VmFlowRef::SetVm(const VmEntry *vm) {
    if (vm == vm_.get())
        return;
    FreeRef();

    vm_.reset(vm);
    if (vm == NULL)
        return;

    // update per-vm flow accounting
    vm->update_flow_count(1);
    if (fd_ != kInvalidFd) {
        vm_->update_linklocal_flow_count(1);
    }

    return;
}

bool VmFlowRef::AllocateFd(Agent *agent, uint8_t l3_proto) {
    if (fd_ != kInvalidFd)
        return true;

    port_ = 0;
    // Short flows are always dropped. Dont allocate FD for short flow
    if (flow_->IsShortFlow())
        return false;

    if (l3_proto == IPPROTO_TCP) {
        fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    } else if (l3_proto == IPPROTO_UDP) {
        fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }

    if (fd_ == kInvalidFd) {
        return false;
    }

    // Update agent accounting info
    agent->pkt()->get_flow_proto()->update_linklocal_flow_count(1);
    flow_->flow_table()->AddLinkLocalFlowInfo(fd_, flow_->flow_handle(),
                                              flow_->key(), UTCTimestampUsec());

    // allow the socket to be reused upon close
    int optval = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    if (bind(fd_, (struct sockaddr*) &address, sizeof(address)) < 0) {
        FreeFd();
        return false;
    }

    struct sockaddr_in bound_to;
    socklen_t len = sizeof(bound_to);
    if (getsockname(fd_, (struct sockaddr*) &bound_to, &len) < 0) {
        FreeFd();
        return false;
    }

    port_ = ntohs(bound_to.sin_port);
    return true;
}

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
    source_vn_list.clear();
    source_vn_match = "";
    dest_vn_match = "";
    dest_vn_list.clear();
    source_sg_id_l.clear();
    dest_sg_id_l.clear();
    flow_source_vrf = VrfEntry::kInvalidIndex;
    flow_dest_vrf = VrfEntry::kInvalidIndex;
    match_p.Reset();
    vn_entry.reset(NULL);
    intf_entry.reset(NULL);
    in_vm_entry.Reset(true);
    out_vm_entry.Reset(true);
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
    bgp_as_a_service_port = 0;
    ecmp_rpf_nh_ = 0;
    acl_assigned_vrf_index_ = VrfEntry::kInvalidIndex;
}

static std::vector<std::string> MakeList(const VnListType &ilist) {
    std::vector<std::string> olist;
    for (VnListType::const_iterator it = ilist.begin();
         it != ilist.end(); ++it) {
        olist.push_back(*it);
    }
    return olist;
}

std::vector<std::string> FlowData::SourceVnList() const {
    return MakeList(source_vn_list);
}

std::vector<std::string> FlowData::DestinationVnList() const {
    return MakeList(dest_vn_list);
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
    tunnel_type_(TunnelType::INVALID),
    fip_vmi_(AgentKey::ADD_DEL_CHANGE, nil_uuid(), "") {
    Reset();
    alloc_count_.fetch_and_increment();
}

FlowEntry::~FlowEntry() {
    assert(refcount_ == 0);
    Reset();
    alloc_count_.fetch_and_decrement();
}

void FlowEntry::Reset() {
    uuid_ = flow_table_->rand_gen();
    data_.Reset();
    l3_flow_ = true;
    flow_handle_ = kInvalidFlowHandle;
    reverse_flow_entry_ = NULL;
    deleted_ = false;
    flags_ = 0;
    short_flow_reason_ = SHORT_UNKNOWN;
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
    fsc_ = NULL;
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
    FlowEntry *flow;
    if (flow_table == NULL) {
        flow = new FlowEntry(flow_table);
        flow->Reset(key);
    } else {
        flow = flow_table->free_list()->Allocate(key);
    }

    flow->data_.in_vm_entry.Init(flow);
    flow->data_.out_vm_entry.Init(flow);
    return flow;
}

// selectively copy fields from RHS
void FlowEntry::Copy(FlowEntry *rhs, bool update) {
    if (update) {
        rhs->data_.in_vm_entry.FreeFd();
        rhs->data_.out_vm_entry.FreeFd();
    } else {
        // The operator= below will call VmFlowRef operator=. In case of flow
        // eviction, we want to move ownership from rhs to lhs. However rhs is
        // const ref in operator so, invode Move API to transfer ownership
        data_.in_vm_entry.Move(&rhs->data_.in_vm_entry);
        data_.out_vm_entry.Move(&rhs->data_.out_vm_entry);
    }
    data_ = rhs->data_;
    flags_ = rhs->flags_;
    short_flow_reason_ = rhs->short_flow_reason_;
    sg_rule_uuid_ = rhs->sg_rule_uuid_;
    nw_ace_uuid_ = rhs->nw_ace_uuid_;
    peer_vrouter_ = rhs->peer_vrouter_;
    tunnel_type_ = rhs->tunnel_type_;
    fip_ = rhs->fip_;
    fip_vmi_ = rhs->fip_vmi_;
    if (update == false) {
        flow_handle_ = rhs->flow_handle_;
        /* Flow Entry is being re-used. Generate a new UUID for it. */
        // results is delete miss for previous uuid to stats collector
        // with eviction disabled following is not required
        // uuid_ = flow_table_->rand_gen();
    }
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
            if (flow_table->ConcurrencyCheck() == false) {
                FlowEntryPtr ref(fe);
                FlowProto *proto=flow_table->agent()->pkt()->get_flow_proto();
                proto->ForceEnqueueFreeFlowReference(ref);
                return;
            }
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
    if (info->bgp_router_service_flow) {
        set_flags(FlowEntry::BgpRouterService);
        data_.bgp_as_a_service_port = info->nat_sport;
    } else {
        reset_flags(FlowEntry::BgpRouterService);
        data_.bgp_as_a_service_port = 0;
    }

    data_.intf_entry = ctrl->intf_ ? ctrl->intf_ : rev_ctrl->intf_;
    data_.vn_entry = ctrl->vn_ ? ctrl->vn_ : rev_ctrl->vn_;
    data_.in_vm_entry.SetVm(ctrl->vm_);
    data_.out_vm_entry.SetVm(rev_ctrl->vm_);
    l3_flow_ = info->l3_flow;
    data_.ecmp_rpf_nh_ = 0;
    data_.acl_assigned_vrf_index_ = VrfEntry::kInvalidIndex;
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
        data_.dest_vn_list = data_.source_vn_list;
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
        data_.dest_vn_list = data_.source_vn_list;
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
    data_.source_vn_list = FlowHandler::UnknownVnList();
    data_.dest_vn_list = FlowHandler::UnknownVnList();
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
        InetUnicastRouteEntry key(NULL, addr, 32, false);
        rt = entry->GetUcRoute(key);
    } else {
        InetUnicastRouteEntry key(NULL, addr, 128, false);
        rt = entry->GetUcRoute(key);
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

void FlowEntry::set_acl_assigned_vrf_index() {
    VrfKey vrf_key(data_.match_p.action_info.vrf_translate_action_.vrf_name());
    const VrfEntry *vrf = static_cast<const VrfEntry *>(
            flow_table()->agent()->vrf_table()->FindActiveEntry(&vrf_key));
    if (vrf) {
        data_.acl_assigned_vrf_index_ = vrf->vrf_id();
        return;
    }
    data_.acl_assigned_vrf_index_ = VrfEntry::kInvalidIndex;
}

uint32_t FlowEntry::acl_assigned_vrf_index() const {
    return data_.acl_assigned_vrf_index_;
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
std::string FlowEntry::DropReasonStr(uint16_t reason) {
    std::map<uint16_t, const char*>::const_iterator it =
        FlowDropReasonStr.find(reason);
    if (it != FlowDropReasonStr.end()) {
        return string(it->second);
    }
    return "UNKNOWN";
}

// Get src-vn/sg-id/plen from route
// src-vn and sg-id are used for policy lookup
// plen is used to track the routes to use by flow_mgmt module
void FlowEntry::GetSourceRouteInfo(const AgentRoute *rt) {
    const AgentPath *path = NULL;
    if (rt) {
        path = rt->GetActivePath();
    }
    if (path == NULL) {
        data_.source_vn_list = FlowHandler::UnknownVnList();
        data_.source_vn_match = FlowHandler::UnknownVn();
        data_.source_sg_id_l = default_sg_list();
        data_.source_plen = 0;
    } else {
        data_.source_vn_list = path->dest_vn_list();
        if (path->dest_vn_list().size())
            data_.source_vn_match = *path->dest_vn_list().begin();
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
        data_.dest_vn_list = FlowHandler::UnknownVnList();
        data_.dest_vn_match = FlowHandler::UnknownVn();
        data_.dest_sg_id_l = default_sg_list();
        data_.dest_plen = 0;
    } else {
        data_.dest_vn_list = path->dest_vn_list();
        if (path->dest_vn_list().size())
            data_.dest_vn_match = *path->dest_vn_list().begin();
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

//Given a NH take reference on the NH and set the RPF
bool FlowEntry::SetRpfNHState(FlowTable *ft, const NextHop *nh) {

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
                FlowEntry::GetUcRoute(rt->vrf(), key().src_addr));
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

    return SetRpfNHState(ft, nh);
}

bool FlowEntry::SetEcmpRpfNH(FlowTable *ft, uint32_t nh_id) {
    if (!nh_id) {
        return SetRpfNHState(ft, NULL);
    }

    const NextHop *nh = ft->agent()->nexthop_table()->FindNextHop(nh_id);
    return SetRpfNHState(ft, nh);
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

    // Dont apply network-policy for linklocal, bgp router service
    // and subnet broadcast flow
    if (is_flags_set(FlowEntry::LinkLocalFlow) ||
        is_flags_set(FlowEntry::Multicast) ||
        is_flags_set(FlowEntry::BgpRouterService)) {
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
        is_flags_set(FlowEntry::Multicast) ||
        is_flags_set(FlowEntry::BgpRouterService)) {
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
        is_flags_set(FlowEntry::Multicast) ||
        is_flags_set(FlowEntry::BgpRouterService)) {
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
        bool ignore_acl =
            acl_it->action_info.vrf_translate_action_.ignore_acl();
        data_.match_p.action_info.vrf_translate_action_.set_ignore_acl
            (ignore_acl);
    }
    if (data_.vrf_assign_evaluated && vrf_assigned_name !=
        data_.match_p.action_info.vrf_translate_action_.vrf_name()) {
        MakeShortFlow(SHORT_VRF_CHANGE);
    }

    set_acl_assigned_vrf_index();
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
            } else if (is_flags_set(FlowEntry::BgpRouterService)) {
                info->uuid = FlowPolicyStateStr.at(BGPROUTERSERVICE_FLOW);
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
    hdr->src_policy_id = &(data_.source_vn_list);
    hdr->dst_policy_id = &(data_.dest_vn_list);
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
    hdr->src_policy_id = &(rflow->data().dest_vn_list);
    hdr->dst_policy_id = &(rflow->data().source_vn_list);
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
    if (!nw_acl_info.src_match_vn.empty())
        data_.source_vn_match = nw_acl_info.src_match_vn;
    if (!nw_acl_info.dst_match_vn.empty())
        data_.dest_vn_match = nw_acl_info.dst_match_vn;
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
    UpdateEcmpInfo();

    // If this is forward flow, update the SG action for reflexive entry
    FlowEntry *rflow = (is_flags_set(FlowEntry::ReverseFlow) == false) ?
        reverse_flow_entry() : NULL;
    if (rflow) {
        // Update action for reverse flow
        rflow->UpdateReflexiveAction();
        rflow->ActionRecompute();
    }
}

const VrfEntry*
FlowEntry::GetDestinationVrf() {
    const VrfEntry *vrf = NULL;
    VrfTable *vrf_table = flow_table()->agent()->vrf_table();

    if (match_p().action_info.action &
            (1 << TrafficAction::VRF_TRANSLATE)) {
        vrf = vrf_table->FindVrfFromId(acl_assigned_vrf_index());
    } else if (is_flags_set(FlowEntry::NatFlow)) {
        vrf = vrf_table->FindVrfFromId(data().dest_vrf);
    } else {
        vrf = vrf_table->FindVrfFromId(data().vrf);
    }
    return vrf;
}

void FlowEntry::SetComponentIndex(const NextHopKey *nh_key,
                                  uint32_t label, bool mpls_path_select) {

    const VrfEntry *vrf = GetDestinationVrf();
    if (vrf == NULL) {
        FlowInfo flow_info;
        FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Invalid reverse while setting ECMP index", flow_info);
        return;
    }

    FlowEntry *rflow = reverse_flow_entry();
    const IpAddress dip = rflow->key().src_addr;
    InetUnicastRouteEntry *rt =
        static_cast<InetUnicastRouteEntry *>(FlowEntry::GetUcRoute(vrf, dip));
    if (!rt || rt->GetActiveNextHop()->GetType() != NextHop::COMPOSITE) {
        rflow->set_ecmp_rpf_nh(0);
        return;
    }

    const NextHop *nh = rt->GetActiveNextHop();
    //Set composite NH based on local mpls label flow
    if (mpls_path_select) {
        nh = rt->GetLocalNextHop();
    }

    if (!nh) {
        rflow->set_ecmp_rpf_nh(0);
        return;
    }

    if (nh->GetType() != NextHop::COMPOSITE) {
        rflow->set_ecmp_rpf_nh(nh->id());
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
        flow_table()->agent()->nexthop_table()->FindActiveEntry(nh_key));
    ComponentNH component_nh(label, component_nh_ptr);

    uint32_t idx = 0;
    if (comp_nh->GetIndex(component_nh, idx)) {
        if (data_.component_nh_idx != idx) {
            data_.component_nh_idx = idx;
        }
        //Update the reverse flow source RPF check based on this
        //composite NH
        rflow->set_ecmp_rpf_nh(nh->id());
    } else {
        FlowInfo flow_info;
        FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Invalid reverse while setting ECMP index", flow_info);
    }
}

void FlowEntry::SetLocalFlowEcmpIndex() {
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
    FlowEntry *rflow = reverse_flow_entry();

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
    const NextHop *nh = flow_table()->agent()->nexthop_table()->
                            FindNextHop(rflow->key().nh);
    if (nh == NULL) {
        return;
    }
    DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());

    //All component nexthop are NULL
    nh_key->SetPolicy(false);

    if (nh->GetType() == NextHop::VLAN) {
        const VlanNH *vlan_nh = static_cast<const VlanNH *>(nh);
        label = vm_port->GetServiceVlanLabel(vlan_nh->GetVrf());
    }

    bool mpls_path = false;
    if (!is_flags_set(FlowEntry::LocalFlow)) {
        mpls_path = true;
    }

    SetComponentIndex(nh_key, label, mpls_path);
}

void FlowEntry::SetRemoteFlowEcmpIndex() {
    uint32_t label;

    //Get tunnel info from reverse flow
    label = 0;
    boost::system::error_code ec;
    Ip4Address dest_ip = Ip4Address::from_string(peer_vrouter_, ec);
    if (ec.value() != 0) {
        return;
    }

    boost::scoped_ptr<NextHopKey> nh_key(
            new TunnelNHKey(flow_table()->agent()->fabric_vrf_name(),
                            flow_table()->agent()->router_id(),
                            dest_ip, false, tunnel_type()));
    SetComponentIndex(nh_key.get(), label, false);
}

void FlowEntry::UpdateEcmpInfo() {
    FlowEntry *rflow = reverse_flow_entry();

    if (is_flags_set(FlowEntry::EcmpFlow) == false ||
        is_flags_set(FlowEntry::ShortFlow) ||
        l3_flow() == false) {
        return;
    }

    if (rflow == NULL) {
        FlowInfo flow_info;
        FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Invalid reverse flow for setting ECMP index", flow_info);
        return;
    }

    bool local_flow = false;
    if (is_flags_set(FlowEntry::LocalFlow) ||
            !is_flags_set(FlowEntry::IngressDir)) {
        local_flow = true;
    }

    if (local_flow) {
        SetLocalFlowEcmpIndex();
    } else {
        SetRemoteFlowEcmpIndex();
    }
}

void FlowEntry::set_ecmp_rpf_nh(uint32_t id) {
    if (data_.ecmp_rpf_nh_ == id) {
        return;
    }

    data_.ecmp_rpf_nh_ = id;
    bool update_ksync = false;

    if (!id) {
        const VrfEntry *vrf = flow_table()->agent()->vrf_table()->
            FindVrfFromId(data().flow_source_vrf);
        if (vrf) {
            //Flow transitioned from ECMP to non ecmp
            InetUnicastRouteEntry *ip_rt = static_cast<InetUnicastRouteEntry *>(
                    FlowEntry::GetUcRoute(vrf, key().src_addr));
            update_ksync = SetRpfNH(flow_table(), ip_rt);
        }
    } else {
        update_ksync = SetEcmpRpfNH(flow_table(), id);
    }

    if (ksync_index_entry()->ksync_entry() && update_ksync) {
        flow_table()->UpdateKSync(this, true);
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
    info.set_source_vn_list(data_.SourceVnList());
    info.set_dest_vn_list(data_.DestinationVnList());
    info.set_source_vn_match(data_.source_vn_match);
    info.set_dest_vn_match(data_.dest_vn_match);
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
    info.set_drop_reason(FlowEntry::DropReasonStr(data_.drop_reason));
    if (flow_table_) {
        info.set_table_id(flow_table_->table_index());
    }
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
    fe_sandesh_data.set_source_vn(data_.source_vn_match);
    fe_sandesh_data.set_dest_vn(data_.dest_vn_match);
    fe_sandesh_data.set_source_vn_list(data_.SourceVnList());
    fe_sandesh_data.set_dest_vn_list(data_.DestinationVnList());
    std::vector<uint32_t> v;
    if (!fsc_) {
        return;
    }
    const FlowExportInfo *info = fsc_->FindFlowExportInfo(uuid_);
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

string FlowEntry::KeyString() const {
    std::ostringstream str;
    int idx = flow_handle_ == FlowEntry::kInvalidFlowHandle ? -1 : flow_handle_;
    str << " Idx : " << idx
        << " Key : "
        << key_.nh << " "
        << key_.src_addr.to_string() << ":"
        << key_.src_port << " "
        << key_.dst_addr.to_string() << ":"
        << key_.dst_port << " "
        << (uint16_t)key_.protocol;
    return str.str();
}
