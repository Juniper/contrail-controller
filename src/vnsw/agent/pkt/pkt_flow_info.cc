/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */


#include "base/os.h"
#include <arpa/inet.h>
#include <netinet/in.h>

#include "net/address_util.h"
#include "route/route.h"

#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
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

static void LogError(const PktInfo *pkt, const char *str) {
    if (pkt->family == Address::INET) {
        FLOW_TRACE(DetailErr, pkt->agent_hdr.cmd_param, pkt->agent_hdr.ifindex,
                   pkt->agent_hdr.vrf, pkt->ip_saddr.to_v4().to_ulong(),
                   pkt->ip_daddr.to_v4().to_ulong(), str, pkt->l3_forwarding,
                   0, 0, 0, 0);
    } else if (pkt->family == Address::INET6) {
        uint64_t sip[2], dip[2];
        Ip6AddressToU64Array(pkt->ip_saddr.to_v6(), sip, 2);
        Ip6AddressToU64Array(pkt->ip_daddr.to_v6(), dip, 2);
        FLOW_TRACE(DetailErr, pkt->agent_hdr.cmd_param, pkt->agent_hdr.ifindex,
                   pkt->agent_hdr.vrf, -1, -1, str, pkt->l3_forwarding,
                   sip[0], sip[1], dip[0], dip[1]);
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
    pkt->l3_forwarding = true;
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

static const NextHop* GetPolicyDisabledNH(NextHopTable *nh_table,
                                          const NextHop *nh) {
    if (nh->PolicyEnabled() == false) {
        return nh;
    }
    DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
    nh_key->SetPolicy(false);
    return static_cast<const NextHop *>
        (nh_table->FindActiveEntryNoLock(key.get()));
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

static bool PickEcmpMember(const NextHop **nh, const PktInfo *pkt,
                           PktFlowInfo *info,
                           const EcmpLoadBalance &ecmp_load_balance) {
    // We dont support ECMP in L2 yet. Return failure to drop packet
    if (pkt->l3_forwarding == false) {
        info->out_component_nh_idx = CompositeNH::kInvalidComponentNHIdx;
        return true;
    }

    const CompositeNH *comp_nh = dynamic_cast<const CompositeNH *>(*nh);
    // ECMP supported only if composite-type is ECMP or LOCAL_ECMP
    if (comp_nh == NULL ||
        (comp_nh->composite_nh_type() != Composite::ECMP &&
        comp_nh->composite_nh_type() != Composite::LOCAL_ECMP)) {
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
        comp_nh->PickMember(pkt->hash(ecmp_load_balance),
                            info->out_component_nh_idx);
    *nh = comp_nh->GetNH(info->out_component_nh_idx);

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
static bool NhDecode(const NextHop *nh, const PktInfo *pkt, PktFlowInfo *info,
                     PktControlInfo *in, PktControlInfo *out,
                     bool force_vmport,
                     const EcmpLoadBalance &ecmp_load_balance) {
    bool ret = true;

    if (!nh->IsActive())
        return false;

    // If nh is Composite, pick the ECMP first. The nh index and vrf used
    // in reverse flow will depend on the ECMP member picked
    if (PickEcmpMember(&nh, pkt, info, ecmp_load_balance) == false) {
        return false;
    }

    NextHopTable *nh_table = info->agent->nexthop_table();
    // Pick out going attributes based on the NH selected above
    switch (nh->GetType()) {
    case NextHop::INTERFACE:
        out->intf_ = static_cast<const InterfaceNH*>(nh)->GetInterface();
        if (out->intf_->type() == Interface::VM_INTERFACE) {
            //Local flow, pick destination interface
            //nexthop as reverse flow key
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
        assert(pkt->l3_forwarding == true);
        out->intf_ = static_cast<const ReceiveNH *>(nh)->GetInterface();
        out->vrf_ = out->intf_->vrf();
        out->nh_ = GetPolicyDisabledNH(nh_table, nh)->id();
        break;

    case NextHop::VLAN: {
        assert(pkt->l3_forwarding == true);
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
        out->nh_ = in->nh_;

        // The NH in reverse flow can change only if ECMP-NH is used. There is
        // no ECMP for layer2 flows
        if (pkt->l3_forwarding == false) {
            break;
        }

        // If source-ip is in ECMP, reverse flow would use ECMP-NH as key
        const InetUnicastRouteEntry *rt =
            dynamic_cast<const InetUnicastRouteEntry *>(in->rt_);
        if (rt == NULL) {
            break;
        }

        // Get only local-NH from route
        const NextHop *local_nh = rt->GetLocalNextHop();
        if (local_nh && local_nh->IsActive() == false) {
            LogError(pkt, "Invalid or Inactive local nexthop ");
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
        assert(pkt->l3_forwarding == true);
        const ArpNH *arp_nh = static_cast<const ArpNH *>(nh);
        if (in->intf_->type() == Interface::VM_INTERFACE) {
            const VmInterface *vm_intf =
                static_cast<const VmInterface *>(in->intf_);
            if (vm_intf->device_type() == VmInterface::LOCAL_DEVICE) {
                out->nh_ = arp_nh->id();
            } else {
                out->nh_ = arp_nh->GetInterface()->flow_key_nh()->id();
            }
        }
        out->intf_ = arp_nh->GetInterface();
        out->vrf_ = arp_nh->GetVrf();
        break;
    }

        // RESOLVE Nexthop means traffic came from gateway interface and destined
        // to another gateway interface
    case NextHop::RESOLVE: {
        assert(pkt->l3_forwarding == true);
        const ResolveNH *rsl_nh = static_cast<const ResolveNH *>(nh);
        out->nh_ = rsl_nh->interface()->flow_key_nh()->id();
        out->intf_ = rsl_nh->interface();
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
static bool RouteToOutInfo(const AgentRoute *rt, const PktInfo *pkt,
                           PktFlowInfo *info, PktControlInfo *in,
                           PktControlInfo *out) {
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

    return NhDecode(nh, pkt, info, in, out, false,
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
    if (pkt_info->l3_flow == false)
        return false;

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
            pkt->l3_forwarding = true;
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
    if (RouteToOutInfo(out->rt_, pkt, this, in, out) == false) {
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
    if (in->intf_->type() == Interface::VM_INTERFACE) {
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
                                       &sport) == false) {
        return;
    }

    out->vrf_ = agent->vrf_table()->FindVrfFromName(agent->fabric_vrf_name());
    dest_vrf = out->vrf_->vrf_id();

    nat_done = true;
    //Populate NAT
    nat_ip_saddr = agent->router_id();
    nat_ip_daddr = nat_server;
    nat_sport = sport;
    nat_dport = pkt->dport;
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

        if (pkt->ip_daddr.to_v4() == it->floating_ip_) {
            break;
        }
    }

    if (it == fip_list.end()) {
        // No matching floating ip for destination-ip
        return;
    }
    in->vn_ = NULL;
    if (nat_done == false) {
        UpdateRoute(&in->rt_, it->vrf_.get(), pkt->ip_saddr, pkt->smac,
                    flow_source_plen_map);
        nat_dest_vrf = it->vrf_.get()->vrf_id();
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
    if (VrfTranslate(pkt, in, out, pkt->ip_saddr, true) == false) {
        return;
    }

    // Translate the Dest-IP
    if (nat_done == false)
        nat_ip_saddr = pkt->ip_saddr;
    nat_ip_daddr = it->fixed_ip_;
    nat_sport = pkt->sport;
    nat_dport = pkt->dport;
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
    bool change = false;
    // Find Floating-IP matching destination-ip
    for ( ; it != fip_list.end(); ++it) {
        if (it->vrf_.get() == NULL) {
            continue;
        }

        if (pkt->ip_saddr != it->fixed_ip_) {
            continue;
        }

        const AgentRoute *rt_match = FlowEntry::GetUcRoute(it->vrf_.get(),
                pkt->ip_daddr);
        if (rt_match == NULL) {
            flow_dest_plen_map[it->vrf_.get()->vrf_id()] = 0;
            continue;
        }
        uint8_t out_rt_plen = RouteToPrefixLen(out->rt_);
        uint8_t rt_match_plen = RouteToPrefixLen(rt_match);
        // found the route match
        // prefer the route with longest prefix match
        // if prefix length is same prefer route from floating IP
        // if routes are from fip of difference VRF, prefer the one with lower name.
        // if both the selected and current FIP is from same vrf prefer the one with lower ip addr.
        if (out->rt_ == NULL || rt_match_plen > out_rt_plen) {
            change = true;
        } else if (rt_match_plen == out_rt_plen) {
            if (fip_it == fip_list.end()) {
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
                flow_dest_plen_map[out->rt_->vrf_id()] = RouteToPrefixLen(out->rt_);
            }
            out->rt_ = rt_match;
            fip_it = it;
            change = false;
        } else {
            flow_dest_plen_map[rt_match->vrf_id()] = RouteToPrefixLen(rt_match);
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

    if (VrfTranslate(pkt, in, out, fip_it->floating_ip_, true) == false) {
        return;
    }
    if (out->rt_ == NULL || in->rt_ == NULL) {
        //If After VRF translation, ingress route or
        //egress route is NULL, mark the flow as short flow
        return;
    }

    // Compute out-intf and ECMP info from out-route
    if (RouteToOutInfo(out->rt_, pkt, this, in, out) == false) {
        return;
    }

    dest_vrf = out->rt_->vrf_id();
    // Setup reverse flow to translate sip.
    nat_done = true;
    nat_ip_saddr = fip_it->floating_ip_;
    nat_ip_daddr = pkt->ip_daddr;
    nat_sport = pkt->sport;
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
    }
    return true;
}

void PktFlowInfo::IngressProcess(const PktInfo *pkt, PktControlInfo *in,
                                 PktControlInfo *out) {
    // Flow packets are expected only on VMPort interfaces
    if (in->intf_->type() != Interface::VM_INTERFACE &&
        in->intf_->type() != Interface::INET) {
        LogError(pkt, "Unexpected packet on Non-VM interface");
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
        if (RouteToOutInfo(out->rt_, pkt, this, in, out)) {
            if (out->intf_) {
                out->vn_ = InterfaceToVn(out->intf_);
                if (out->vrf_) {
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
        if (IntfHasFloatingIp(this, in->intf_, pkt->family)) {
            FloatingIpSNat(pkt, in, out);
        }
    }
    
    if (out->rt_ != NULL) {
        // Route is present. If IP-DA is a floating-ip, we need DNAT
        if (RouteToOutInfo(out->rt_, pkt, this, in, out)) {
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
        RouteToOutInfo(out->rt_, pkt, this, in, out);
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
    return;
}

const NextHop *PktFlowInfo::TunnelToNexthop(const PktInfo *pkt) {
    tunnel_type = pkt->tunnel.type;
    if (tunnel_type.GetType() == TunnelType::MPLS_GRE ||
        tunnel_type.GetType() == TunnelType::MPLS_UDP) {
        MplsLabel *mpls = agent->mpls_table()->FindMplsLabel(pkt->tunnel.label);
        if (mpls == NULL) {
            LogError(pkt, "Invalid Label in egress flow");
            return NULL;
        }
        return mpls->nexthop();
    } else if (tunnel_type.GetType() == TunnelType::VXLAN) {
        VxLanTable *table = static_cast<VxLanTable *>(agent->vxlan_table());
        VxLanId *vxlan = table->FindNoLock(pkt->tunnel.vxlan_id);
        if (vxlan == NULL) {
            LogError(pkt, "Invalid vxlan in egress flow");
            return NULL;
        }

        const VrfNH *nh = dynamic_cast<const VrfNH *>(vxlan->nexthop());
        if (nh == NULL)
            return NULL;

        const VrfEntry *vrf = nh->GetVrf();
        if (vrf == NULL)
            return NULL;

        // In case of VXLAN, the NH points to VrfNH. Need to do route lookup
        // on dmac to find the real nexthop
        AgentRoute *rt = FlowEntry::GetL2Route(vrf, pkt->dmac);
        if (rt != NULL) {
            return rt->GetActiveNextHop();
        }

        return NULL;
    } else {
        LogError(pkt, "Invalid tunnel type in egress flow");
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
    if (NhDecode(nh, pkt, this, in, out, true, EcmpLoadBalance()) == false) {
        return;
    }

    if (out->intf_ && out->intf_->type() == Interface::VM_INTERFACE) {
        const VmInterface *vm_intf = static_cast<const VmInterface *>(out->intf_);
        if (vm_intf->IsFloatingIp(pkt->ip_daddr)) {
            pkt->l3_forwarding = true;
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
                                   hash(out->rt_->GetActivePath()->
                                        ecmp_load_balance()));
        }
        if (out->rt_->GetActiveNextHop()->GetType() == NextHop::ARP ||
            out->rt_->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
            //If a packet came with mpls label pointing to
            //vrf NH, then we need to do a route lookup
            //and set the nexthop for reverse flow properly
            //as mpls pointed NH would not be used for reverse flow
            if (RouteToOutInfo(out->rt_, pkt, this, in, out)) {
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

bool PktFlowInfo::Process(const PktInfo *pkt, PktControlInfo *in,
                          PktControlInfo *out) {
    in->intf_ = agent->interface_table()->FindInterface(pkt->agent_hdr.ifindex);
    out->nh_ = in->nh_ = pkt->agent_hdr.nh;

    if (agent->tsn_enabled()) {
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_FLOW_ON_TSN;
        return false;
    }

    if (in->intf_ == NULL ||
        (pkt->l3_forwarding == true &&
         in->intf_->type() == Interface::VM_INTERFACE &&
         in->intf_->ip_active(pkt->family) == false) ||
        (pkt->l3_forwarding == false &&
         in->intf_->l2_active() == false)) {
        in->intf_ = NULL;
        LogError(pkt, "Invalid or Inactive ifindex");
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_UNAVIALABLE_INTERFACE;
        return false;
    }

    if (in->intf_->type() == Interface::VM_INTERFACE) {
        const VmInterface *vm_intf = 
            static_cast<const VmInterface *>(in->intf_);
        if (pkt->l3_forwarding && vm_intf->layer3_forwarding() == false) {
            LogError(pkt, "ipv4 service not enabled for ifindex");
            short_flow = true;
            short_flow_reason = FlowEntry::SHORT_IPV4_FWD_DIS;
            return false;
        }

        if (pkt->l3_forwarding == false &&
            vm_intf->bridging() == false) {
            LogError(pkt, "Bridge service not enabled for ifindex");
            short_flow = true;
            short_flow_reason = FlowEntry::SHORT_IPV4_FWD_DIS;
            return false;
        }
    }

    in->vrf_ = agent->vrf_table()->FindVrfFromId(pkt->agent_hdr.vrf);
    if (in->vrf_ == NULL || !in->vrf_->IsActive()) {
        in->vrf_ = NULL;
        LogError(pkt, "Invalid or Inactive VRF");
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_UNAVIALABLE_VRF;
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

    if (in->rt_ == NULL) {
        LogError(pkt, "Flow : No route for Src-IP");
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_NO_SRC_ROUTE;
        return false;
    }

    if (out->rt_ == NULL) {
        LogError(pkt, "Flow : No route for Dst-IP");
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_NO_DST_ROUTE;
        return false;
    }
    flow_source_vrf = static_cast<const AgentRoute *>(in->rt_)->vrf_id();
    flow_dest_vrf = out->rt_->vrf_id();

    //If source is ECMP, establish a reverse flow pointing
    //to the component index
    if (in->rt_->GetActiveNextHop() &&
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
    // Generate event if route was waiting for traffic
    if (rt && rt->WaitForTraffic()) {
        enqueue_traffic_seen = true;
    } else if (vm_intf) {
        //L3 route is not in wait for traffic state
        //EVPN route could be in wait for traffic, if yes
        //enqueue traffic seen
        rt = FlowEntry::GetEvpnRoute(in->vrf_, pkt->smac, sip,
                vm_intf->ethernet_tag());
        if (rt && rt->WaitForTraffic()) {
            enqueue_traffic_seen = true;
        }
    }

    if (enqueue_traffic_seen) {
        uint8_t plen = 32;
        if (pkt->family == Address::INET6) {
            plen = 128;
        }
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
    if (agent->max_vm_flows() &&
        (in->vm_ && ((in->vm_->flow_count() + 2) > agent->max_vm_flows()))) {
        limit_exceeded = true;
    }

    if (agent->max_vm_flows() &&
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

    FlowKey key(in->nh_, pkt->ip_saddr, pkt->ip_daddr, pkt->ip_proto,
                pkt->sport, pkt->dport);
    FlowEntryPtr flow = FlowEntry::Allocate(key, flow_table);

    ApplyFlowLimits(in, out);
    LinkLocalPortBind(pkt, in, flow.get());

    // In case the packet is for a reverse flow of a linklocal flow,
    // link to that flow (avoid creating a new reverse flow entry for the case)
    FlowEntryPtr rflow = flow->reverse_flow_entry();
    // rflow for newly allocated entry should always be NULL
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
        FlowKey rkey(out->nh_, pkt->ip_daddr, pkt->ip_saddr, pkt->ip_proto,
                     r_sport, r_dport);
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
     l3_flow = pkt_info->l3_forwarding;
     family = pkt_info->family;
     pkt = pkt_info;
 }
