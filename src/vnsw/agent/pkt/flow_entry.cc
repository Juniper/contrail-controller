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
#include <oper/qos_config.h>

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
        ((uint16_t)SHORT_FLOW_ON_TSN,        "Short flow TSN flow")
        ((uint16_t)SHORT_NO_MIRROR_ENTRY,     "Short flow No mirror entry ")
        ((uint16_t)SHORT_SAME_FLOW_RFLOW_KEY,"Short flow same flow and rflow")
        ((uint16_t)DROP_POLICY,              "Flow drop Policy")
        ((uint16_t)DROP_OUT_POLICY,          "Flow drop Out Policy")
        ((uint16_t)DROP_SG,                  "Flow drop SG")
        ((uint16_t)DROP_OUT_SG,              "Flow drop OUT SG")
        ((uint16_t)DROP_REVERSE_SG,          "Flow drop REVERSE SG")
        ((uint16_t)DROP_REVERSE_OUT_SG,      "Flow drop REVERSE OUT SG")
        ((uint16_t)SHORT_NO_SRC_ROUTE_L2RPF,
         "Short flow No Source route for RPF NH");

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
    src_ip_nh.reset(NULL);
    vrf = VrfEntry::kInvalidIndex;
    mirror_vrf = VrfEntry::kInvalidIndex;
    dest_vrf = 0;
    component_nh_idx = (uint32_t)CompositeNH::kInvalidComponentNHIdx;
    source_plen = 0;
    dest_plen = 0;
    drop_reason = 0;
    vrf_assign_evaluated = false;
    if_index_info = 0;
    tunnel_info.Reset();
    flow_source_plen_map.clear();
    flow_dest_plen_map.clear();
    enable_rpf = true;
    rpf_nh.reset(NULL);
    rpf_plen = Address::kMaxV4PrefixLen;
    rpf_vrf = VrfEntry::kInvalidIndex;
    vm_cfg_name = "";
    bgp_as_a_service_port = 0;
    acl_assigned_vrf_index_ = VrfEntry::kInvalidIndex;
    qos_config_idx = AgentQosConfigTable::kInvalidIndex;
    ttl = 0;
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
// FlowEventLog constructor/destructor
/////////////////////////////////////////////////////////////////////////////
FlowEventLog::FlowEventLog() : time_(0), event_(EVENT_MAX),
    flow_handle_(FlowEntry::kInvalidFlowHandle), flow_gen_id_(0),
    ksync_entry_(NULL), hash_id_(FlowEntry::kInvalidFlowHandle), gen_id_(0),
    vrouter_flow_handle_(FlowEntry::kInvalidFlowHandle), vrouter_gen_id_(0) {
}

FlowEventLog::~FlowEventLog() {
}

/////////////////////////////////////////////////////////////////////////////
// FlowEntry constructor/destructor
/////////////////////////////////////////////////////////////////////////////
FlowEntry::FlowEntry(FlowTable *flow_table) :
    flow_table_(flow_table), flags_(0),
    tunnel_type_(TunnelType::INVALID),
    fip_vmi_(AgentKey::ADD_DEL_CHANGE, nil_uuid(), ""),
    flow_mgmt_request_(NULL), flow_mgmt_info_() {
    // ksync entry is set to NULL only on constructor and on flow delete
    // it should not have any other explicit set to NULL
    ksync_entry_ = NULL;
    Reset();
    alloc_count_.fetch_and_increment();
}

FlowEntry::~FlowEntry() {
    assert(refcount_ == 0);
    Reset();
    alloc_count_.fetch_and_decrement();
}

void FlowEntry::Reset() {
    assert(ksync_entry_ == NULL);
    uuid_ = flow_table_->rand_gen();
    egress_uuid_ = flow_table_->rand_gen();
    data_.Reset();
    l3_flow_ = true;
    gen_id_ = 0;
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
    fsc_ = NULL;
    trace_ = false;
    event_logs_.reset();
    event_log_index_ = 0;
    last_event_ = FlowEvent::INVALID;
    flow_retry_attempts_ = 0;
    is_flow_on_unresolved_list = false;
    pending_actions_.Reset();
    assert(flow_mgmt_request_ == NULL);
    assert(flow_mgmt_info_.get() == NULL);
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
// When flow is being updated, rhs will be new flow allocated in PktFlowInfo
void FlowEntry::Copy(FlowEntry *rhs, bool update) {
    if (update) {
        rhs->data_.in_vm_entry.Reset(false);
        rhs->data_.out_vm_entry.Reset(false);
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
    last_event_ = rhs->last_event_;
    flow_retry_attempts_ = rhs->flow_retry_attempts_;
    trace_ = rhs->trace_;
    if (update == false) {
        gen_id_ = rhs->gen_id_;
        flow_handle_ = rhs->flow_handle_;
        /* Flow Entry is being re-used. Generate a new UUID for it. */
        uuid_ = flow_table_->rand_gen();
        egress_uuid_ = flow_table_->rand_gen();
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
            if (flow_table->ConcurrencyCheck(flow_table->flow_task_id())
                == false) {
                FlowEntryPtr ref(fe);
                FlowProto *proto=flow_table->agent()->pkt()->get_flow_proto();
                proto->ForceEnqueueFreeFlowReference(ref);
                return;
            }
            FlowTable::FlowEntryMap::iterator it =
                flow_table->flow_entry_map_.find(fe->key());
            assert(it != flow_table->flow_entry_map_.end());
            flow_table->flow_entry_map_.erase(it);
            flow_table->agent()->stats()->decr_flow_count();
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

    if (info->alias_ip_flow) {
        set_flags(FlowEntry::AliasIpFlow);
    } else {
        reset_flags(FlowEntry::AliasIpFlow);
    }

    data_.intf_entry = ctrl->intf_ ? ctrl->intf_ : rev_ctrl->intf_;
    data_.vn_entry = ctrl->vn_ ? ctrl->vn_ : rev_ctrl->vn_;
    data_.in_vm_entry.SetVm(ctrl->vm_);
    data_.out_vm_entry.SetVm(rev_ctrl->vm_);
    l3_flow_ = info->l3_flow;
    data_.acl_assigned_vrf_index_ = VrfEntry::kInvalidIndex;
    return true;
}

void FlowEntry::InitFwdFlow(const PktFlowInfo *info, const PktInfo *pkt,
                            const PktControlInfo *ctrl,
                            const PktControlInfo *rev_ctrl,
                            FlowEntry *rflow, Agent *agent) {
    gen_id_ = pkt->GetAgentHdr().cmd_param_5;
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
        RpfInit(ctrl->rt_);
    }

    if (info->bgp_router_service_flow) {
        if (info->ttl == 1) {
            data_.ttl = BGP_SERVICE_TTL_FWD_FLOW;
        }
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
        RpfInit(ctrl->rt_);
    }

    if (info->bgp_router_service_flow) {
        if (info->ttl == 1) {
            data_.ttl = BGP_SERVICE_TTL_REV_FLOW;
        }
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
    data_.component_nh_idx = CompositeNH::kInvalidComponentNHIdx;

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

void FlowEntry::InitAuditFlow(uint32_t flow_idx, uint8_t gen_id) {
    gen_id_ = gen_id;
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

AgentRoute *FlowEntry::GetEvpnRoute(const VrfEntry *vrf,
                                    const MacAddress &mac,
                                    const IpAddress &ip,
                                    uint32_t ethernet_tag) {
    EvpnAgentRouteTable *table = static_cast<EvpnAgentRouteTable *>(
            vrf->GetEvpnRouteTable());
    return table->FindRouteNoLock(mac, ip, ethernet_tag);
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

void FlowEntry::set_flow_handle(uint32_t flow_handle, uint8_t gen_id) {
    if (flow_handle_ != flow_handle) {
        assert(flow_handle_ == kInvalidFlowHandle);
        flow_handle_ = flow_handle;
    }
    gen_id_ = gen_id;
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
        bool set_dest_vrf = true;
        if (is_flags_set(FlowEntry::NatFlow) &&
            reverse_flow_entry() &&
            key().dst_addr != reverse_flow_entry()->key().src_addr) {
            //Packet is getting DNATed, VRF assign ACL action
            //is applied on floating-ip VN and the destination VRF should
            //be retained as interface VRF
            set_dest_vrf = false;
        }

        if (set_dest_vrf) {
            data_.dest_vrf = vrf->vrf_id();
        }
        return;
    }
    data_.acl_assigned_vrf_index_ = VrfEntry::kInvalidIndex;
}

uint32_t FlowEntry::acl_assigned_vrf_index() const {
    return data_.acl_assigned_vrf_index_;
}

void FlowEntry::RevFlowDepInfo(RevFlowDepParams *params) {
    params->sip_ = key().src_addr;
    FlowEntry *rev_flow = reverse_flow_entry();
    if (rev_flow) {
        params->rev_uuid_ = rev_flow->uuid();
        params->vm_cfg_name_ = rev_flow->data().vm_cfg_name;
        params->sg_uuid_ = rev_flow->sg_rule_uuid();
        params->rev_egress_uuid_ = rev_flow->egress_uuid();
        if (rev_flow->intf_entry()) {
            params->vmi_uuid_ = UuidToString(rev_flow->intf_entry()->GetUuid());
        }

        if (key().family != Address::INET) {
            return;
        }
        if (is_flags_set(FlowEntry::NatFlow) &&
            is_flags_set(FlowEntry::IngressDir)) {
            const FlowKey *nat_key = &rev_flow->key();
            if (key().src_addr != nat_key->dst_addr) {
                params->sip_ = nat_key->dst_addr;
            }
        }
    }
}

static bool ShouldDrop(uint32_t action) {
    if (action & TrafficAction::DROP_FLAGS)
        return true;

    if (action & TrafficAction::IMPLICIT_DENY_FLAGS)
        return true;

    return false;
}

/////////////////////////////////////////////////////////////////////////////
// Flow RPF
//
// VRouter enforces RPF check based on RPF-NH programmed in the flow. The RPF
// NH can be of two types,
// - Unicast NH :
//   In this case, VRouter matches with incoming interface/tunnel-src with
//   RPF NH the flow
//
// - ECMP NH :
//   In this case, VRouter picks an ECMP component-nh and matches with incoming
//   interface/tunnel-src. The index for component-nh is got from reverse flow.
//
// RPF-NH cases
// ------------
// 1. Baremetals
//    Agent is not aware of IP address assigned for baremetals. So, RPF check
//    for baremetals is based on the L2-Route
//
// 2. Layer-2 Flows
//    If agent know the Inet route for source-ip in packet, RPF is based on
//    the Inet route for source-ip. There are some exceptions for this rule,
//    - If Inet route for source-ip is ECMP, then RPF is based on layer-2 route
//    - If Inet route for source-ip is not-host route, then RPF is based on
//      layer-2 routes
//
//    If packet is from BMS (egress flow), its possible that agent does not
//    know IP address for BMS. In such case, RPF is based on L2-Route
//
// 3. Layer-3 Flows from VMI
//    RPF check will be based on the InterfaceNH for VMI
//
// 4. Layer-3 Flows from Fabric with unicast-NH
//    The unicast-nh is used as RPF-NH
//
// 5. Layer-3 Flows from Fabric with composite-NH
//    VRouter picks NH from flow and the ecmp-index from reverse flow.
//
//    The ecmp-index in reverse is computed based on route for dest-ip in
//    VRF got post VRF translation if any
//
//    Note, the RPF must be picked post VRF translation since order of members
//    in Composite-NH may vary across VRF
//
// Route Tracking
// --------------
//   Flow Management should track the route for ip1 in vrf4 to update RPF-NH
//
// RPF Computation
// ---------------
// RPF computation happens in two stages
// 1. FlowEntry creation (RpfInit):
//    Called during FlowEntry init. Computes src_ip_nh for flow.
//
//    For layer-2 flows, RPF-NH Is same as src_ip_nh
//    For Non-ECMP layer-3 flows, RPF-NH is same as src_ip_nh
//    For ECMP layer-3 flows, RPF-NH must be computed only after VRF
//    translation is computed for reverse flow.
//
// 2. Post ACL processing (RpfUpdate):
//    Post ACL processing, all VRF translation rules are identified.
//    The RPF-NH is computed in this method.
/////////////////////////////////////////////////////////////////////////////
// Utility method to set rpf_vrf and rpf_plen fields
static void SetRpfFieldsInternal(FlowEntry *flow, const AgentRoute *rt) {
    // If there is no route, we should track default route
    if (rt == NULL) {
        flow->data().rpf_vrf = flow->data().vrf;
        flow->data().rpf_plen = 0;
        return;
    }

    if (dynamic_cast<const InetUnicastRouteEntry *>(rt)) {
        flow->data().rpf_vrf = rt->vrf()->vrf_id();
        flow->data().rpf_plen = rt->plen();
        return;
    }

    // Route is not INET. Dont track any route
    flow->data().rpf_vrf = VrfEntry::kInvalidIndex;
    flow->data().rpf_plen = 0;
    return;
}

// Utility method to set src_ip_nh fields
void FlowEntry::RpfSetSrcIpNhFields(const AgentRoute *rt,
                                    const NextHop *src_ip_nh) {
    data_.src_ip_nh.reset(src_ip_nh);
    SetRpfFieldsInternal(this, rt);
    return;
}

void FlowEntry::RpfSetRpfNhFields(const NextHop *rpf_nh) {
    data_.rpf_nh.reset(rpf_nh);
}

// Utility method to set rpf_nh fields
void FlowEntry::RpfSetRpfNhFields(const AgentRoute *rt, const NextHop *rpf_nh) {
    data_.rpf_nh.reset(rpf_nh);
    SetRpfFieldsInternal(this, rt);
    return;
}

// This method is called when flow is initialized. The RPF-NH cannot be
// computed at this stage since we dont know if reverse flow has VRF
// translation or not.
// This method only sets src_ip_nh field
//
// In case of layer-3 flow "rt" is inet route for source-ip in source-vrf
// In case of layer-2 flow "rt" is l2 route for smac in source-vrf
void FlowEntry::RpfInit(const AgentRoute *rt) {
    // Set src_ip_nh based on rt first
    RpfSetSrcIpNhFields(rt, rt->GetActiveNextHop());

    // RPF enabled?
    bool rpf_enable = true;
    if (data_.vn_entry && data_.vn_entry->enable_rpf() == false)
        rpf_enable = false;

    // The src_ip_nh can change below only for l2 flows
    // For l3-flow, rt will already be a INET route
    if (l3_flow())
        return;

    // For layer-2 flows, we use l2-route for RPF in following cases
    // 1. Interface is of type BAREMETAL (ToR/TSN only)
    //
    // 2. ECMP is not supported for l2-flows. If src-ip lookup resulted in
    //    ECMP-NH fallback to the original l2-route
    //
    // 3. In case of OVS, ToR will export layer-2 route and MX will export a
    //    layer-3 subnet route covering all the ToRs. In this case, when ToR
    //    send layer-2 packet the layer-3 route will point to MX and RPF fails.
    //    Assuming MX only gives subnet-route, use inet-route only if its
    //    host-route
    // 4. Its an egress flow and there is no route for IP address
    const VmInterface *vmi =
        dynamic_cast<const VmInterface *>(intf_entry());
    if (vmi && vmi->vmi_type() == VmInterface::BAREMETAL) {
        return;
    }

    const InetUnicastRouteEntry *src_ip_rt =
        static_cast<InetUnicastRouteEntry *>
        (FlowEntry::GetUcRoute(rt->vrf(), key().src_addr));

    if (src_ip_rt == NULL) {
        // For egress flow, with no l3-route then do rpf based on l2-route
        // For ingress flow, with no l3-route, make it short flow
        if (rpf_enable && IsIngressFlow()) {
            set_flags(FlowEntry::ShortFlow);
            short_flow_reason_ = SHORT_NO_SRC_ROUTE_L2RPF;
        }
        return;
    }

    if (src_ip_rt->IsHostRoute() == false)
        return;

    const NextHop *src_ip_nh = src_ip_rt->GetActiveNextHop();
    if (src_ip_nh->GetType() == NextHop::COMPOSITE)
        return;

    RpfSetSrcIpNhFields(src_ip_rt, src_ip_nh);
    return;
}

// Should src_ip_nh be treated as RPF-NH
bool FlowEntry::RpfFromSrcIpNh() const {
    // For l2-flows, src_ip_nh is same as RPF-NH
    if (l3_flow() == false)
        return true;

    // Dont bother about RPF for short-flows
    if (is_flags_set(FlowEntry::ShortFlow))
        return true;

    // rpf-nh can change only in case of composite
    if (data_.src_ip_nh->GetType() != NextHop::COMPOSITE)
        return true;

    const FlowEntry *rflow = reverse_flow_entry();
    if (rflow == NULL) {
        FlowInfo flow_info;
        FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Invalid reverse flow for setting ECMP index",
                   flow_info);
        return true;
    }

    return false;
}

void FlowEntry::RpfComputeIngress() {
    const NextHop *nh = NULL;
    // If we are here, its guaranteed that src_ip_nh is composite
    const CompositeNH *cnh =
        dynamic_cast<const CompositeNH *>(data_.src_ip_nh.get());
    assert(cnh != NULL);

    // Use flow_key_nh if VMI is part of composite. Else its guaranteed
    // that RPF will fail for flow. So, use discard-nh for RPF
    const VmInterface *vmi =
        dynamic_cast<const VmInterface *>(intf_entry());
    if (cnh->HasVmInterface(vmi)) {
        nh = vmi->flow_key_nh();
    } else {
        nh = data_.src_ip_nh.get();
    }

    // Change only the RPF-NH. The vrf and plen dont change here
    RpfSetRpfNhFields(nh);
    return;
}

void FlowEntry::RpfComputeEgress() {
    const FlowEntry *rflow = reverse_flow_entry();

    // RPF-NH can change only if VRF in forward flow and translated VRF in
    // reverse flow are different
    if (rflow->data().flow_dest_vrf == data_.vrf) {
        RpfSetRpfNhFields(data_.src_ip_nh.get());
        return;
    }

    // Find destination VRF from reverse flow
    const VrfEntry *vrf = rflow->GetDestinationVrf();
    if (vrf == NULL) {
        FlowInfo flow_info;
        FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Error setting RPF NH. Destination VRF not found "
                   "in reverse flow", flow_info);
        return;
    }

    InetUnicastRouteEntry *rt = static_cast<InetUnicastRouteEntry *>
        (FlowEntry::GetUcRoute(vrf, key().src_addr));
    if (!rt ) {
        FlowInfo flow_info;
        FillFlowInfo(flow_info);
        FLOW_TRACE(Trace, "Error setting RPF NH. Route not found in "
                  "destination vrf", flow_info);
        return;
    }

    RpfSetRpfNhFields(rt, rt->GetActiveNextHop());
    return;
}

// Computes RPF-NH for flow based on ip_src_nh
// Must be called after policy lookup is done
void FlowEntry::RpfUpdate() {
    // Is RPF check enabled?
    data_.enable_rpf = true;
    if (data_.vn_entry) {
        data_.enable_rpf = data_.vn_entry->enable_rpf();
    }

    if (data_.enable_rpf == false) {
        data_.rpf_nh = NULL;
        return;
    }

    if (RpfFromSrcIpNh()) {
        data_.rpf_nh = data_.src_ip_nh;
        return;
    }

    if (is_flags_set(FlowEntry::IngressDir)) {
        RpfComputeIngress();
    } else {
        RpfComputeEgress();
    }
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
    // VRF-Assign rules valid only for routed packets
    if (l3_flow() == false)
        return;

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
    /* If policy is NOT enabled on VMI, do not copy SG rules */
    if (!vm_port->policy_enabled()) {
        return false;
    }
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

    //Update reverse SG flow entry so that it can be used in below 2 scenario
    //1> Forwarding flow is deny, but reverse flow says Allow the traffic
    //   in that scenario we mark the reverse flow for Trap
    //2> TCP ACK workaround:
    //   Ideally TCP State machine should be run to age TCP flows
    //   Temporary workaound in place of state machine. For TCP ACK packets allow
    //   the flow if either forward or reverse flow is allowed

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

    //Update reverse SG flow entry so that it can be used in below 2 scenario
    //1> Forwarding flow is deny, but reverse flow says Allow the traffic
    //   in that scenario we mark the reverse flow for Trap
    //2> TCP ACK workaround:
    //   Ideally TCP State machine should be run to age TCP flows
    //   Temporary workaound in place of state machine. For TCP ACK packets allow
    //   the flow if either forward or reverse flow is allowed

    // Copy the SG rules to be applied for reverse flow
    data_.match_p.reverse_out_sg_rule_present =
        CopySgEntries(vm_port, !ingress,
                      data_.match_p.m_reverse_out_sg_acl_l);
    data_.match_p.reverse_sg_rule_present = false;
}

// For an L2-Flow, refresh the vn-list and sg-list from the route used for
// flow
void FlowEntry::UpdateL2RouteInfo() {
    // Skip L3-Flows
    if (l3_flow())
        return;

    Agent *agent = flow_table()->agent();
    // Get VRF for the flow. L2 flow have same in and out-vrf. So, use same
    // vrf for both smac and dmac lookup
    uint32_t vrf_id = data().flow_source_vrf;
    const VrfEntry *vrf = agent->vrf_table()->FindVrfFromId(vrf_id);
    if (vrf == NULL || vrf->IsDeleted()) {
        return;
    }
    BridgeAgentRouteTable *table =
        static_cast<BridgeAgentRouteTable *>(vrf->GetBridgeRouteTable());

    // Get route-info for smac
    GetSourceRouteInfo(table->FindRoute(data().smac));
    // Get route-info for dmac
    GetDestRouteInfo(table->FindRoute(data().dmac));
}

/////////////////////////////////////////////////////////////////////////////
// Flow policy processing routines
/////////////////////////////////////////////////////////////////////////////
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
    if (acl_assigned_vrf_index() == VrfEntry::kInvalidIndex) {
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

void FlowEntry::SetSgAclInfo(const FlowPolicyInfo &fwd_flow_info,
                             const FlowPolicyInfo &rev_flow_info,
                             bool tcp_rev_sg) {

    FlowEntry *rflow = reverse_flow_entry();
    if (rflow == NULL) {
        return;
    }

    sg_rule_uuid_ = fwd_flow_info.uuid;
    rflow->sg_rule_uuid_ = rev_flow_info.uuid;

    //If Forward flow SG rule says drop, copy corresponding
    //ACE id to both forward and reverse flow
    if (fwd_flow_info.drop) {
        rflow->sg_rule_uuid_ = fwd_flow_info.uuid;
        return;
    }

    //If reverse flow SG rule says drop, copy corresponding
    //ACE id to both forward and reverse flow
    if (rev_flow_info.drop) {
        sg_rule_uuid_ = rev_flow_info.uuid;
        return;
    }

    if (tcp_rev_sg == false) {
        if (data_.match_p.sg_rule_present == false) {
            sg_rule_uuid_ = rev_flow_info.uuid;
        }

        if (data_.match_p.out_sg_rule_present == false) {
            rflow->sg_rule_uuid_ = fwd_flow_info.uuid;
        }
    }

    if (tcp_rev_sg == true) {
        if (data_.match_p.reverse_sg_rule_present == false) {
             rflow->sg_rule_uuid_ = fwd_flow_info.uuid;
        }

        if (data_.match_p.reverse_out_sg_rule_present == false) {
            sg_rule_uuid_ = rev_flow_info.uuid;
        }
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
    FlowPolicyInfo out_sg_acl_info(value);
    FlowPolicyInfo rev_sg_acl_info(value);
    FlowPolicyInfo rev_out_sg_acl_info(value);

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
                         !data_.match_p.out_sg_rule_present, &out_sg_acl_info);
        }

        bool check_rev_sg = false;
        if (ShouldDrop(data_.match_p.sg_action) ||
            ShouldDrop(data_.match_p.out_sg_action)) {
            //If forward direction SG action is DENY
            //verify if packet is allowed in reverse side,
            //if yes we set a flag to TRAP the reverse flow
            check_rev_sg = true;
        }

        // For TCP-ACK packet, we allow packet if either forward or reverse
        // flow says allow. So, continue matching reverse flow even if forward
        // flow says drop
        if ((check_rev_sg || is_flags_set(FlowEntry::TcpAckFlow)) && rflow) {
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
                             &rev_out_sg_acl_info);
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
                SetSgAclInfo(sg_acl_info, out_sg_acl_info, false);
        } else {
            if (ShouldDrop(data_.match_p.sg_action |
                           data_.match_p.out_sg_action)
                &&
                ShouldDrop(data_.match_p.reverse_sg_action |
                           data_.match_p.reverse_out_sg_action)) {
                data_.match_p.sg_action_summary = (1 << TrafficAction::DENY);
                SetSgAclInfo(sg_acl_info, out_sg_acl_info, false);
            } else {
                data_.match_p.sg_action_summary = (1 << TrafficAction::PASS);
                if (!ShouldDrop(data_.match_p.sg_action |
                                data_.match_p.out_sg_action)) {
                    SetSgAclInfo(sg_acl_info, out_sg_acl_info, false);
                } else if (!ShouldDrop(data_.match_p.reverse_sg_action |
                                       data_.match_p.reverse_out_sg_action)) {
                    SetSgAclInfo(rev_out_sg_acl_info, rev_sg_acl_info, true);
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

const VrfEntry*
FlowEntry::GetDestinationVrf() const {
    const VrfEntry *vrf = NULL;
    VrfTable *vrf_table = flow_table()->agent()->vrf_table();

    if (is_flags_set(FlowEntry::NatFlow) ||
        match_p().action_info.action & (1 << TrafficAction::VRF_TRANSLATE)) {
        vrf = vrf_table->FindVrfFromId(data().dest_vrf);
    } else {
        vrf = vrf_table->FindVrfFromId(data().vrf);
    }
    return vrf;
}

bool FlowEntry::SetQosConfigIndex() {
    uint32_t i = AgentQosConfigTable::kInvalidIndex;
    MatchAclParamsList::const_iterator it;

    //Priority of QOS config
    // 1> SG
    // 2> Interface
    // 3> ACL
    // 4> VN
    if (is_flags_set(FlowEntry::ReverseFlow) &&
        data_.match_p.sg_action_summary & 1 << TrafficAction::APPLY_QOS) {
        i = reverse_flow_entry()->data().qos_config_idx;
    } else if (data_.match_p.sg_action & 1 << TrafficAction::APPLY_QOS) {
        for(it = data_.match_p.m_sg_acl_l.begin();
                it != data_.match_p.m_sg_acl_l.end(); it++) {
            if (it->action_info.action & 1 << TrafficAction::APPLY_QOS &&
                    it->action_info.qos_config_action_.id()
                    != AgentQosConfigTable::kInvalidIndex) {
                i = it->action_info.qos_config_action_.id();
                break;
            }
        }
    } else if (data_.match_p.out_sg_action & 1 << TrafficAction::APPLY_QOS) {
        for(it = data_.match_p.m_out_sg_acl_l.begin();
                it != data_.match_p.m_out_sg_acl_l.end(); it++) {
            if (it->action_info.action & 1 << TrafficAction::APPLY_QOS &&
                    it->action_info.qos_config_action_.id() !=
                    AgentQosConfigTable::kInvalidIndex) {
                i = it->action_info.qos_config_action_.id();
                break;
            }
        }
    } else if (data_.match_p.policy_action & 1 << TrafficAction::APPLY_QOS) {
        for(it = data_.match_p.m_acl_l.begin();
                it != data_.match_p.m_acl_l.end(); it++) {
            if (it->action_info.action & 1 << TrafficAction::APPLY_QOS &&
                    it->action_info.qos_config_action_.id() !=
                    AgentQosConfigTable::kInvalidIndex) {
                i = it->action_info.qos_config_action_.id();
                break;
            }
        }
    } else if (data_.match_p.out_policy_action & 1 << TrafficAction::APPLY_QOS) {
        for(it = data_.match_p.m_out_acl_l.begin();
                it != data_.match_p.m_out_acl_l.end(); it++) {
            if (it->action_info.action & 1 << TrafficAction::APPLY_QOS &&
                    it->action_info.qos_config_action_.id() !=
                    AgentQosConfigTable::kInvalidIndex) {
                i = it->action_info.qos_config_action_.id();
                break;
            }
        }
    }

    const VmInterface *intf =
        dynamic_cast<const VmInterface*>(data_.intf_entry.get());
    if (intf && intf->qos_config()) {
        if (intf->is_vn_qos_config() == false ||
                i == AgentQosConfigTable::kInvalidIndex) {
            i = intf->qos_config()->id();
        }
    }

    if (i != data_.qos_config_idx) {
        data_.qos_config_idx = i;
        return true;
    }

    return false;
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

    if (SetQosConfigIndex()) {
        ret = true;
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
    }
    // If forward flow is DROP, set action for reverse flow to
    // TRAP. If packet hits reverse flow, we will re-establish
    // the flows
    if (ShouldDrop(data_.match_p.sg_action_summary)) {
        if (fwd_flow &&
            ShouldDrop(fwd_flow->data().match_p.reverse_sg_action) == false &&
            ShouldDrop(fwd_flow->data().match_p.reverse_out_sg_action) == false) {
            data_.match_p.sg_action &= ~(TrafficAction::DROP_FLAGS);
            set_flags(FlowEntry::Trap);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
// Routines to manage pending actions on a flow. The pending actions are used
// to state-compress actions trigged due to update of,
// - DBEntries like interface, ACL etc..
// - Routes
/////////////////////////////////////////////////////////////////////////////
FlowPendingAction::FlowPendingAction()  {
    Reset();
}

FlowPendingAction::~FlowPendingAction() {
}

void FlowPendingAction::Reset() {
    delete_ = false;
    recompute_ = false;
    recompute_dbentry_ = false;
    revaluate_ = false;
}

bool FlowPendingAction::SetDelete() {
    if (delete_)
        return false;

    delete_ = true;
    return true;
}

void FlowPendingAction::ResetDelete() {
    delete_ = false;
    recompute_ = false;
    recompute_dbentry_ = false;
    revaluate_ = false;
}

bool FlowPendingAction::CanDelete() {
    return delete_;
}

bool FlowPendingAction::SetRecompute() {
    if (delete_ || recompute_)
        return false;

    recompute_ = true;
    return true;
}

void FlowPendingAction::ResetRecompute() {
    recompute_ = false;
    recompute_dbentry_ = false;
    revaluate_ = false;
}

bool FlowPendingAction::CanRecompute() {
    if (delete_)
        return false;

    return recompute_;
}

bool FlowPendingAction::SetRecomputeDBEntry() {
    if (delete_ || recompute_ || recompute_dbentry_)
        return false;

    recompute_dbentry_ = true;
    return true;
}

void FlowPendingAction::ResetRecomputeDBEntry() {
    recompute_dbentry_ = false;
    revaluate_ = false;
}

bool FlowPendingAction::CanRecomputeDBEntry() {
    if (delete_ || recompute_)
        return false;

    return recompute_dbentry_;
}

bool FlowPendingAction::SetRevaluate() {
    if (delete_ || recompute_ || recompute_dbentry_ || revaluate_)
        return false;

    revaluate_ = true;
    return true;
}

void FlowPendingAction::ResetRevaluate() {
    revaluate_ = false;
}

bool FlowPendingAction::CanRevaluate() {
    if (delete_ || recompute_ || recompute_dbentry_)
        return false;

    return revaluate_;
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

void FlowEntry::FillFlowInfo(FlowInfo &info) const {
    info.set_gen_id(gen_id_);
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

    if (reverse_flow_entry_.get()) {
        info.set_reverse_index(reverse_flow_entry_->flow_handle());
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
            info.set_nat_mirror_vrf(nat_flow->data().mirror_vrf);
        }
    }

    if (data_.match_p.action_info.action & (1 << TrafficAction::MIRROR)) {
        info.set_mirror(true);
        std::vector<MirrorActionSpec>::const_iterator it;
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
    info.set_short_flow_reason(FlowEntry::DropReasonStr(short_flow_reason_));
    info.set_drop_reason(FlowEntry::DropReasonStr(data_.drop_reason));
    if (flow_table_) {
        info.set_table_id(flow_table_->table_index());
    }

    if (rpf_nh()) {
        info.set_rpf_nh(rpf_nh()->id());
    } else {
        info.set_rpf_nh(0xFFFFFFFF);
    }
    if (src_ip_nh()) {
        info.set_src_ip_nh(src_ip_nh()->id());
    } else {
        info.set_src_ip_nh(0xFFFFFFFF);
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
    fe_sandesh_data.set_flow_uuid(UuidToString(uuid()));
    if (fsc_) {
        const FlowExportInfo *info = fsc_->FindFlowExportInfo(this);
        if (info) {
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

static std::string EventToString(FlowEventLog::Event event,
                                 std::string &event_str) {
    switch (event) {
    case FlowEventLog::FLOW_ADD:
        event_str = "FlowAdd";
        break;
    case FlowEventLog::FLOW_UPDATE:
        event_str = "FlowUpdate";
        break;
    case FlowEventLog::FLOW_DELETE:
        event_str = "FlowDelete";
        break;
    case FlowEventLog::FLOW_EVICT:
        event_str = "FlowEvict";
        break;
    case FlowEventLog::FLOW_HANDLE_ASSIGN:
        event_str = "FlowHandleAssign";
        break;
    case FlowEventLog::FLOW_MSG_SKIP_EVICTED:
        event_str = "FlowMessageSkippedEvictedFlow";
        break;
    default:
        event_str = "Unknown";
        break;
    }
    return event_str;
}

void FlowEntry::SetEventSandeshData(SandeshFlowIndexInfo *info) {
    KSyncFlowIndexManager *mgr =
        flow_table_->agent()->ksync()->ksync_flow_index_manager();
    info->set_trace_index(event_log_index_);
    if (mgr->sm_log_count() == 0) {
        return;
    }
    int start = 0;
    int count = event_log_index_;
    if (event_log_index_ >= mgr->sm_log_count()) {
        start = event_log_index_ % mgr->sm_log_count();
        count = mgr->sm_log_count();
    }
    std::vector<SandeshFlowIndexTrace> trace_list;
    for (int i = 0; i < count; i++) {
        SandeshFlowIndexTrace trace;
        FlowEventLog *log = &event_logs_[((start + i) % mgr->sm_log_count())];
        trace.set_timestamp(log->time_);
        trace.set_flow_handle(log->flow_handle_);
        trace.set_flow_gen_id(log->flow_gen_id_);
        string event_str;
        trace.set_event(EventToString(log->event_, event_str));
        trace.set_ksync_hash_id(log->hash_id_);
        trace.set_ksync_gen_id(log->gen_id_);
        trace.set_vrouter_flow_handle(log->vrouter_flow_handle_);
        trace.set_vrouter_gen_id(log->vrouter_gen_id_);
        trace_list.push_back(trace);
    }
    info->set_flow_index_trace(trace_list);
}

void FlowEntry::LogFlow(FlowEventLog::Event event, FlowTableKSyncEntry* ksync,
                        uint32_t flow_handle, uint8_t gen_id) {
    KSyncFlowIndexManager *mgr =
        flow_table_->agent()->ksync()->ksync_flow_index_manager();
    string event_str;
    LOG(DEBUG, "Flow event = " << EventToString(event, event_str)
        << " flow = " << (void *)this
        << " flow->flow_handle = " << flow_handle_
        << " flow->gen_id = " << (int)gen_id_
        << " ksync = " << (void *)ksync
        << " Ksync->hash_id = " << ((ksync != NULL) ? ksync->hash_id() : -1)
        << " Ksync->gen_id = " << ((ksync != NULL) ? (int)ksync->gen_id() : 0)
        << " new_flow_handle = " << flow_handle
        << " new_gen_id = " << (int)gen_id);

    if (mgr->sm_log_count() == 0) {
        return;
    }

    if (event_logs_ == NULL) {
        event_log_index_ = 0;
        event_logs_.reset(new FlowEventLog[mgr->sm_log_count()]);
    }

    FlowEventLog *log = &event_logs_[event_log_index_ % mgr->sm_log_count()];
    event_log_index_++;

    log->time_ = ClockMonotonicUsec();
    log->event_ = event;
    log->flow_handle_ = flow_handle_;
    log->flow_gen_id_ = gen_id_;
    log->ksync_entry_ = ksync;
    log->hash_id_ = (ksync != NULL) ? ksync->hash_id() : -1;
    log->gen_id_ = (ksync != NULL) ? ksync->gen_id() : 0;
    log->vrouter_flow_handle_ = flow_handle;
    log->vrouter_gen_id_ = gen_id;
}
