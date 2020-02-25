/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */


#include "base/os.h"
#include <arpa/inet.h>
#include <netinet/in.h>

#include "base/address_util.h"
#include "route/route.h"

#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "oper/ecmp.h"
#include "oper/interface_common.h"
#include "oper/metadata_ip.h"
#include "oper/nexthop.h"
#include "oper/route_common.h"
#include "oper/path_preference.h"
#include "oper/vrf.h"
#include "oper/sg.h"
#include "oper/global_vrouter.h"
#include "oper/operdb_init.h"
#include "oper/tunnel_nh.h"
#include "oper/bgp_as_service.h"
#include "oper/health_check.h"

#include "filter/packet_header.h"
#include "filter/acl.h"

#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "pkt/pkt_handler.h"
#include "pkt/flow_mgmt.h"
#include "pkt/flow_proto.h"
#include "pkt/pkt_sandesh_flow.h"
#include "cmn/agent_stats.h"
#include <vrouter/ksync/flowtable_ksync.h>
#include <vrouter/ksync/ksync_init.h>

const Ip4Address PktFlowInfo::kDefaultIpv4;
const Ip6Address PktFlowInfo::kDefaultIpv6;

static void LogError(const PktInfo *pkt, const PktFlowInfo *flow_info,
                     const char *str) {
    if (pkt->family == Address::INET || pkt->family == Address::INET6) {
        FLOW_TRACE(DetailErr, pkt->agent_hdr.cmd_param, pkt->agent_hdr.ifindex,
                   pkt->agent_hdr.vrf, pkt->ip_saddr.to_string(),
                   pkt->ip_daddr.to_string(), str, flow_info->l3_flow);
    } else {
        assert(0);
    }
}

// VRF changed for the packet. Treat it as Layer3 packet from now.
// Note:
// Features like service chain are supported only for Layer3. Bridge
// entries are not leaked into the new VRF and any bridge entry lookup
// into new VRF will also Fail. So, even VRouter will treat packets
// as L3 after VRF transaltion.
void PktFlowInfo::ChangeVrf(const PktInfo *pkt, PktControlInfo *info,
                            const VrfEntry *vrf) {
    l3_flow = true;
}

void PktFlowInfo::UpdateRoute(const AgentRoute **rt, const VrfEntry *vrf,
                              const IpAddress &ip, const MacAddress &mac,
                              FlowRouteRefMap &ref_map) {
    if (*rt != NULL && (*rt)->GetTableType() != Agent::BRIDGE)
        ref_map[(*rt)->vrf_id()] = RouteToPrefixLen(*rt);
    if (l3_flow) {
        *rt = FlowEntry::GetUcRoute(vrf, ip);
    } else {
        *rt = FlowEntry::GetL2Route(vrf, mac);
    }
    if (*rt == NULL)
        ref_map[vrf->vrf_id()] = 0;
}

uint8_t PktFlowInfo::RouteToPrefixLen(const AgentRoute *route) {
    if (route == NULL) {
        return 0;
    }

    const InetUnicastRouteEntry *inet_rt =
        dynamic_cast<const InetUnicastRouteEntry *>(route);
    if (inet_rt != NULL) {
        return inet_rt->plen();
    }

    const BridgeRouteEntry *l2_rt =
        dynamic_cast<const BridgeRouteEntry *>(route);
    if (l2_rt) {
        return l2_rt->mac().bit_len();
    }

    assert(0);
    return -1;
}

// Traffic from IPFabric to VM is treated as EGRESS
// Any other traffic is INGRESS
bool PktFlowInfo::ComputeDirection(const Interface *intf) {
    bool ret = true;
    if (intf->type() == Interface::PHYSICAL) {
        ret = false;
    }
    return ret;
}


// Get VRF corresponding to a NH
static uint32_t NhToVrf(const NextHop *nh) {
    const VrfEntry *vrf = NULL;
    switch (nh->GetType()) {
    case NextHop::COMPOSITE: {
        vrf = (static_cast<const CompositeNH *>(nh))->vrf();
        break;
    }
    case NextHop::NextHop::INTERFACE: {
        const Interface *intf =
            (static_cast<const InterfaceNH *>(nh))->GetInterface();
        if (intf)
            vrf = intf->vrf();
        break;
    }
    default:
        break;
    }

    if (vrf == NULL)
        return VrfEntry::kInvalidIndex;

    if (!vrf->IsActive())
        return VrfEntry::kInvalidIndex;

    return vrf->vrf_id();
}

static bool IsVgwOrVmInterface(const Interface *intf) {
    if (intf->type() == Interface::VM_INTERFACE)
        return true;

    if (intf->type() == Interface::INET) {
        const InetInterface *inet = static_cast<const InetInterface *>(intf);
        if (inet->sub_type() == InetInterface::SIMPLE_GATEWAY)
            return true;
    }
    return false;
}

static bool PickEcmpMember(const Agent *agent, const NextHop **nh,
               const PktInfo *pkt, PktFlowInfo *info,
                           const EcmpLoadBalance &ecmp_load_balance) {
    const CompositeNH *comp_nh = dynamic_cast<const CompositeNH *>(*nh);
    // ECMP supported only if composite-type is ECMP or LOCAL_ECMP or LU_ECMP
    if (comp_nh == NULL ||
        (comp_nh->composite_nh_type() != Composite::ECMP &&
        comp_nh->composite_nh_type() != Composite::LOCAL_ECMP &&
        comp_nh->composite_nh_type() != Composite::LU_ECMP)) {
        info->out_component_nh_idx = CompositeNH::kInvalidComponentNHIdx;
        return true;
    }

    info->ecmp = true;
    // If this is flow revluation,
    // 1. If flow transitions from non-ECMP to ECMP, the old-nh will be first
    //    member in the composite-nh. So set affinity-nh index to 0
    // 2. If flow is already ECMP, the out-component-nh-idx is retained as
    //    affinity
    if (pkt->type == PktType::MESSAGE &&
        info->out_component_nh_idx == CompositeNH::kInvalidComponentNHIdx) {
        info->out_component_nh_idx = 0;
    }

    // Compute out_component_nh_idx,
    // 1. In case of non-ECMP to ECMP transition, component-nh-index and
    //    in-turn affinity-nh is set to 0
    // 2. In case of MESSAGE, the old component-nh-index is used as affinity-nh
    // 3. In case of new flows, new index is allocated
    //
    // If affinity-nh is set but points to deleted NH, then affinity is ignored
    // and new index is allocated
    info->out_component_nh_idx =
        comp_nh->PickMember(pkt->hash(agent, ecmp_load_balance),
                            info->out_component_nh_idx,
                            info->ingress);
    *nh = comp_nh->GetNH(info->out_component_nh_idx);
    // nh can be NULL if out_component_nh_index is invalid
    // this index is returned as invalid from the above function
    // when composite nexthop is having inactive/NULL
    // component nexthops or no local nexthops if traffic
    // is coming from fabric
    if ((*nh) && ((*nh)->GetType() == NextHop::COMPOSITE)) {
        // this is suboptimal solution to pick component NH in
        // 2 level ecmp. ideally hashing should be independent for
        // VPN level ecmp and label inet ecmp. label inet ecmp relies on
        // underlay node information which is not available in packet
        // if pkt originates from local VM, so using same ecmp index
        // to get component nh for both vpn and label inet ecmp
        // this results in either (0,0) or (1,1)
        // TODO:find optimum solution to address this
        const CompositeNH *comp_composite_nh =
            static_cast<const CompositeNH *>(*nh);
        if (comp_composite_nh->composite_nh_type()  == Composite::LU_ECMP) {
            *nh = comp_composite_nh->GetNH(info->out_component_nh_idx);
        }
    }

    // TODO: Should we re-hash here?
    if (!(*nh) || (*nh)->IsActive() == false) {
        return false;
    }

    return true;
}

// Get interface from a NH. Also, decode ECMP information from NH
// Responsible to set following fields,
// out->nh_ : outgoing Nexthop Index. Will also be used to set reverse flow-key
// out->vrf_ : VRF to be used after flow processing. Value set here can
//             potentially get overridden later
//             TODO: Revisit the use of vrf_, dest_vrf and nat_vrf
// force_vmport means, we want destination to be VM_INTERFACE only
// This is to avoid routing across fabric interface itself
static bool NhDecode(const Agent *agent, const NextHop *nh, const PktInfo *pkt,
             PktFlowInfo *info, PktControlInfo *in,
             PktControlInfo *out, bool force_vmport,
                     const EcmpLoadBalance &ecmp_load_balance) {
    bool ret = true;

    if (!nh->IsActive())
        return false;

    // If nh is Composite, pick the ECMP first. The nh index and vrf used
    // in reverse flow will depend on the ECMP member picked
    if (PickEcmpMember(agent, &nh, pkt, info, ecmp_load_balance) == false) {
        return false;
    }

    // Pick out going attributes based on the NH selected above
    switch (nh->GetType()) {
    case NextHop::INTERFACE:
        out->intf_ = static_cast<const InterfaceNH*>(nh)->GetInterface();
        if (out->intf_->type() == Interface::VM_INTERFACE) {
            //Local flow, pick destination interface
            //nexthop as reverse flow key
            if (out->intf_->flow_key_nh() == NULL)
                return false;
            out->nh_ = out->intf_->flow_key_nh()->id();
            out->vrf_ = static_cast<const InterfaceNH*>(nh)->GetVrf();
            const VmInterface *vm_port =
                dynamic_cast<const VmInterface *>(out->intf_);
            if (vm_port != NULL) {
                VrfEntry *alias_vrf = vm_port->GetAliasIpVrf(pkt->ip_daddr);
                if (alias_vrf != NULL) {
                    out->vrf_ = alias_vrf;
                    // translate to alias ip vrf for destination, unless
                    // overriden by translation due to NAT or ACL
                    info->dest_vrf = alias_vrf->vrf_id();
                    info->alias_ip_flow = true;
                }
            }
        } else if (out->intf_->type() == Interface::PACKET) {
            //Packet destined to pkt interface, packet originating
            //from pkt0 interface will use destination interface as key
            out->nh_ = in->nh_;
        } else {
            // Most likely a GATEWAY interface.
            // Remote flow, use source interface as nexthop key
            out->nh_ = nh->id();
            out->vrf_ = static_cast<const InterfaceNH*>(nh)->GetVrf();
        }
        break;

    case NextHop::RECEIVE:
        assert(info->l3_flow == true);
        out->intf_ = static_cast<const ReceiveNH *>(nh)->GetInterface();
        out->vrf_ = out->intf_->vrf();
        if (out->intf_->vrf()->forwarding_vrf()) {
            out->vrf_ = out->intf_->vrf()->forwarding_vrf();
        }
        out->nh_ = out->intf_->flow_key_nh()->id();
        break;

    case NextHop::VLAN: {
        assert(info->l3_flow == true);
        const VlanNH *vlan_nh = static_cast<const VlanNH*>(nh);
        out->intf_ = vlan_nh->GetInterface();
        out->vlan_nh_ = true;
        out->vlan_tag_ = vlan_nh->GetVlanTag();
        out->vrf_ = vlan_nh->GetVrf();
        out->nh_ = nh->id();
        break;
    }

        // Destination present on remote-compute node. The reverse traffic will
        // have MPLS label. The MPLS label can point to
        // 1. In case of non-ECMP, label will points to local interface
        // 2. In case of ECMP, label will point to ECMP of local-composite members
        // Setup the NH for reverse flow appropriately
    case NextHop::TUNNEL: {
        // out->intf_ is invalid for packets going out on tunnel. Reset it.
        out->intf_ = NULL;

        // Packet going out on tunnel. Assume NH in reverse flow is same as
        // that of forward flow. It can be over-written down if route for
        // source-ip is ECMP
        if (info->port_allocated == false) {
            out->nh_ = in->nh_;
        }

        // The NH in reverse flow can change only if ECMP-NH is used. There is
        // no ECMP for layer2 flows
        if (info->l3_flow == false) {
            break;
        }

        // If source-ip is in ECMP, reverse flow would use ECMP-NH as key
        const InetUnicastRouteEntry *rt =
            dynamic_cast<const InetUnicastRouteEntry *>(in->rt_);
        if (rt == NULL) {
            break;
        }

        // Get only local-NH from route
        const NextHop *local_nh = EcmpData::GetLocalNextHop(rt);
        if (local_nh && local_nh->IsActive() == false) {
            LogError(pkt, info, "Invalid or Inactive local nexthop ");
            info->short_flow = true;
            info->short_flow_reason = FlowEntry::SHORT_UNAVIALABLE_INTERFACE;
            break;
        }

        // Change NH in reverse flow if route points to composite-NH
        const CompositeNH *comp_nh = dynamic_cast<const CompositeNH *>
            (local_nh);
        if (comp_nh != NULL) {
            out->nh_ = comp_nh->id();
        }
        break;
    }

        // COMPOSITE is valid only for multicast traffic. We simply forward
        // multicast traffic and its mostly unidirectional. nh_ used in reverse
        // flow woule not matter really
    case NextHop::COMPOSITE: {
        out->nh_ = nh->id();
        out->intf_ = NULL;
        break;
    }

        // VRF Nexthop means traffic came as tunnelled packet and interface is
        // gateway kind of interface. It also means ARP is not yet resolved for the
        // dest-ip (otherwise we should have it ARP-NH). Let out->nh_ to be same as
        // in->nh_. It will be modified later when ARP is resolved
    case NextHop::VRF: {
        const VrfNH *vrf_nh = static_cast<const VrfNH *>(nh);
        out->vrf_ = vrf_nh->GetVrf();
        break;
    }

        // ARP Nexthop means outgoing interface is gateway kind of interface with
        // ARP already resolved
    case NextHop::ARP: {
        assert(info->l3_flow == true);
        const ArpNH *arp_nh = static_cast<const ArpNH *>(nh);
        if (in->intf_->type() == Interface::VM_INTERFACE) {
            const VmInterface *vm_intf =
                static_cast<const VmInterface *>(in->intf_);
            if (vm_intf->vmi_type() == VmInterface::VHOST) {
                out->nh_ = in->intf_->flow_key_nh()->id();
                out->intf_ = in->intf_;
            } else if (vm_intf->device_type() == VmInterface::LOCAL_DEVICE) {
                out->nh_ = arp_nh->id();
                out->intf_ = arp_nh->GetInterface();
            }
        } else {
            out->intf_ = arp_nh->GetInterface();
        }
        out->vrf_ = arp_nh->GetVrf();
        break;
    }

        // RESOLVE Nexthop means traffic came from gateway interface and destined
        // to another gateway interface
    case NextHop::RESOLVE: {
        assert(info->l3_flow == true);
        const ResolveNH *rsl_nh = static_cast<const ResolveNH *>(nh);
        out->nh_ = rsl_nh->get_interface()->flow_key_nh()->id();
        out->intf_ = rsl_nh->get_interface();
        break;
    }

    default:
        out->intf_ = NULL;
        break;
    }

    if (out->intf_) {
        if (!out->intf_->IsActive()) {
            out->intf_ = NULL;
            ret = false;
        } else if (force_vmport && IsVgwOrVmInterface(out->intf_) == false) {
            out->intf_ = NULL;
            out->vrf_ = NULL;
            ret = true;
        }
    }

    if (out->vrf_ && (out->vrf_->IsActive() == false)) {
        out->vrf_ = NULL;
        ret = false;
    }

    return ret;
}

// Decode route and get Interface / ECMP information
static bool RouteToOutInfo(const Agent *agent, const AgentRoute *rt,
               const PktInfo *pkt, PktFlowInfo *info,
               PktControlInfo *in, PktControlInfo *out) {
    const AgentPath *path = rt->GetActivePath();
    if (path == NULL)
        return false;

    const NextHop *nh = static_cast<const NextHop *>
        (path->ComputeNextHop(info->agent));
    if (nh == NULL)
        return false;

    if (nh->IsActive() == false) {
        return false;
    }

    return NhDecode(agent, nh, pkt, info, in, out, false,
                    path->ecmp_load_balance());
}

static const VnEntry *InterfaceToVn(const Interface *intf) {
    if (intf->type() != Interface::VM_INTERFACE)
        return NULL;

    const VmInterface *vm_port = static_cast<const VmInterface *>(intf);
    return vm_port->vn();
}

static bool IntfHasFloatingIp(PktFlowInfo *pkt_info, const Interface *intf,
                              Address::Family family) {
    if (!intf || intf->type() != Interface::VM_INTERFACE)
        return false;

    return static_cast<const VmInterface *>(intf)->HasFloatingIp(family);
}

static bool IsLinkLocalRoute(Agent *agent, const AgentRoute *rt,
                             uint32_t sport, uint32_t dport) {
    //Local CN and BGP has been allowed for testing purpose.
    if ((sport == BgpAsAService::DefaultBgpPort) ||
        (dport == BgpAsAService::DefaultBgpPort))
        return false;

    const AgentPath *path = rt->GetActivePath();
    if (path && path->peer() == agent->link_local_peer())
        return true;

    return false;
}

bool PktFlowInfo::IsBgpRouterServiceRoute(const AgentRoute *in_rt,
                                          const AgentRoute *out_rt,
                                          const Interface *intf,
                                          uint32_t sport,
                                          uint32_t dport) {
    if (bgp_router_service_flow)
        return true;

    if (intf == NULL || in_rt == NULL || out_rt == NULL)
        return false;

    if ((sport != BgpAsAService::DefaultBgpPort) &&
        (dport != BgpAsAService::DefaultBgpPort))
        return false;

    if (intf->type() == Interface::VM_INTERFACE) {
        const VmInterface *vm_intf =
            dynamic_cast<const VmInterface *>(intf);
        const InetUnicastRouteEntry *in_inet_rt =
            dynamic_cast<const InetUnicastRouteEntry *>(in_rt);
        const InetUnicastRouteEntry *out_inet_rt =
            dynamic_cast<const InetUnicastRouteEntry *>(out_rt);
        if (in_inet_rt == NULL || out_inet_rt == NULL)
            return false;
        if (agent->oper_db()->bgp_as_a_service()->
            IsBgpService(vm_intf, in_inet_rt->addr(), out_inet_rt->addr())) {
            bgp_router_service_flow = true;
            return true;
        }
    }

    return false;
}

static const VnListType *RouteToVn(const AgentRoute *rt) {
    const AgentPath *path = NULL;
    if (rt) {
        path = rt->GetActivePath();
    }
    if (path == NULL) {
        return &(Agent::NullStringList());
    }

    return &path->dest_vn_list();
}

bool PktFlowInfo::RouteAllowNatLookupCommon(const AgentRoute *rt,
                                            uint32_t sport,
                                            uint32_t dport,
                                            const Interface *intf) {
    // No NAT for bridge routes
    if (dynamic_cast<const BridgeRouteEntry *>(rt) != NULL)
        return false;

    if (rt != NULL && IsLinkLocalRoute(agent, rt, sport, dport)) {
        // skip NAT lookup if found route has link local peer.
        return false;
    }

    return true;
}

bool PktFlowInfo::IngressRouteAllowNatLookup(const AgentRoute *in_rt,
                                             const AgentRoute *out_rt,
                                             uint32_t sport,
                                             uint32_t dport,
                                             const Interface *intf) {
    if (RouteAllowNatLookupCommon(out_rt, sport, dport, intf) == false) {
        return false;
    }

    if (IsBgpRouterServiceRoute(in_rt, out_rt, intf, sport, dport)) {
        // skip NAT lookup if found route has link local peer.
        return false;
    }

    return true;
}

bool PktFlowInfo::EgressRouteAllowNatLookup(const AgentRoute *in_rt,
                                            const AgentRoute *out_rt,
                                            uint32_t sport,
                                            uint32_t dport,
                                            const Interface *intf) {
    if (RouteAllowNatLookupCommon(out_rt, sport, dport, intf) == false) {
        return false;
    }

    return true;
}

void PktFlowInfo::CheckLinkLocal(const PktInfo *pkt) {
    if (!l3_flow && pkt->ip_daddr.is_v4()) {
        uint16_t nat_port;
        Ip4Address nat_server;
        std::string service_name;
        GlobalVrouter *global_vrouter = agent->oper_db()->global_vrouter();
        if (global_vrouter->FindLinkLocalService(pkt->ip_daddr.to_v4(),
                                                 pkt->dport, &service_name,
                                                 &nat_server, &nat_port)) {
            // it is link local service request, treat it as l3
            l3_flow = true;
        }
    }
}

void PktFlowInfo::LinkLocalServiceFromVm(const PktInfo *pkt, PktControlInfo *in,
                                         PktControlInfo *out) {

    // Link local services supported only for IPv4 for now
    if (pkt->family != Address::INET) {
        in->rt_ = NULL;
        out->rt_ = NULL;
        return;
    }

    const VmInterface *vm_port =
        static_cast<const VmInterface *>(in->intf_);

    uint16_t nat_port;
    Ip4Address nat_server;
    std::string service_name;
    if (!agent->oper_db()->global_vrouter()->FindLinkLocalService
        (pkt->ip_daddr.to_v4(), pkt->dport, &service_name, &nat_server,
         &nat_port)) {
        // link local service not configured, drop the request
        in->rt_ = NULL;
        out->rt_ = NULL;
        return;
    }

    out->vrf_ = agent->vrf_table()->FindVrfFromName(agent->fabric_vrf_name());
    dest_vrf = out->vrf_->vrf_id();

    // Set NAT flow fields
    linklocal_flow = true;
    nat_done = true;
    underlay_flow = false;
    if (nat_server == agent->router_id()) {
        // In case of metadata or when link local destination is local host,
        // set VM's metadata address as NAT source address. This is required
        // to avoid response from the linklocal service being looped back and
        // the packet not coming to vrouter for reverse NAT.
        // Destination would be local host (FindLinkLocalService returns this)
        nat_ip_saddr = vm_port->mdata_ip_addr();
        // Services such as metadata will run on compute_node_ip. Set nat
        // address to compute_node_ip
        nat_server = agent->compute_node_ip();
        nat_sport = pkt->sport;
    } else {
        nat_ip_saddr = agent->router_id();
        // we bind to a local port & use it as NAT source port (cannot use
        // incoming src port); init here and bind in Add;
        nat_sport = 0;
        linklocal_bind_local_port = true;
    }
    nat_ip_daddr = nat_server;
    nat_dport = nat_port;

    nat_vrf = dest_vrf;
    nat_dest_vrf = vm_port->vrf_id();

    out->rt_ = FlowEntry::GetUcRoute(out->vrf_, nat_server);
    return;
}

void PktFlowInfo::LinkLocalServiceFromHost(const PktInfo *pkt, PktControlInfo *in,
                                           PktControlInfo *out) {
    if (RouteToOutInfo(agent, out->rt_, pkt, this, in, out) == false) {
        return;
    }

    // Link local services supported only for IPv4 for now
    if (pkt->family != Address::INET) {
        in->rt_ = NULL;
        out->rt_ = NULL;
        return;
    }

    const VmInterface *vm_port =
        static_cast<const VmInterface *>(out->intf_);
    if (vm_port == NULL) {
        // Force implicit deny
        in->rt_ = NULL;
        out->rt_ = NULL;
        return;
    }

    // Check if packet is destined to metadata of interface
    MetaDataIp *mip = vm_port->GetMetaDataIp(pkt->ip_daddr.to_v4());
    if (mip == NULL) {
        // Force implicit deny
        in->rt_ = NULL;
        out->rt_ = NULL;
        return;
    }

    dest_vrf = vm_port->vrf_id();
    out->vrf_ = vm_port->vrf();

    //If the destination route is ECMP set component index
    //This component index would be used only for forwarding
    //the first packet in flow (HOLD flow flushing)
    InetUnicastRouteEntry *out_rt = NULL;
    if (out->vrf_) {
        out_rt = static_cast<InetUnicastRouteEntry *>(
                     FlowEntry::GetUcRoute(out->vrf_, mip->destination_ip()));
        if (out_rt &&
            out_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE) {
            const CompositeNH *comp_nh =
                static_cast<const CompositeNH *>(out_rt->GetActiveNextHop());
            ComponentNH component_nh(vm_port->label(), vm_port->flow_key_nh());
            comp_nh->GetIndex(component_nh, out_component_nh_idx);
        }
    }

    linklocal_flow = true;
    nat_done = true;
    underlay_flow = false;
    // Get NAT source/destination IP from MetadataIP retrieved from interface
    nat_ip_saddr = mip->service_ip();
    nat_ip_daddr = mip->destination_ip();
    if (nat_ip_saddr == IpAddress(kDefaultIpv4) ||
        nat_ip_saddr == IpAddress(kDefaultIpv6) ||
        nat_ip_daddr == IpAddress(kDefaultIpv4) ||
        nat_ip_daddr == IpAddress(kDefaultIpv6)) {
        // Failed to find associated source or destination address
        // Force implicit deny
        in->rt_ = NULL;
        out->rt_ = NULL;
        return;
    }

    nat_dport = pkt->dport;
    if (pkt->sport == agent->metadata_server_port()) {
        nat_sport = METADATA_NAT_PORT;
    } else {
        nat_sport = pkt->sport;
    }
    nat_vrf = dest_vrf;
    nat_dest_vrf = pkt->vrf;
    return;
}

void PktFlowInfo::LinkLocalServiceTranslate(const PktInfo *pkt, PktControlInfo *in,
                                            PktControlInfo *out) {
    const VmInterface *vm_intf = dynamic_cast<const VmInterface *>(in->intf_);
    if (vm_intf->vmi_type() != VmInterface::VHOST) {
        LinkLocalServiceFromVm(pkt, in, out);
    } else {
        LinkLocalServiceFromHost(pkt, in, out);
    }
}

void PktFlowInfo::BgpRouterServiceFromVm(const PktInfo *pkt, PktControlInfo *in,
                                         PktControlInfo *out) {

    // Link local services supported only for IPv4 for now
    if (pkt->family != Address::INET) {
        in->rt_ = NULL;
        out->rt_ = NULL;
        return;
    }

    const VmInterface *vm_port =
        static_cast<const VmInterface *>(in->intf_);

    const VnEntry *vn = static_cast<const VnEntry *>(vm_port->vn());
    uint32_t sport = 0;
    uint32_t dport = 0;
    IpAddress nat_server = IpAddress();

    if (vn == NULL) {
        in->rt_ = NULL;
        out->rt_ = NULL;
        return;
    }

    if (agent->oper_db()->bgp_as_a_service()->
        GetBgpRouterServiceDestination(vm_port,
                                       pkt->ip_saddr.to_v4(),
                                       pkt->ip_daddr.to_v4(),
                                       &nat_server,
                                       &sport, &dport) == false) {
        return;
    }

    out->vrf_ = agent->vrf_table()->FindVrfFromName(agent->fabric_vrf_name());
    dest_vrf = out->vrf_->vrf_id();

    nat_done = true;
    //Populate NAT
    nat_ip_saddr = agent->router_id();
    nat_ip_daddr = nat_server;
    nat_sport = sport;
    nat_dport = dport;
    if ((nat_ip_daddr == agent->router_id()) &&
        (nat_ip_daddr == nat_ip_saddr)) {
        boost::system::error_code ec;
        //TODO may be use MDATA well known address.
        nat_ip_saddr = vm_port->mdata_ip_addr();
    }

    nat_vrf = dest_vrf;
    nat_dest_vrf = vm_port->vrf_id();


    out->rt_ = FlowEntry::GetUcRoute(out->vrf_, nat_server);
    out->intf_ = agent->vhost_interface();
    out->nh_ = out->intf_->flow_key_nh()->id();
    ttl = pkt->ttl;
    return;
}

void PktFlowInfo::BgpRouterServiceTranslate(const PktInfo *pkt,
                                            PktControlInfo *in,
                                            PktControlInfo *out) {
    if (in->intf_->type() == Interface::VM_INTERFACE) {
        BgpRouterServiceFromVm(pkt, in, out);
    }
}

// DestNAT for packets entering into a VM with floating-ip.
// Can come here in two paths,
// - Packet originated on local vm.
// - Packet originated from remote vm
void PktFlowInfo::FloatingIpDNat(const PktInfo *pkt, PktControlInfo *in,
                                 PktControlInfo *out) {
    const VmInterface *vm_port =
        static_cast<const VmInterface *>(out->intf_);
    const VmInterface::FloatingIpSet &fip_list =
        vm_port->floating_ip_list().list_;

    // We must NAT if the IP-DA is not same as Primary-IP on interface
    if (pkt->ip_daddr.to_v4() == vm_port->primary_ip_addr()) {
        return;
    }

    // Look for matching floating-ip
    VmInterface::FloatingIpSet::const_iterator it = fip_list.begin();
    for ( ; it != fip_list.end(); ++it) {

        if (it->vrf_.get() == NULL) {
            continue;
        }

        if (pkt->ip_daddr.to_v4() != it->floating_ip_) {
            continue;
        }

        // Check if floating-ip direction matches
        if (it->AllowDNat() == false) {
            continue;
        }

        break;
    }

    if (it == fip_list.end()) {
        // No matching floating ip for destination-ip
        return;
    }
    in->vn_ = NULL;
    if (nat_done == false) {
        // lookup for source route in FIP VRF for egress flows only
        // because for source route VRF is always present for ingress flows
        // so there is no need to update source route with route lookup
        // in FIP's VRF
        if(!ingress) {
            UpdateRoute(&in->rt_, it->vrf_.get(), pkt->ip_saddr, pkt->smac,
                    flow_source_plen_map);
            nat_dest_vrf = it->vrf_.get()->vrf_id();
        }
    }
    UpdateRoute(&out->rt_, it->vrf_.get(), pkt->ip_daddr, pkt->dmac,
                flow_dest_plen_map);
    out->vn_ = it->vn_.get();
    VrfEntry *alias_vrf = vm_port->GetAliasIpVrf(it->GetFixedIp(vm_port));
    if (alias_vrf == NULL) {
        dest_vrf = out->intf_->vrf()->vrf_id();
    } else {
        dest_vrf = alias_vrf->vrf_id();
    }

    underlay_flow = false;
    if (VrfTranslate(pkt, in, out, pkt->ip_saddr, true) == false) {
        return;
    }

    if (underlay_flow) {
        if (it->vrf_->forwarding_vrf()) {
            //Pick the underlay ip-fabric VRF for forwarding
            nat_dest_vrf = it->vrf_->forwarding_vrf()->vrf_id();
        }
        if (out->intf_->vrf()->forwarding_vrf()) {
            dest_vrf = out->intf_->vrf()->forwarding_vrf()->vrf_id();
        }
    }

    // Force packet to be treated as L3-flow in such case
    // Flow is already marked as l3-flow by the time we are here. But, there is
    // an exception in case of DNat.
    // - In normal cases, packet hits bridge entry with receive-nh
    // - If native-vrf and floating-ip vrf are same, the bridge entry points
    //   to interface-nh instead of receive nh.
    l3_flow = true;
    // Translate the Dest-IP
    if (nat_done == false)
        nat_ip_saddr = pkt->ip_saddr;
    nat_ip_daddr = it->fixed_ip_;
    if (it->port_map_enabled()) {
        int32_t map_port = it->GetDstPortMap(pkt->ip_proto, pkt->dport);
        if (map_port < 0) {
            short_flow = true;
            short_flow_reason = FlowEntry::SHORT_PORT_MAP_DROP;
        } else {
            nat_dport = map_port;
        }
    } else {
        nat_dport = pkt->dport;
    }
    nat_sport = pkt->sport;
    nat_vrf = dest_vrf;
    nat_done = true;

    if (in->rt_) {
        flow_source_vrf = static_cast<const AgentRoute *>(in->rt_)->vrf_id();
    } else {
        flow_source_vrf = VrfEntry::kInvalidIndex;
    }
    flow_dest_vrf = it->vrf_.get()->vrf_id();

    // Update fields required for floating-IP stats accounting
    fip_dnat = true;

    return;
}

void PktFlowInfo::FloatingIpSNat(const PktInfo *pkt, PktControlInfo *in,
                                 PktControlInfo *out) {
    if (pkt->family == Address::INET6) {
        return;
        //TODO: V6 FIP
    }
    const VmInterface *intf =
        static_cast<const VmInterface *>(in->intf_);
    const VmInterface::FloatingIpSet &fip_list = intf->floating_ip_list().list_;
    VmInterface::FloatingIpSet::const_iterator it = fip_list.begin();
    VmInterface::FloatingIpSet::const_iterator fip_it = fip_list.end();
    const AgentRoute *rt = out->rt_;
    uint8_t rt_plen = 0;
    if (rt) {
        rt_plen = RouteToPrefixLen(rt);
    }
    bool change = false;
    // Find Floating-IP matching destination-ip
    for ( ; it != fip_list.end(); ++it) {
        if (it->vrf_.get() == NULL) {
            continue;
        }

        if (it->fixed_ip_ != Ip4Address(0) && (pkt->ip_saddr != it->fixed_ip_))  {
            continue;
        }

        // Check if floating-ip direction matches
        if (it->AllowSNat() == false) {
            continue;
        }

        const AgentRoute *rt_match = FlowEntry::GetUcRoute(it->vrf_.get(),
                pkt->ip_daddr);
        if (rt_match == NULL) {
            flow_dest_plen_map[it->vrf_.get()->vrf_id()] = 0;
            continue;
        }
        // found the route match
        // prefer the route with longest prefix match
        // if prefix length is same prefer route from rt(original out->rt_)
        // if routes are from fip of difference VRF, prefer the one with lower name.
        // if both the selected and current FIP is from same vrf prefer the one with lower ip addr.
        uint8_t rt_match_plen = RouteToPrefixLen(rt_match);
        if (rt != NULL && rt_plen >= rt_match_plen) {
            flow_dest_plen_map[rt_match->vrf_id()] = rt_match_plen;
            continue;
        }
        uint8_t out_rt_plen = RouteToPrefixLen(out->rt_);
        if (out->rt_ == NULL || rt_match_plen > out_rt_plen) {
            change = true;
        } else if (rt_match_plen == out_rt_plen) {
            if (it->port_nat()) {
               change = false;
            } else if (fip_it == fip_list.end()) {
                change = true;
            } else if (rt_match->vrf()->GetName() < out->rt_->vrf()->GetName()) {
                change = true;
            } else if (rt_match->vrf()->GetName() == out->rt_->vrf()->GetName() &&
                    it->floating_ip_ < fip_it->floating_ip_) {
                change = true;
            }
        }

        if (change) {
            if (out->rt_ != NULL) {
                flow_dest_plen_map[out->rt_->vrf_id()] = out_rt_plen;
            }
            out->rt_ = rt_match;
            fip_it = it;
            change = false;
        } else {
            flow_dest_plen_map[rt_match->vrf_id()] = rt_match_plen;
        }
    }

    if (out->rt_ == rt) {
        // No change in route, no floating-ip found
        return;
    }

    //Populate in->vn, used for VRF translate ACL lookup
    in->vn_ = fip_it->vn_.get();

    // Floating-ip found. We will change src-ip to floating-ip. Recompute route
    // for new source-ip. All policy decisions will be based on this new route
    UpdateRoute(&in->rt_, fip_it->vrf_.get(), fip_it->floating_ip_, pkt->smac,
                flow_source_plen_map);
    if (in->rt_ == NULL) {
        return;
    }

    underlay_flow = false;
    if (VrfTranslate(pkt, in, out, fip_it->floating_ip_, true) == false) {
        return;
    }
    if (out->rt_ == NULL || in->rt_ == NULL) {
        //If After VRF translation, ingress route or
        //egress route is NULL, mark the flow as short flow
        return;
    }

    // Compute out-intf and ECMP info from out-route
    if (RouteToOutInfo(agent, out->rt_, pkt, this, in, out) == false) {
        return;
    }

    dest_vrf = out->rt_->vrf_id();
    // Setup reverse flow to translate sip.
    nat_done = true;
    nat_ip_saddr = fip_it->floating_ip_;
    nat_ip_daddr = pkt->ip_daddr;
    if (fip_it->port_map_enabled()) {
        int32_t map_port = fip_it->GetSrcPortMap(pkt->ip_proto, pkt->sport);
        if (map_port < 0) {
            short_flow = true;
            short_flow_reason = FlowEntry::SHORT_PORT_MAP_DROP;
        } else {
            nat_sport = map_port;
        }
    } else if (fip_it->port_nat()) {
        FlowKey key(in->nh_, pkt->ip_saddr, pkt->ip_daddr, pkt->ip_proto,
                    pkt->sport, pkt->dport);
        if (fip_it->floating_ip_ == pkt->ip_daddr) {
            nat_sport = pkt->sport;
            nat_ip_saddr = intf->mdata_ip_addr();
        } else {
            nat_sport =
                agent->GetFlowProto()->port_table_manager()->Allocate(key);
            if (nat_sport == 0) {
                short_flow = true;
                short_flow_reason = FlowEntry::SHORT_PORT_MAP_DROP;
                nat_done = false;
                out->nh_ = in->nh_;
                return;
            } else {
                port_allocated = true;
            }
        }
        out->nh_ = agent->vhost_interface()->flow_key_nh()->id();
    } else {
        nat_sport = pkt->sport;
    }
    nat_dport = pkt->dport;

    // Compute VRF for reverse flow
    if (out->intf_) {
        // Egress-vm present on same compute node, take VRF from vm-port
        nat_vrf = out->vrf_->vrf_id();
        out->vn_ = InterfaceToVn(out->intf_);
    } else {
        // Egress-vm is remote. Find VRF from the NH for source-ip
        nat_vrf = NhToVrf(in->rt_->GetActiveNextHop());
    }

    // Dest VRF for reverse flow is In-Port VRF
    nat_dest_vrf = in->vrf_->vrf_id();

    flow_source_vrf = pkt->vrf;
    if (out->rt_) {
        flow_dest_vrf = dest_vrf;
    } else {
        flow_dest_vrf = VrfEntry::kInvalidIndex;
    }
    // Update fields required for floating-IP stats accounting
    snat_fip = nat_ip_saddr;
    fip_snat = true;
    return;
}

//Check if both source and destination route support Native Encap
//if yes use underlay forwarding (no change)
//Else Do a VRF translate to interface VRF
void PktFlowInfo::ChangeEncapToOverlay(const VmInterface *intf,
                                       const PktInfo *pkt,
                                       PktControlInfo *in,
                                       PktControlInfo *out) {

    if (l3_flow == false) {
        return;
    }

    bool can_be_underlay_flow = false;
    if (intf->vrf() && intf->vrf()->forwarding_vrf() &&
        intf->vrf()->forwarding_vrf() != intf->vrf()) {
        can_be_underlay_flow = true;
    }

    //Route needs to be check because out interface might
    //not be populated, if destination uses native forwarding
    //then also we need to do vrf translate
    if (out->rt_) {
        const InterfaceNH *intf_nh =
            dynamic_cast<const InterfaceNH *>(out->rt_->GetActiveNextHop());
        if (intf_nh) {
            const Interface *out_itf = intf_nh->GetInterface();
            if (out_itf->vrf() && out_itf->vrf()->forwarding_vrf() &&
                out_itf->vrf()->forwarding_vrf() != out_itf->vrf()) {
                can_be_underlay_flow = true;
            }
        }
    }

    if (can_be_underlay_flow == false) {
        return;
    }

    const AgentRoute *src_rt = FlowEntry::GetUcRoute(intf->vrf(),
                                                     pkt->ip_saddr);
    const AgentRoute *dst_rt = FlowEntry::GetUcRoute(intf->vrf(),
                                                     pkt->ip_daddr);

    if (src_rt == NULL || dst_rt == NULL) {
        overlay_route_not_found = true;
        return;
    }

    overlay_route_not_found = false;
    uint32_t src_tunnel_bmap = src_rt->GetActivePath()->tunnel_bmap();
    uint32_t dst_tunnel_bmap = dst_rt->GetActivePath()->tunnel_bmap();

     if ((src_tunnel_bmap & (1 << TunnelType::NATIVE)) &&
         (dst_tunnel_bmap & (1 << TunnelType::NATIVE))) {
         underlay_flow = true;
         //Set policy VRF for route tracking
         src_policy_vrf = intf->vrf()->vrf_id();
         dst_policy_vrf = intf->vrf()->vrf_id();
         src_vn = RouteToVn(src_rt);
         dst_vn = RouteToVn(dst_rt);
         return;
     }

     const VrfEntry *vrf = intf->vrf();
     ChangeVrf(pkt, out, vrf);
     dest_vrf = vrf->vrf_id();
     alias_ip_flow = true;
     UpdateRoute(&out->rt_, vrf, pkt->ip_daddr, pkt->dmac,
             flow_dest_plen_map);
     UpdateRoute(&in->rt_, vrf, pkt->ip_saddr, pkt->smac,
             flow_source_plen_map);
}

void PktFlowInfo::ChangeFloatingIpEncap(const PktInfo *pkt,
                                        PktControlInfo *in,
                                        PktControlInfo *out) {
     if (in->rt_ == NULL || out->rt_ == NULL) {
         return;
     }

     overlay_route_not_found = false;
     const InetUnicastRouteEntry *src =
         static_cast<const InetUnicastRouteEntry *>(in->rt_);
     const IpAddress src_ip = src->addr();

     const InetUnicastRouteEntry *dst =
         static_cast<const InetUnicastRouteEntry *>(out->rt_);
     const IpAddress dst_ip = dst->addr();

     uint32_t src_tunnel_bmap = in->rt_->GetActivePath()->tunnel_bmap();
     uint32_t dst_tunnel_bmap = out->rt_->GetActivePath()->tunnel_bmap();

     if ((src_tunnel_bmap & (1 << TunnelType::NATIVE)) &&
         (dst_tunnel_bmap & (1 << TunnelType::NATIVE))) {

         underlay_flow = true;
         src_vn = RouteToVn(in->rt_);
         dst_vn = RouteToVn(out->rt_);

         if (nat_done == false) {
             src_policy_vrf = in->rt_->vrf()->vrf_id();
         }
         dst_policy_vrf = out->rt_->vrf()->vrf_id();
         const VrfEntry *vrf = agent->fabric_vrf();
         ChangeVrf(pkt, out, vrf);
         UpdateRoute(&out->rt_, vrf, dst_ip, pkt->dmac,
                 flow_dest_plen_map);
         UpdateRoute(&in->rt_, vrf, src_ip, pkt->smac,
                 flow_source_plen_map);
     }
}

void PktFlowInfo::ChangeEncap(const VmInterface *intf, const PktInfo *pkt,
                              PktControlInfo *in, PktControlInfo *out,
                              bool nat_flow) {
    if (nat_flow) {
        ChangeFloatingIpEncap(pkt, in, out);
    } else {
        ChangeEncapToOverlay(intf, pkt, in, out);
    }
}

bool PktFlowInfo::VrfTranslate(const PktInfo *pkt, PktControlInfo *in,
                               PktControlInfo *out, const IpAddress &src_ip,
                               bool nat_flow) {
    const Interface *intf = NULL;
    if (ingress) {
        intf = in->intf_;
    } else {
        intf = out->intf_;
    }
    if (!intf || intf->type() != Interface::VM_INTERFACE) {
        return true;
    }

    const VmInterface *vm_intf = static_cast<const VmInterface *>(intf);
    //If interface has a VRF assign rule, choose the acl and match the
    //packet, else get the acl attached to VN and try matching the packet to
    //network acl

    ChangeEncap(vm_intf, pkt, in, out, nat_flow);

    const AclDBEntry *acl = NULL;
    if (nat_flow == false) {
        acl = vm_intf->vrf_assign_acl();
    }
    //In case of floating IP translation, dont apply
    //interface VRF assign rule
    if (acl == NULL) {
        if (ingress && in->vn_) {
            //Check if the network ACL is present
            acl = in->vn_->GetAcl();
        } else if (out->vn_) {
            acl = out->vn_->GetAcl();
        }
    }

    if (!acl) {
        return true;
    }

    PacketHeader hdr;
    hdr.vrf = pkt->vrf;
    hdr.src_ip = src_ip;
    hdr.dst_ip = pkt->ip_daddr;

    hdr.protocol = pkt->ip_proto;
    if (hdr.protocol == IPPROTO_UDP || hdr.protocol == IPPROTO_TCP) {
        hdr.src_port = pkt->sport;
        hdr.dst_port = pkt->dport;
    } else {
        hdr.src_port = 0;
        hdr.dst_port = 0;
    }
    hdr.src_policy_id = RouteToVn(in->rt_);
    hdr.dst_policy_id = RouteToVn(out->rt_);

    if (underlay_flow) {
        hdr.src_policy_id = src_vn;
        hdr.dst_policy_id = dst_vn;
    }

    if (in->rt_) {
        const AgentPath *path = in->rt_->GetActivePath();
        hdr.src_sg_id_l = &(path->sg_list());
    }
    if (out->rt_) {
        const AgentPath *path = out->rt_->GetActivePath();
        hdr.dst_sg_id_l = &(path->sg_list());
    }

    MatchAclParams match_acl_param;
    if (!acl->PacketMatch(hdr, match_acl_param, NULL)) {
        return true;
    }

    if (match_acl_param.action_info.vrf_translate_action_.vrf_name() != "") {
        VrfKey key(match_acl_param.action_info.vrf_translate_action_.vrf_name());
        const VrfEntry *vrf = static_cast<const VrfEntry*>
            (agent->vrf_table()->FindActiveEntry(&key));
        if (vrf == NULL) {
            short_flow = true;
            short_flow_reason = FlowEntry::SHORT_UNAVIALABLE_VRF;
            in->rt_ = NULL;
            out->rt_ = NULL;
            return false;
        }

        ChangeVrf(pkt, out, vrf);
        UpdateRoute(&out->rt_, vrf, pkt->ip_daddr, pkt->dmac,
                    flow_dest_plen_map);
        UpdateRoute(&in->rt_, vrf, hdr.src_ip, pkt->smac,
                    flow_source_plen_map);
        underlay_flow = false;
    }
    return true;
}

void PktFlowInfo::IngressProcess(const PktInfo *pkt, PktControlInfo *in,
                                 PktControlInfo *out) {
    // Flow packets are expected only on VMPort interfaces
    if (in->intf_->type() != Interface::VM_INTERFACE &&
        in->intf_->type() != Interface::INET) {
        LogError(pkt, this, "Unexpected packet on Non-VM interface");
        return;
    }

    const VmInterface *vm_port =
        dynamic_cast<const VmInterface *>(in->intf_);
    if (vm_port != NULL) {
        VrfEntry *alias_vrf = vm_port->GetAliasIpVrf(pkt->ip_saddr);
        if (alias_vrf != NULL) {
            in->vrf_ = alias_vrf;
            // translate to alias ip vrf for destination, unless overriden by
            // translation due to NAT or ACL
            dest_vrf = alias_vrf->vrf_id();
            alias_ip_flow = true;
        }
    }

    // We always expect route for source-ip for ingress flows.
    // If route not present, return from here so that a short flow is added
    UpdateRoute(&in->rt_, in->vrf_, pkt->ip_saddr, pkt->smac,
                flow_source_plen_map);
    in->vn_ = InterfaceToVn(in->intf_);

    // Consider linklocal service requests as l3 always
    CheckLinkLocal(pkt);

    // Compute Out-VRF and Route for dest-ip
    out->vrf_ = in->vrf_;
    UpdateRoute(&out->rt_, out->vrf_, pkt->ip_daddr, pkt->dmac,
                flow_dest_plen_map);
    //Native VRF of the interface and acl assigned vrf would have
    //exact same route with different nexthop, hence if both ingress
    //route and egress route are present in native vrf, acl match condition
    //can be applied
    if (VrfTranslate(pkt, in, out, pkt->ip_saddr, false) == false) {
        return;
    }

    if (out->rt_) {
        // Compute out-intf and ECMP info from out-route
        if (RouteToOutInfo(agent, out->rt_, pkt, this, in, out)) {
            if (out->intf_) {
                out->vn_ = InterfaceToVn(out->intf_);
                //In case of alias IP destination VRF would
                //be already while NHDecode or if its underlay
                //to overlay transition then encap change takes
                //care of it.
                //In case of VM using ip-fabric for forwarding
                //NH would be set with VRF as ip-fabric which
                //would mean local IPV6 traffic would be forwarded
                //in ip-fabric VRF which is not needed
                if (out->vrf_ && alias_ip_flow == false) {
                    dest_vrf = out->vrf_->vrf_id();
                }
            }
        }
    }

    if (IngressRouteAllowNatLookup(in->rt_,
                                   out->rt_,
                                   pkt->sport,
                                   pkt->dport,
                                   in->intf_)) {
        // If interface has floating IP, check if we have more specific route in
        // public VN (floating IP)
        if (l3_flow && IntfHasFloatingIp(this, in->intf_, pkt->family)) {
            FloatingIpSNat(pkt, in, out);
        }
    }

    if (out->rt_ != NULL) {
        // Route is present. If IP-DA is a floating-ip, we need DNAT
        if (RouteToOutInfo(agent, out->rt_, pkt, this, in, out)) {
            if (out->intf_ && IntfHasFloatingIp(this, out->intf_, pkt->family)) {
                FloatingIpDNat(pkt, in, out);
            }
        }
    }

    // Packets needing linklocal service will have route added by LinkLocal peer
    if ((in->rt_ && IsLinkLocalRoute(agent, in->rt_, pkt->sport, pkt->dport)) ||
        (out->rt_ && IsLinkLocalRoute(agent, out->rt_,
                                      pkt->sport, pkt->dport))) {
        LinkLocalServiceTranslate(pkt, in, out);
    }

    //Packets needing bgp router service handling
    if (IsBgpRouterServiceRoute(in->rt_, out->rt_,
                                in->intf_, pkt->sport,
                                pkt->dport)) {
        BgpRouterServiceTranslate(pkt, in, out);
    }

    // If out-interface was not found, get it based on out-route
    if (out->intf_ == NULL && out->rt_) {
        RouteToOutInfo(agent, out->rt_, pkt, this, in, out);
    }
    if (out->rt_) {
        const NextHop* nh = out->rt_->GetActiveNextHop();
        if (nh && nh->GetType() == NextHop::COMPOSITE) {
            const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
            nh = comp_nh->GetNH(out_component_nh_idx);
        }

        if (nh && nh->GetType() == NextHop::TUNNEL) {
            const TunnelNH* tunnel_nh = static_cast<const TunnelNH *>(nh);
            const Ip4Address *ip = tunnel_nh->GetDip();
            if (ip) {
                peer_vrouter = ip->to_string();
                tunnel_type = tunnel_nh->GetTunnelType();
            }
        } else {
            peer_vrouter = agent->router_id().to_string();
        }
    }

    //In case of distributed SNAT we dont want policy to be applied based
    //on translated FIP route. hence change ingress route to VM's actual
    //route and also the source policy VRF
    if (port_allocated && short_flow == false) {
        UpdateRoute(&in->rt_, in->vrf_, pkt->ip_saddr, pkt->smac,
                    flow_source_plen_map);
        in->vn_ = InterfaceToVn(in->intf_);
        src_policy_vrf = in->intf_->vrf()->vrf_id();
    }

    return;
}

const NextHop *PktFlowInfo::TunnelToNexthop(const PktInfo *pkt) {
    tunnel_type = pkt->tunnel.type;
    if (tunnel_type.GetType() == TunnelType::MPLS_GRE ||
        tunnel_type.GetType() == TunnelType::MPLS_UDP) {
        MplsLabel *mpls = agent->mpls_table()->FindMplsLabel(pkt->tunnel.label);
        if (mpls == NULL) {
            LogError(pkt, this, "Invalid Label in egress flow");
            return NULL;
        }
        return mpls->nexthop();
    } else if (tunnel_type.GetType() == TunnelType::VXLAN) {
        VxLanTable *table = static_cast<VxLanTable *>(agent->vxlan_table());
        VxLanId *vxlan = table->FindNoLock(pkt->tunnel.vxlan_id);
        if (vxlan == NULL) {
            LogError(pkt, this, "Invalid vxlan in egress flow");
            return NULL;
        }

        const VrfNH *nh = dynamic_cast<const VrfNH *>(vxlan->nexthop());
        if (nh == NULL)
            return NULL;

        const VrfEntry *vrf = nh->GetVrf();
        if (vrf == NULL)
            return NULL;

        AgentRoute *rt = NULL;
        if (vrf->vn()->vxlan_routing_vn()) {
            rt = FlowEntry::GetUcRoute(vrf, pkt->ip_daddr);
        } else {
            // In case of VXLAN, the NH points to VrfNH. Need to do route lookup
            // on dmac to find the real nexthop
            rt = FlowEntry::GetL2Route(vrf, pkt->dmac);
        }
        if (rt != NULL) {
            return rt->GetActiveNextHop();
        }

        return NULL;
    } else {
        AgentRoute *rt = FlowEntry::GetUcRoute(agent->fabric_vrf(),
                                               pkt->ip_daddr);
        if (rt != NULL) {
            return rt->GetActiveNextHop();
        }


        LogError(pkt, this, "Invalid tunnel type in egress flow");
        return NULL;
    }

    return NULL;
}

void PktFlowInfo::EgressProcess(const PktInfo *pkt, PktControlInfo *in,
                                PktControlInfo *out) {
    peer_vrouter = Ip4Address(pkt->tunnel.ip_saddr).to_string();

    const NextHop *nh = TunnelToNexthop(pkt);
    if (nh == NULL) {
        return;
    }
    //Delay hash pick up till route is picked.
    if (NhDecode(agent, nh, pkt, this, in, out, true,
         EcmpLoadBalance()) == false) {
        return;
    }

    if (out->intf_ && out->intf_->type() == Interface::VM_INTERFACE) {
        const VmInterface *vm_intf = static_cast<const VmInterface *>(out->intf_);
        if (vm_intf->IsFloatingIp(pkt->ip_daddr)) {
            l3_flow = true;
        } else {
            VrfEntry *alias_vrf = vm_intf->GetAliasIpVrf(pkt->ip_daddr);
            if (alias_vrf != NULL) {
                out->vrf_ = alias_vrf;
                // translate to alias ip vrf for destination, unless overriden by
                // translation due to NAT or ACL
                dest_vrf = alias_vrf->vrf_id();
                alias_ip_flow = true;
            }
        }
    }

    if (out->vrf_ == NULL) {
        return;
    }

    UpdateRoute(&out->rt_, out->vrf_, pkt->ip_daddr, pkt->dmac,
                flow_dest_plen_map);
    UpdateRoute(&in->rt_, out->vrf_, pkt->ip_saddr, pkt->smac,
                flow_source_plen_map);

    if (out->intf_) {
        out->vn_ = InterfaceToVn(out->intf_);
    }

    //Apply vrf translate ACL to get ingress route
    if (VrfTranslate(pkt, in, out, pkt->ip_saddr, false) == false) {
        return;
    }

    if (EgressRouteAllowNatLookup(in->rt_,
                                  out->rt_,
                                  pkt->sport,
                                  pkt->dport,
                                  out->intf_)) {
        // If interface has floating IP, check if destination is one of the
        // configured floating IP.
        if (IntfHasFloatingIp(this, out->intf_, pkt->family)) {
            FloatingIpDNat(pkt, in, out);
        }
    }

    if (out->rt_) {
        if (ecmp && out->rt_->GetActivePath()) {
            const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
            out_component_nh_idx = comp_nh->hash(pkt->
                                   hash(agent, out->rt_->GetActivePath()->
                                        ecmp_load_balance()), ingress);
        }
        if (out->rt_->GetActiveNextHop()->GetType() == NextHop::ARP ||
            out->rt_->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
            //If a packet came with mpls label pointing to
            //vrf NH, then we need to do a route lookup
            //and set the nexthop for reverse flow properly
            //as mpls pointed NH would not be used for reverse flow
            if (RouteToOutInfo(agent, out->rt_, pkt, this, in, out)) {
                if (out->intf_) {
                    out->vn_ = InterfaceToVn(out->intf_);
                }
            }
        }
    }

    return;
}

bool PktFlowInfo::UnknownUnicastFlow(const PktInfo *pkt,
                                     const PktControlInfo *in,
                                     const PktControlInfo *out) {
    bool ret = false;
    if (ingress && out->rt_ == NULL && in->rt_) {
        const VmInterface *vm_intf =
            static_cast<const VmInterface *>(in->intf_);
        //If interface has flag to flood unknown unicast
        //and destination route is not present
        //mark the flow for forward
        if (vm_intf->flood_unknown_unicast()) {
            flood_unknown_unicast = true;
            flow_source_vrf = flow_dest_vrf =
                static_cast<const AgentRoute *>(in->rt_)->vrf_id();
            ret = true;
        }
    } else if (in->rt_ == NULL && out->rt_) {
        //This packet should not be ideally trapped
        //from vrouter for flow setup.
        //VxLAN nexthop would be set with flag
        //to flood multicast, hence we would
        //hit all broadcast multicast route and
        //packet would never be trapped for flow setup
        tunnel_type = pkt->tunnel.type;
        if (tunnel_type.GetType() == TunnelType::VXLAN) {
            VxLanTable *table = static_cast<VxLanTable *>(agent->vxlan_table());
            VxLanId *vxlan = table->FindNoLock(pkt->tunnel.vxlan_id);
            if (vxlan && vxlan->nexthop()) {
                const VrfNH *vrf_nh =
                    static_cast<const VrfNH *>(vxlan->nexthop());
                if (vrf_nh->flood_unknown_unicast()) {
                    flow_source_vrf = flow_dest_vrf =
                        static_cast<const AgentRoute *>(out->rt_)->vrf_id();
                    flood_unknown_unicast = true;
                    ret = true;
                }
            }
        }
    }
    return ret;
}

// Ignore in case of BFD health check
bool IsValidationDisabled(Agent *agent, const PktInfo *pkt,
                          const Interface *interface) {
    if (!interface)
        return false;
    return ((agent->pkt()->pkt_handler()->
             IsBFDHealthCheckPacket(pkt, interface)) ||
            (agent->pkt()->pkt_handler()->
             IsSegmentHealthCheckPacket(pkt, interface)));
}

// Basic config validations for the flow
bool PktFlowInfo::ValidateConfig(const PktInfo *pkt, PktControlInfo *in) {
    disable_validation = IsValidationDisabled(agent, pkt, in->intf_);

    if (agent->tsn_enabled()) {
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_FLOW_ON_TSN;
        return false;
    }

    if (in->intf_ == NULL) {
        LogError(pkt, this, "Invalid interface");
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_UNAVIALABLE_INTERFACE;
        return false;
    }

    const VmInterface *vm_intf = dynamic_cast<const VmInterface *>(in->intf_);
    if (l3_flow == true && !disable_validation) {
        if (vm_intf && in->intf_->ip_active(pkt->family) == false) {
            in->intf_ = NULL;
            LogError(pkt, this, "IP protocol inactive on interface");
            short_flow = true;
            short_flow_reason = FlowEntry::SHORT_UNAVIALABLE_INTERFACE;
            return false;
        }

        if (vm_intf && vm_intf->layer3_forwarding() == false) {
            LogError(pkt, this, "IP service not enabled for interface");
            short_flow = true;
            short_flow_reason = FlowEntry::SHORT_IPV4_FWD_DIS;
            return false;
        }
    }

    if (l3_flow == false && !disable_validation) {
        if (in->intf_->l2_active() == false) {
            in->intf_ = NULL;
            LogError(pkt, this, "L2 inactive on interface");
            short_flow = true;
            short_flow_reason = FlowEntry::SHORT_UNAVIALABLE_INTERFACE;
            return false;
        }

        if (vm_intf && vm_intf->bridging() == false) {
            LogError(pkt, this, "Bridge service not enabled for interface");
            short_flow = true;
            short_flow_reason = FlowEntry::SHORT_IPV4_FWD_DIS;
            return false;
        }
    }

    if (in->vrf_ == NULL || in->vrf_->IsActive() == false) {
        in->vrf_ = NULL;
        LogError(pkt, this, "Invalid or Inactive VRF");
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_UNAVIALABLE_VRF;
        return false;
    }

    return true;
}

bool PktFlowInfo::Process(const PktInfo *pkt, PktControlInfo *in,
                          PktControlInfo *out) {
    in->intf_ = agent->interface_table()->FindInterface(pkt->agent_hdr.ifindex);
    out->nh_ = in->nh_ = pkt->agent_hdr.nh;
    in->vrf_ = agent->vrf_table()->FindVrfFromId(pkt->agent_hdr.vrf);

    if (ValidateConfig(pkt, in) == false) {
        return false;
    }

    //By default assume destination vrf and source vrf to be same
    dest_vrf = pkt->vrf;
    // Compute direction of flow based on in-interface
    ingress = ComputeDirection(in->intf_);
    if (ingress) {
        IngressProcess(pkt, in, out);
    } else {
        EgressProcess(pkt, in, out);
    }

    if (l3_flow == false) {
        if (UnknownUnicastFlow(pkt, in, out) == true) {
            return true;
        }
    }

    if (nat_done && ((pkt->ignore_address == VmInterface::IGNORE_SOURCE) ||
        (pkt->ignore_address == VmInterface::IGNORE_DESTINATION) || (pkt->is_fat_flow_src_prefix) ||
        (pkt->is_fat_flow_dst_prefix))) {
        /* Fat flow not supported for NAT flows */
        LogError(pkt, this, "Flow : Fat-flow and NAT cannot co-exist");
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_FAT_FLOW_NAT_CONFLICT;
        return false;
    }

    if (!disable_validation) {
        if (in->rt_ == NULL || in->rt_->IsDeleted()) {
            LogError(pkt, this, "Flow : No route for Src-IP");
            short_flow = true;
            short_flow_reason = FlowEntry::SHORT_NO_SRC_ROUTE;
            return false;
        }

        if (out->rt_ == NULL || out->rt_->IsDeleted()) {
            LogError(pkt, this, "Flow : No route for Dst-IP");
            short_flow = true;
            short_flow_reason = FlowEntry::SHORT_NO_DST_ROUTE;
            return false;
        }

        flow_source_vrf = static_cast<const AgentRoute *>(in->rt_)->vrf_id();
        flow_dest_vrf = out->rt_->vrf_id();
    } else {
        flow_source_vrf = flow_dest_vrf = in->vrf_->vrf_id();
    }

    if (overlay_route_not_found) {
        LogError(pkt, this, "Flow : Overlay route not found");
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_NO_DST_ROUTE;
        return false;
    }

    //If source is ECMP, establish a reverse flow pointing
    //to the component index
    if (in->rt_ && in->rt_->GetActiveNextHop() &&
        in->rt_->GetActiveNextHop()->GetType() == NextHop::COMPOSITE) {
        ecmp = true;
    }

    if (out->rt_ && out->rt_->GetActiveNextHop() &&
        out->rt_->GetActiveNextHop()->GetType() == NextHop::COMPOSITE) {
        ecmp = true;
    }

    return true;
}

// A flow can mean that traffic is seen on an interface. The path preference
// module can potentially be interested in this event. Check and generate
// traffic seen event
void PktFlowInfo::GenerateTrafficSeen(const PktInfo *pkt,
                                      const PktControlInfo *in) {
    // Traffic seen should not be generated for MESSAGE
    if (pkt->type == PktType::MESSAGE) {
        return;
    }

    // Dont generate Traffic seen for egress flows or short or linklocal flows
    if (ingress == false || short_flow || linklocal_flow) {
        return;
    }

    // TODO : No need for one more route lookup
    const AgentRoute *rt = NULL;
    bool enqueue_traffic_seen = false;
    const VmInterface *vm_intf = dynamic_cast<const VmInterface *>(in->intf_);

    IpAddress sip = pkt->ip_saddr;
    if (pkt->family == Address::INET ||
        pkt->family == Address::INET6) {
        if (l3_flow) {
            rt = in->rt_;
        } else if (in->vrf_) {
            rt = FlowEntry::GetUcRoute(in->vrf_, sip);
        }
    }
    uint8_t plen = 0;
    // Generate event if route was waiting for traffic
    if (rt && rt->WaitForTraffic()) {
        enqueue_traffic_seen = true;
        plen = rt->plen();
    } else if (vm_intf) {
        //L3 route is not in wait for traffic state
        //EVPN route could be in wait for traffic, if yes
        //enqueue traffic seen
        rt = FlowEntry::GetEvpnRoute(in->vrf_, pkt->smac, sip,
                vm_intf->ethernet_tag());
        if (rt && rt->WaitForTraffic()) {
            const EvpnRouteEntry *evpn_rt = static_cast<const EvpnRouteEntry *>
                (rt);
            plen = evpn_rt->GetVmIpPlen();
            enqueue_traffic_seen = true;
        } else {
            IpAddress addr;
            rt = FlowEntry::GetEvpnRoute(in->vrf_, pkt->smac, addr,
                                         vm_intf->ethernet_tag());
            if (rt && rt->WaitForTraffic()) {
                plen = 32;
                if (pkt->family == Address::INET6) {
                    plen = 128;
                }
                enqueue_traffic_seen = true;
            }
        }

    }

    if (enqueue_traffic_seen) {
        flow_table->agent()->oper_db()->route_preference_module()->
            EnqueueTrafficSeen(sip, plen, in->intf_->id(),
                               pkt->vrf, pkt->smac);
    }
}

// Apply flow limits for in and out VMs
void PktFlowInfo::ApplyFlowLimits(const PktControlInfo *in,
                                  const PktControlInfo *out) {
    // Ignore flow limit checks for flow-update and short-flows
    if (short_flow || pkt->type == PktType::MESSAGE) {
        return;
    }

    bool limit_exceeded = false;
    bool interface_max_flow = false;
    if (in->intf_ && (in->intf_->type() == Interface::VM_INTERFACE)) {
        const VmInterface *vm_intf =
            dynamic_cast<const VmInterface *>(in->intf_);
        if (vm_intf) {
            if (vm_intf->max_flows()) {
                interface_max_flow = true;
                if ((vm_intf->flow_count() +2) > vm_intf->max_flows())
                    limit_exceeded = true;
            }
        }
    }

    if (out->intf_ && (out->intf_->type() == Interface::VM_INTERFACE)) {
        const VmInterface *vm_intf =
            dynamic_cast<const VmInterface *>(out->intf_);
        if (vm_intf) {
            if (vm_intf->max_flows()) {
                interface_max_flow = true;
                if ((vm_intf->flow_count() +2) > vm_intf->max_flows())
                    limit_exceeded = true;
            }
        }
    }

    if (agent->max_vm_flows() && (!interface_max_flow) &&
        (in->vm_ && ((in->vm_->flow_count() + 2) > agent->max_vm_flows()))) {
        limit_exceeded = true;
    }

    if (agent->max_vm_flows() && (!interface_max_flow) &&
        (out->vm_ && ((out->vm_->flow_count() + 2) > agent->max_vm_flows()))) {
        limit_exceeded = true;
    }

    if (limit_exceeded) {
        agent->stats()->incr_flow_drop_due_to_max_limit();
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_FLOW_LIMIT;
        return;
    }

    if (linklocal_bind_local_port == false)
        return;

    // Apply limits for link-local flows
    if (agent->pkt()->get_flow_proto()->linklocal_flow_count() >=
        agent->params()->linklocal_system_flows()) {
        limit_exceeded = true;
    }

    // Check per-vm linklocal flow-limits if specified
    if ((agent->params()->linklocal_vm_flows() !=
         agent->params()->linklocal_system_flows())) {
        if (in->vm_ && in->vm_->linklocal_flow_count() >=
            agent->params()->linklocal_vm_flows()) {
            limit_exceeded = true;
        }
    }

    if (limit_exceeded) {
        agent->stats()->incr_flow_drop_due_to_max_limit();
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_LINKLOCAL_SRC_NAT;
        return;
    }

    return;
}

void PktFlowInfo::LinkLocalPortBind(const PktInfo *pkt,
                                    const PktControlInfo *in,
                                    FlowEntry *flow) {
    assert(flow->in_vm_flow_ref()->fd() == VmFlowRef::kInvalidFd);
    if (linklocal_bind_local_port == false)
        return;

    // link-local service flow. Initialize nat-sport to original src-port.
    // It will be over-ridden if socket could be allocated later
    nat_sport = pkt->sport;

    // Dont allocate FD for short flows
    if (short_flow)
        return;

    if (flow->in_vm_flow_ref()->AllocateFd(agent, pkt->ip_proto) == false) {
        // Could not allocate FD. Make it short flow
        agent->stats()->incr_flow_drop_due_to_max_limit();
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_LINKLOCAL_SRC_NAT;
        return;
    }
    nat_sport = flow->in_vm_flow_ref()->port();

    return;
}

void PktFlowInfo::UpdateEvictedFlowStats(const PktInfo *pkt) {
    Agent *agent = flow_table->agent();
    KSyncFlowIndexManager *imgr = agent->ksync()->ksync_flow_index_manager();
    FlowEntryPtr flow = imgr->FindByIndex(pkt->agent_hdr.cmd_param);
    FlowMgmtManager *mgr = agent->pkt()->flow_mgmt_manager(
                               flow_table->table_index());

    /* Enqueue stats update request with UUID of the flow */
    if (flow.get() && flow->deleted() == false) {
        mgr->FlowStatsUpdateEvent(flow.get(), pkt->agent_hdr.cmd_param_2,
                                  pkt->agent_hdr.cmd_param_3,
                                  pkt->agent_hdr.cmd_param_4, flow->uuid());
    }
}

void PktFlowInfo::Add(const PktInfo *pkt, PktControlInfo *in,
                      PktControlInfo *out) {
    bool update = false;
    if (pkt->type != PktType::MESSAGE &&
        pkt->agent_hdr.cmd == AgentHdr::TRAP_FLOW_MISS) {
        if (pkt->agent_hdr.cmd_param != FlowEntry::kInvalidFlowHandle) {
            UpdateEvictedFlowStats(pkt);
        }
    }

    if ((pkt->type == PktType::MESSAGE &&
        pkt->agent_hdr.cmd == AgentHdr::TRAP_FLOW_MISS)) {
        update = true;
    }

    // Generate traffic seen event for path preference module
    GenerateTrafficSeen(pkt, in);
    IpAddress sip = pkt->ip_saddr;
    IpAddress dip = pkt->ip_daddr;
    if (pkt->ignore_address == VmInterface::IGNORE_SOURCE) {
        if (ingress) {
            sip = FamilyToAddress(pkt->family);
        } else {
            dip = FamilyToAddress(pkt->family);
        }
    } else if (pkt->ignore_address == VmInterface::IGNORE_DESTINATION) {
        if (ingress) {
            dip = FamilyToAddress(pkt->family);
        } else {
            sip = FamilyToAddress(pkt->family);
        }
    }

    if (pkt->is_fat_flow_src_prefix) {
        sip = pkt->ip_ff_src_prefix;
    }
    if (pkt->is_fat_flow_dst_prefix) {
        dip = pkt->ip_ff_dst_prefix;
    }

    FlowKey key(in->nh_, sip, dip, pkt->ip_proto, pkt->sport, pkt->dport);
    FlowEntryPtr flow = FlowEntry::Allocate(key, flow_table);

    ApplyFlowLimits(in, out);
    LinkLocalPortBind(pkt, in, flow.get());

    // rflow for newly allocated entry should always be NULL
    FlowEntryPtr rflow = flow->reverse_flow_entry();
    assert(rflow == NULL);

    uint16_t r_sport;
    uint16_t r_dport;
    if ((pkt->family == Address::INET && pkt->ip_proto == IPPROTO_ICMP) ||
        (pkt->family == Address::INET6 && pkt->ip_proto == IPPROTO_ICMPV6)) {
        r_sport = pkt->sport;
        r_dport = pkt->dport;
    } else if (nat_done) {
        r_sport = nat_dport;
        r_dport = nat_sport;
    } else {
        r_sport = pkt->dport;
        r_dport = pkt->sport;
    }

    // Allocate reverse flow
    if (nat_done) {
        FlowKey rkey(out->nh_, nat_ip_daddr, nat_ip_saddr, pkt->ip_proto,
                     r_sport, r_dport);
        rflow = FlowEntry::Allocate(rkey, flow_table);
    } else {
        if (pkt->same_port_number && (in->nh_ == out->nh_)) {
            /* When source and destination ports are same and FatFlow is
             * configured for that port, always mask source port for both
             * forward and reverse flows */
            r_sport = pkt->sport;
            r_dport = pkt->dport;
        }
        FlowKey rkey(out->nh_, dip, sip, pkt->ip_proto, r_sport, r_dport);
        rflow = FlowEntry::Allocate(rkey, flow_table);
    }

    bool swap_flows = false;
    // If this is message processing, then retain forward and reverse flows
    if (pkt->type == PktType::MESSAGE && !short_flow &&
        flow_entry->is_flags_set(FlowEntry::ReverseFlow)) {
        // for cases where we need to swap flows rflow should always
        // be Non-NULL
        assert(rflow != NULL);
        swap_flows = true;
    }

    tcp_ack = pkt->tcp_ack;
    flow->InitFwdFlow(this, pkt, in, out, rflow.get(), agent);
    if (rflow != NULL) {
        rflow->InitRevFlow(this, pkt, out, in, flow.get(), agent);
    }

    flow->GetPolicyInfo();
    if (rflow != NULL) {
        rflow->GetPolicyInfo();
    }

    flow->ResyncFlow();
    if (rflow != NULL) {
        rflow->ResyncFlow();
    }

    // RPF computation can be done only after policy processing.
    // Do RPF computation now
    flow->RpfUpdate();
    if (rflow)
        rflow->RpfUpdate();

    /* Fip stats info in not updated in InitFwdFlow and InitRevFlow because
     * both forward and reverse flows are not not linked to each other yet.
     * We need both forward and reverse flows to update Fip stats info */
    UpdateFipStatsInfo(flow.get(), rflow.get(), pkt, in, out);

    FlowEntry *tmp = swap_flows ? rflow.get() : flow.get();
    if (update) {
        agent->pkt()->get_flow_proto()->UpdateFlow(tmp);
    } else {
        agent->pkt()->get_flow_proto()->AddFlow(tmp);
    }
}

void PktFlowInfo::UpdateFipStatsInfo
    (FlowEntry *flow, FlowEntry *rflow, const PktInfo *pkt,
     const PktControlInfo *in, const PktControlInfo *out) {

    if (pkt->family != Address::INET) {
        //TODO: v6 handling
        return;
    }
    uint32_t intf_id, r_intf_id;
    uint32_t fip, r_fip;
    intf_id = Interface::kInvalidIndex;
    r_intf_id = Interface::kInvalidIndex;
    fip = 0;
    r_fip = 0;
    if (fip_snat && fip_dnat && rflow != NULL) {
        /* This is the case where Source and Destination VMs (part of
         * same compute node) have floating-IP assigned to each of them from
         * a common VN and then each of these VMs send traffic to other VM by
         * addressing the other VM's Floating IP. In this case both SNAT and
         * DNAT flags will be set. We identify SNAT and DNAT flows by
         * inspecting IP of forward and reverse flows and update Fip stats
         * info based on that. */
        const FlowKey *nat_key = &(rflow->key());
        if (flow->key().src_addr != nat_key->dst_addr) {
            //SNAT case
            fip = snat_fip.to_v4().to_ulong();
            intf_id = in->intf_->id();
        } else if (flow->key().dst_addr != nat_key->src_addr) {
            //DNAT case
            fip = flow->key().dst_addr.to_v4().to_ulong();
            intf_id = out->intf_->id();
        }
        nat_key = &(flow->key());
        if (rflow->key().src_addr != nat_key->dst_addr) {
            //SNAT case
            r_fip = snat_fip.to_v4().to_ulong();
            r_intf_id = in->intf_->id();
        } else if (rflow->key().dst_addr != nat_key->src_addr) {
            //DNAT case
            r_fip = rflow->key().dst_addr.to_v4().to_ulong();
            r_intf_id = out->intf_->id();
        }
    } else if (fip_snat) {
        fip = r_fip = nat_ip_saddr.to_v4().to_ulong();
        intf_id = r_intf_id = in->intf_->id();
    } else if (fip_dnat) {
        fip = r_fip = pkt->ip_daddr.to_v4().to_ulong();
        intf_id = r_intf_id = out->intf_->id();
    }

    if (fip_snat || fip_dnat) {
        flow->UpdateFipStatsInfo(fip, intf_id, agent);
        if (rflow != NULL) {
            rflow->UpdateFipStatsInfo(r_fip, r_intf_id, agent);
        }
    }
}

void PktFlowInfo::SetPktInfo(boost::shared_ptr<PktInfo> pkt_info) {
     family = pkt_info->family;
     pkt = pkt_info;
 }

IpAddress PktFlowInfo::FamilyToAddress(Address::Family family) {
    if (pkt->family == Address::INET6) {
    return Ip6Address();
    }
    return Ip4Address();
}
