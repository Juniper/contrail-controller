/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include "route/route.h"

#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/route_common.h"
#include "oper/path_preference.h"
#include "oper/vrf.h"
#include "oper/sg.h"
#include "oper/global_vrouter.h"
#include "oper/operdb_init.h"
#include "oper/tunnel_nh.h"

#include "filter/packet_header.h"
#include "filter/acl.h"

#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "pkt/pkt_handler.h"
#include "pkt/flow_table.h"
#include "pkt/flow_proto.h"
#include "pkt/pkt_sandesh_flow.h"
#include "pkt/agent_stats.h"
#include "ksync/flowtable_ksync.h"
#include <ksync/ksync_init.h>

static void LogError(const PktInfo *pkt, const char *str) {
    if (pkt->family == Address::INET) {
        FLOW_TRACE(DetailErr, pkt->agent_hdr.cmd_param, pkt->agent_hdr.ifindex,
                   pkt->agent_hdr.vrf, pkt->ip_saddr.to_v4().to_ulong(),
                   pkt->ip_daddr.to_v4().to_ulong(), str);
    } else if (pkt->family == Address::INET6) {
        // TODO : IPv6
        FLOW_TRACE(DetailErr, pkt->agent_hdr.cmd_param, pkt->agent_hdr.ifindex,
                   pkt->agent_hdr.vrf, -1, -1, str);
    } else {
        assert(0);
    }
}

uint8_t PktFlowInfo::RouteToPrefixLen(const AgentRoute *route) {
    if (route == NULL) {
        return 0;
    }
    const InetUnicastRouteEntry *rt =
        static_cast<const InetUnicastRouteEntry *>(route);
    return rt->plen();
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

static const NextHop* GetPolicyEnabledNH(const NextHop *nh) {
    if (nh->PolicyEnabled()) {
        return nh;
    }
    DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
    nh_key->SetPolicy(true);
    return static_cast<const NextHop *>(
            Agent::GetInstance()->nexthop_table()->FindActiveEntry(key.get()));
}

static const NextHop* GetPolicyDisabledNH(const NextHop *nh) {
    if (nh->PolicyEnabled() == false) {
        return nh;
    }
    DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
    nh_key->SetPolicy(false);
    return static_cast<const NextHop *>(
            Agent::GetInstance()->nexthop_table()->FindActiveEntry(key.get()));
}

// Get interface from a NH. Also, decode ECMP information from NH
static bool NhDecode(const NextHop *nh, const PktInfo *pkt, PktFlowInfo *info,
                     PktControlInfo *in, PktControlInfo *out,
                     bool force_vmport) {
    bool ret = true;

    if (!nh->IsActive())
        return false;

    // If its composite NH, find interface information from the component NH
    const CompositeNH *comp_nh = NULL;
    if (nh->GetType() == NextHop::COMPOSITE) {
        comp_nh = static_cast<const CompositeNH *>(nh);
        if (comp_nh->composite_nh_type() == Composite::ECMP ||
            comp_nh->composite_nh_type() == Composite::LOCAL_ECMP) {
            info->ecmp = true;
            const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
            if (info->out_component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx ||
                (comp_nh->GetNH(info->out_component_nh_idx) == NULL)) {
                if (info->out_component_nh_idx !=
                        CompositeNH::kInvalidComponentNHIdx) {
                    //Dont trap reverse flow, upon flow establishment
                    //To be removed once trapped packets are retransmitted
                    info->trap_rev_flow = true;
                }
                info->out_component_nh_idx =
                    comp_nh->hash(pkt->hash());
             }
             nh = comp_nh->GetNH(info->out_component_nh_idx);
             if (nh->IsActive() == false) {
                return false;
             }
        }
    } else {
        info->out_component_nh_idx = CompositeNH::kInvalidComponentNHIdx;
    }

    switch (nh->GetType()) {
    case NextHop::INTERFACE:
        out->intf_ = static_cast<const InterfaceNH*>(nh)->GetInterface();
        if (out->intf_->type() != Interface::PACKET) {
            out->vrf_ = static_cast<const InterfaceNH*>(nh)->GetVrf();
        }
        if (out->intf_->type() == Interface::VM_INTERFACE) {
            //Local flow, pick destination interface
            //nexthop as reverse flow key
            out->nh_ = GetPolicyEnabledNH(nh)->id();
        } else if (out->intf_->type() == Interface::PACKET) {
            //Packet destined to pkt interface, packet originating
            //from pkt0 interface will use destination interface as key
            out->nh_ = in->nh_;
        } else {
            //Remote flow, use source interface as nexthop key
            out->nh_ = nh->id();
        }
        break;

    case NextHop::RECEIVE:
        out->intf_ = static_cast<const ReceiveNH *>(nh)->GetInterface();
        out->vrf_ = out->intf_->vrf();
        out->nh_ = GetPolicyDisabledNH(nh)->id();
        break;

    case NextHop::VLAN: {
        const VlanNH *vlan_nh = static_cast<const VlanNH*>(nh);
        out->intf_ = vlan_nh->GetInterface();
        out->vlan_nh_ = true;
        out->vlan_tag_ = vlan_nh->GetVlanTag();
        out->vrf_ = vlan_nh->GetVrf();
        out->nh_ = nh->id();
        break;
    }

    case NextHop::TUNNEL: {
        if (pkt->family == Address::INET) {
            const InetUnicastRouteEntry *rt =
                static_cast<const InetUnicastRouteEntry *>(in->rt_);
            if (rt != NULL && rt->GetLocalNextHop()) {
                out->nh_ = rt->GetLocalNextHop()->id();
            } else {
                out->nh_ = in->nh_;
            }
        } else {
            //TODO::v6
        }
        out->intf_ = NULL;
        break;
    }

    case NextHop::COMPOSITE: {
        out->nh_ = nh->id();
        out->intf_ = NULL;
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
        } else if (force_vmport &&
                   out->intf_->type() != Interface::VM_INTERFACE) {
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
    Agent *agent = static_cast<AgentRouteTable *>(rt->get_table())->agent();
    const AgentPath *path = rt->GetActivePath();
    if (path == NULL)
        return false;

    const NextHop *nh = static_cast<const NextHop *>(path->nexthop(agent));
    if (nh == NULL)
        return false;

    if (nh->IsActive() == false) {
        return false;
    }

    return NhDecode(nh, pkt, info, in, out, false);
}

static const VnEntry *InterfaceToVn(const Interface *intf) {
    if (intf->type() != Interface::VM_INTERFACE)
        return NULL;

    const VmInterface *vm_port = static_cast<const VmInterface *>(intf);
    return vm_port->vn();
}

static bool IntfHasFloatingIp(const Interface *intf, Address::Family family) {
    if (intf->type() != Interface::VM_INTERFACE)
        return NULL;
    return static_cast<const VmInterface *>(intf)->HasFloatingIp(family);
}

static bool IsLinkLocalRoute(const AgentRoute *rt) {
    const AgentPath *path = rt->GetActivePath();
    if (path && path->peer() == Agent::GetInstance()->link_local_peer())
        return true;

    return false;
}

static const string *RouteToVn(const AgentRoute *rt) {
    const AgentPath *path = NULL;
    if (rt) {
        path = rt->GetActivePath();
    }
    if (path == NULL) {
        return &(Agent::NullString());
    }

    return &path->dest_vn_name();
}

static void SetInEcmpIndex(const PktInfo *pkt, PktFlowInfo *flow_info,
                           PktControlInfo *in, PktControlInfo *out) {
    if (!in->rt_) {
        return;
    }

    if (in->rt_->GetActiveNextHop()->GetType() != NextHop::COMPOSITE) {
        return;
    }

    if (pkt->family == Address::INET6) {
        //TODO::ECMP for v6
    }
    Agent *agent = static_cast<AgentRouteTable *>(in->rt_->get_table())->agent();
    //Get route for source IP route, which would be used to forward
    //reverse flow traffic
    const InetUnicastRouteEntry *rt;
    FlowTable *ftable = agent->pkt()->flow_table();
    if (flow_info->nat_done) {
        VrfEntry *nat_vrf = NULL;
        if (flow_info->fip_snat) {
            nat_vrf = agent->vrf_table()->FindVrfFromId(flow_info->nat_vrf);
        } else {
            nat_vrf = agent->vrf_table()->FindVrfFromId(flow_info->nat_dest_vrf);
        }
        if (nat_vrf == NULL) {
            return;
        }
        rt = static_cast<InetUnicastRouteEntry *>
            (ftable->GetUcRoute(nat_vrf, Ip4Address(flow_info->nat_ip_saddr.to_v4())));
    } else {
        if (out->vrf_ == NULL) {
            return;
        }
        rt = static_cast<InetUnicastRouteEntry *>
            (ftable->GetUcRoute(out->vrf_, Ip4Address(pkt->ip_saddr.to_v4())));
    }

    if (rt == NULL) {
        return;
    }

    NextHop *component_nh_ptr = NULL;
    uint32_t label;
    //Frame key for component NH
    if (flow_info->ingress) {
        //Ingress flow
        const VmInterface *vm_port = static_cast<const VmInterface *>(in->intf_);
        const VrfEntry *vrf = agent->vrf_table()->FindVrfFromId(pkt->vrf);
        if (vm_port->HasServiceVlan() && vm_port->vrf() != vrf) {
            //Packet came on service VRF
            label = vm_port->GetServiceVlanLabel(vrf);
            uint32_t vlan = vm_port->GetServiceVlanTag(vrf);

            VlanNHKey key(vm_port->GetUuid(), vlan);
            component_nh_ptr =
                static_cast<NextHop *>
                (agent->nexthop_table()->FindActiveEntry(&key));
        } else {
            InterfaceNHKey key(static_cast<InterfaceKey *>
                               (vm_port->GetDBRequestKey().release()),
                               false, InterfaceNHFlags::INET4);
            component_nh_ptr =
                static_cast<NextHop *>
                (agent->nexthop_table()->FindActiveEntry(&key));
            label = vm_port->label();
        }
    } else {
        //Packet from fabric
        Ip4Address dest_ip(pkt->tunnel.ip_saddr);
        TunnelNHKey key(agent->fabric_vrf_name(), agent->router_id(),
                        dest_ip, false, pkt->tunnel.type);
        //Get component NH pointer
        component_nh_ptr =
            static_cast<NextHop *>
            (agent->nexthop_table()->FindActiveEntry(&key));
        //Get Label to be used to reach destination server
        const CompositeNH *nh = 
            static_cast<const CompositeNH *>(rt->GetActiveNextHop());
        label = nh->GetRemoteLabel(dest_ip);
    }
    ComponentNH component_nh(label, component_nh_ptr);

    const NextHop *nh = NULL;
    if (out->intf_) {
        //Local destination, use active path
        nh = rt->GetActiveNextHop();
    } else {
        //Destination on remote server
        //Choose local path, which will also pointed by MPLS label
        if (rt->FindPath(Agent::GetInstance()->ecmp_peer())) {
            nh = rt->FindPath(agent->ecmp_peer())->nexthop(agent);
        } else {
            //Aggregarated routes may not have local path
            //Derive local path
            nh = rt->GetLocalNextHop();
        }
    }

    if (nh && nh->GetType() == NextHop::COMPOSITE) {
        const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
        //Find component entry index in composite NH
        uint32_t idx = 0;
        if (comp_nh->GetIndex(component_nh, idx)) {
            flow_info->in_component_nh_idx = idx;
            flow_info->ecmp = true;
        }
    } else {
        //Ideally this case is not ecmp, as on reverse flow we are hitting 
        //a interface NH and not composite NH, install reverse flow for consistency
        flow_info->ecmp = true;
    }
}

static bool RouteAllowNatLookup(const AgentRoute *rt) {
    if (rt != NULL && IsLinkLocalRoute(rt)) {
        // skip NAT lookup if found route has link local peer.
        return false;
    }

    return true;
}

void PktFlowInfo::SetEcmpFlowInfo(const PktInfo *pkt, const PktControlInfo *in,
                                  const PktControlInfo *out) {
    nat_ip_daddr = pkt->ip_daddr;
    nat_ip_saddr = pkt->ip_saddr;
    nat_dport = pkt->dport;
    nat_sport = pkt->sport;
    if (out->intf_ && out->intf_->type() == Interface::VM_INTERFACE) {
        dest_vrf = out->vrf_->vrf_id();
    } else {
        dest_vrf = pkt->vrf;
    }
    nat_vrf = dest_vrf;
    nat_dest_vrf = pkt->vrf;
}

// For link local services, we bind to a local port & use it as NAT source port.
// The socket is closed when the flow entry is deleted.
uint32_t PktFlowInfo::LinkLocalBindPort(const VmEntry *vm, uint8_t proto) {
    if (vm == NULL)
        return 0;
    // Do not allow more than max link local flows
    if (flow_table->linklocal_flow_count() >=
        flow_table->agent()->params()->linklocal_system_flows())
        return 0;
    if (flow_table->VmLinkLocalFlowCount(vm) >=
        flow_table->agent()->params()->linklocal_vm_flows())
        return 0;

    if (proto == IPPROTO_TCP) {
        linklocal_src_port_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    } else if (proto == IPPROTO_UDP) {
        linklocal_src_port_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }

    if (linklocal_src_port_fd == -1) {
        return 0;
    }

    // allow the socket to be reused upon close
    int optval = 1;
    setsockopt(linklocal_src_port_fd, SOL_SOCKET, SO_REUSEADDR,
               &optval, sizeof(optval));

    struct sockaddr_in address;
    memset(&address, '0', sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = 0;
    address.sin_port = 0;
    struct sockaddr_in bound_to;
    socklen_t len = sizeof(bound_to);
    if (bind(linklocal_src_port_fd, (struct sockaddr*) &address,
             sizeof(address)) == -1) {
        goto error;
    }

    if (getsockname(linklocal_src_port_fd, (struct sockaddr*) &bound_to,
                    &len) == -1) {
        goto error;
    }

    return ntohs(bound_to.sin_port);

error:
    if (linklocal_src_port_fd != kLinkLocalInvalidFd) {
        close(linklocal_src_port_fd);
        linklocal_src_port_fd = kLinkLocalInvalidFd;
    }
    return 0;
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
    if (!Agent::GetInstance()->oper_db()->global_vrouter()->
        FindLinkLocalService(pkt->ip_daddr.to_v4(), pkt->dport,
                             &service_name, &nat_server, &nat_port)) {
        // link local service not configured, drop the request
        in->rt_ = NULL;
        out->rt_ = NULL;
        return; 
    }

    out->vrf_ = Agent::GetInstance()->vrf_table()->
                FindVrfFromName(Agent::GetInstance()->fabric_vrf_name());
    dest_vrf = out->vrf_->vrf_id();

    // Set NAT flow fields
    linklocal_flow = true;
    nat_done = true;
    if (nat_server == Agent::GetInstance()->router_id()) {
        // In case of metadata or when link local destination is local host,
        // set VM's metadata address as NAT source address. This is required
        // to avoid response from the linklocal service being looped back and
        // the packet not coming to vrouter for reverse NAT.
        // Destination would be local host (FindLinkLocalService returns this)
        nat_ip_saddr = vm_port->mdata_ip_addr();
        nat_sport = pkt->sport;
    } else {
        nat_ip_saddr = Agent::GetInstance()->router_id();
        // we bind to a local port & use it as NAT source port (cannot use
        // incoming src port); init here and bind in Add;
        nat_sport = 0;
        linklocal_bind_local_port = true;
    }
    nat_ip_daddr = nat_server;
    nat_dport = nat_port;

    nat_vrf = dest_vrf;
    nat_dest_vrf = vm_port->vrf_id();

    out->rt_ = Agent::GetInstance()->pkt()->flow_table()->GetUcRoute(out->vrf_,
            nat_server);
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

    if ((pkt->ip_daddr.to_v4().to_ulong() != vm_port->mdata_ip_addr().to_ulong()) ||
        (pkt->ip_saddr.to_v4().to_ulong() != Agent::GetInstance()->router_id().to_ulong())) {
        // Force implicit deny
        in->rt_ = NULL;
        out->rt_ = NULL;
        return;
    }

    dest_vrf = vm_port->vrf_id();
    out->vrf_ = vm_port->vrf();

    linklocal_flow = true;
    nat_done = true;
    nat_ip_saddr = Ip4Address(METADATA_IP_ADDR);
    nat_ip_daddr = vm_port->ip_addr();
    nat_dport = pkt->dport;
    if (pkt->sport == Agent::GetInstance()->metadata_server_port()) {
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
    if (pkt->ip_daddr.to_v4() == vm_port->ip_addr()) {
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

    FlowTable *ftable = Agent::GetInstance()->pkt()->flow_table();
    in->vn_ = NULL;
    if (nat_done == false) {
        in->rt_ = ftable->GetUcRoute(it->vrf_.get(), pkt->ip_saddr);
        nat_dest_vrf = it->vrf_.get()->vrf_id();
    }
    out->rt_ = ftable->GetUcRoute(it->vrf_.get(), pkt->ip_daddr);
    out->vn_ = it->vn_.get();
    dest_vrf = out->intf_->vrf()->vrf_id();

    // Translate the Dest-IP
    if (nat_done == false)
        nat_ip_saddr = pkt->ip_saddr;
    nat_ip_daddr = vm_port->ip_addr();
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
    FlowTable *ftable = Agent::GetInstance()->pkt()->flow_table();
    const AgentRoute *rt = out->rt_;
    bool change = false;
    // Find Floating-IP matching destination-ip
    for ( ; it != fip_list.end(); ++it) {
        if (it->vrf_.get() == NULL) {
            continue;
        }

        const AgentRoute *rt_match = ftable->GetUcRoute(it->vrf_.get(),
                pkt->ip_daddr);
        if (rt_match == NULL) {
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
            out->rt_ = rt_match;
            fip_it = it;
            change = false;
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
    in->rt_ = ftable->GetUcRoute(fip_it->vrf_.get(), fip_it->floating_ip_);
    if (in->rt_ == NULL) {
        return;
    }

    if (in->rt_ && out->rt_) {
        VrfTranslate(pkt, in, out);
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
    nat_dest_vrf = intf->vrf_id();

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

void PktFlowInfo::VrfTranslate(const PktInfo *pkt, PktControlInfo *in,
                               PktControlInfo *out) {
    const Interface *intf = NULL;
    if (ingress) {
        intf = in->intf_;
    } else {
        intf = out->intf_;
    }
    if (!intf || intf->type() != Interface::VM_INTERFACE) {
        return;
    }

    const VmInterface *vm_intf = static_cast<const VmInterface *>(intf);
    //If interface has a VRF assign rule, choose the acl and match the
    //packet, else get the acl attached to VN and try matching the packet to
    //network acl
    const AclDBEntry *acl = vm_intf->vrf_assign_acl();
    if (acl == NULL) {
        if (ingress && in->vn_) {
            //Check if the network ACL is present
            acl = in->vn_->GetAcl();
        } else if (out->vn_) {
            acl = out->vn_->GetAcl();
        }
    }

    if (!acl) {
        return;
    }

    PacketHeader hdr;
    hdr.vrf = pkt->vrf;
    hdr.src_ip = pkt->ip_saddr;
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
        return;
    }

    if (match_acl_param.action_info.vrf_translate_action_.vrf_name() != "") {
        VrfKey key(match_acl_param.action_info.vrf_translate_action_.vrf_name());
        const VrfEntry *vrf = static_cast<const VrfEntry*>
            (Agent::GetInstance()->vrf_table()->FindActiveEntry(&key));
        out->vrf_ = vrf;
        if (vrf) {
            out->rt_ = vrf->GetUcRoute(pkt->ip_daddr.to_v4());
            if (vm_intf->vrf_assign_acl()) {
                in->rt_ = vrf->GetUcRoute(pkt->ip_saddr.to_v4());
            }
        }
    }
}

void PktFlowInfo::IngressProcess(const PktInfo *pkt, PktControlInfo *in,
                                 PktControlInfo *out) {
    // Flow packets are expected only on VMPort interfaces
    if (in->intf_->type() != Interface::VM_INTERFACE &&
        in->intf_->type() != Interface::INET) {
        LogError(pkt, "Unexpected packet on Non-VM interface");
        return;
    }

    // We always expect route for source-ip for ingress flows.
    // If route not present, return from here so that a short flow is added
    FlowTable *ftable = Agent::GetInstance()->pkt()->flow_table();
    in->rt_ = ftable->GetUcRoute(in->vrf_, pkt->ip_saddr);
    in->vn_ = InterfaceToVn(in->intf_);

    // Compute Out-VRF and Route for dest-ip
    out->vrf_ = in->vrf_;
    out->rt_ = ftable->GetUcRoute(out->vrf_, pkt->ip_daddr);
    //Native VRF of the interface and acl assigned vrf would have
    //exact same route with different nexthop, hence if both ingress
    //route and egress route are present in native vrf, acl match condition
    //can be applied
    if (in->rt_ && out->rt_) {
        VrfTranslate(pkt, in, out);
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

    if (RouteAllowNatLookup(out->rt_)) {
        // If interface has floating IP, check if we have more specific route in
        // public VN (floating IP)
        if (IntfHasFloatingIp(in->intf_, pkt->family)) {
            FloatingIpSNat(pkt, in, out);
        }
    }
    
    if (out->rt_ != NULL) {
        // Route is present. If IP-DA is a floating-ip, we need DNAT
        if (RouteToOutInfo(out->rt_, pkt, this, in, out)) {
            if (out->intf_ && IntfHasFloatingIp(out->intf_, pkt->family)) {
                FloatingIpDNat(pkt, in, out);
            }
        }
    }

    // Packets needing linklocal service will have route added by LinkLocal peer
    if ((in->rt_ && IsLinkLocalRoute(in->rt_)) || 
        (out->rt_ && IsLinkLocalRoute(out->rt_))) {
        LinkLocalServiceTranslate(pkt, in, out);
    }

    // If out-interface was not found, get it based on out-route
    if (out->intf_ == NULL && out->rt_) {
        RouteToOutInfo(out->rt_, pkt, this, in, out);
    }

    if (out->rt_) {
        const NextHop* nh = out->rt_->GetActiveNextHop();
        if (nh && nh->GetType() == NextHop::TUNNEL) {
            const TunnelNH* tunnel_nh = static_cast<const TunnelNH *>(nh);
            const Ip4Address *ip = tunnel_nh->GetDip();
            if (ip) {
                peer_vrouter = ip->to_string();
                tunnel_type = tunnel_nh->GetTunnelType();
            }
        } else {
            peer_vrouter = flow_table->agent()->router_id().to_string();
        }
    }
    return;
}

void PktFlowInfo::EgressProcess(const PktInfo *pkt, PktControlInfo *in,
                                PktControlInfo *out) {
    peer_vrouter = Ip4Address(pkt->tunnel.ip_saddr).to_string();
    tunnel_type = pkt->tunnel.type;
    MplsLabel *mpls = Agent::GetInstance()->mpls_table()->FindMplsLabel(pkt->tunnel.label);
    if (mpls == NULL) {
        LogError(pkt, "Invalid Label in egress flow");
        return;
    }

    // Get VMPort Interface from NH
    if (NhDecode(mpls->nexthop(), pkt, this, in, out, true) == false) {
        return;
    }

    FlowTable *ftable = Agent::GetInstance()->pkt()->flow_table();
    out->rt_ = ftable->GetUcRoute(out->vrf_, pkt->ip_daddr);
    in->rt_ = ftable->GetUcRoute(out->vrf_, pkt->ip_saddr);
    if (out->intf_) {
        out->vn_ = InterfaceToVn(out->intf_);
    }

    //Apply vrf translate ACL to get ingress route
    if (in->rt_ && out->rt_) {
        VrfTranslate(pkt, in, out);
    }

    if (RouteAllowNatLookup(out->rt_)) {
        // If interface has floating IP, check if destination is one of the
        // configured floating IP.
        if (IntfHasFloatingIp(out->intf_, pkt->family)) {
            FloatingIpDNat(pkt, in, out);
        }
    }

    return;
}

bool PktFlowInfo::Process(const PktInfo *pkt, PktControlInfo *in,
                          PktControlInfo *out) {
    if (pkt->agent_hdr.cmd == AgentHdr::TRAP_ECMP_RESOLVE) {
        RewritePktInfo(pkt->agent_hdr.cmd_param);
    }

    in->intf_ = InterfaceTable::GetInstance()->FindInterface(pkt->agent_hdr.ifindex);
    out->nh_ = in->nh_ = pkt->agent_hdr.nh;
    if (in->intf_ == NULL || in->intf_->ip_active(pkt->family) == false) {
        in->intf_ = NULL;
        LogError(pkt, "Invalid or Inactive ifindex");
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_UNAVIALABLE_INTERFACE;
        return false;
    }

    if (in->intf_->type() == Interface::VM_INTERFACE) {
        const VmInterface *vm_intf = 
            static_cast<const VmInterface *>(in->intf_);
        if (!vm_intf->layer3_forwarding()) {
            LogError(pkt, "ipv4 service not enabled for ifindex");
            short_flow = true;
            short_flow_reason = FlowEntry::SHORT_IPV4_FWD_DIS;
            return false;
        }
    }

    in->vrf_ = Agent::GetInstance()->vrf_table()->FindVrfFromId(pkt->agent_hdr.vrf);
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
        SetInEcmpIndex(pkt, this, in, out);
    }

    if (ecmp == true && nat_done == false) {
        SetEcmpFlowInfo(pkt, in, out);
    }

    return true;
}

void PktFlowInfo::Add(const PktInfo *pkt, PktControlInfo *in,
                      PktControlInfo *out) {
    FlowKey key(in->nh_, pkt->ip_saddr, pkt->ip_daddr, pkt->ip_proto,
                pkt->sport, pkt->dport);
    FlowEntryPtr flow(Agent::GetInstance()->pkt()->flow_table()->Allocate(key));

    if (pkt->family == Address::INET) {
        Ip4Address v4_src = pkt->ip_saddr.to_v4();
        if (ingress && !short_flow && !linklocal_flow) {
            if (in->rt_->WaitForTraffic()) {
                flow_table->agent()->oper_db()->route_preference_module()->
                    EnqueueTrafficSeen(v4_src, 32, in->intf_->id(), pkt->vrf);
            }
        }
    } else if (pkt->family == Address::INET6) {
        //TODO:: Handle Ipv6 changes
    }
    // Do not allow more than max flows
    if ((in->vm_ &&
         (flow_table->VmFlowCount(in->vm_) + 2) > flow_table->max_vm_flows()) ||
        (out->vm_ &&
         (flow_table->VmFlowCount(out->vm_) + 2) > flow_table->max_vm_flows())) {
        flow_table->agent()->stats()->incr_flow_drop_due_to_max_limit();
        short_flow = true;
        short_flow_reason = FlowEntry::SHORT_FLOW_LIMIT;
    }

    if (!short_flow && linklocal_bind_local_port &&
        flow->linklocal_src_port_fd() == PktFlowInfo::kLinkLocalInvalidFd) {
        nat_sport = LinkLocalBindPort(in->vm_, pkt->ip_proto);
        if (!nat_sport) {
            flow_table->agent()->stats()->incr_flow_drop_due_to_max_limit();
            short_flow = true;
            short_flow_reason = FlowEntry::SHORT_LINKLOCAL_SRC_NAT;
        }
    }
    
    // In case the packet is for a reverse flow of a linklocal flow,
    // link to that flow (avoid creating a new reverse flow entry for the case)
    FlowEntryPtr rflow = flow->reverse_flow_entry();
    if (rflow && rflow->is_flags_set(FlowEntry::LinkLocalBindLocalSrcPort)) {
        return;
    }

    uint16_t r_sport;
    uint16_t r_dport;
    if (pkt->ip_proto == IPPROTO_ICMP) {
        r_sport = pkt->sport;
        r_dport = pkt->dport;
    } else if (nat_done) {
        r_sport = nat_dport;
        r_dport = nat_sport;
    } else {
        r_sport = pkt->dport;
        r_dport = pkt->sport;
    }

    if (nat_done) {
        FlowKey rkey(out->nh_, nat_ip_daddr, nat_ip_saddr, pkt->ip_proto,
                     r_sport, r_dport);
        rflow = Agent::GetInstance()->pkt()->flow_table()->Allocate(rkey);
    } else {
        FlowKey rkey(out->nh_, pkt->ip_daddr, pkt->ip_saddr, pkt->ip_proto,
                     r_sport, r_dport);
        rflow = Agent::GetInstance()->pkt()->flow_table()->Allocate(rkey);
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
    //      No change in forward/reverse flow. Continue with flow as forward-flow
    bool swap_flows = false;
    if (flow->is_flags_set(FlowEntry::ReverseFlow) &&
        !rflow->is_flags_set(FlowEntry::ReverseFlow)) {
        swap_flows = true;
    }

    tcp_ack = pkt->tcp_ack;
    flow->InitFwdFlow(this, pkt, in, out);
    rflow->InitRevFlow(this, out, in);

    /* Fip stats info in not updated in InitFwdFlow and InitRevFlow because
     * both forward and reverse flows are not not linked to each other yet.
     * We need both forward and reverse flows to update Fip stats info */
    UpdateFipStatsInfo(flow.get(), rflow.get(), pkt, in, out);
    if (swap_flows) {
        Agent::GetInstance()->pkt()->flow_table()->Add(rflow.get(), flow.get());
    } else {
        Agent::GetInstance()->pkt()->flow_table()->Add(flow.get(), rflow.get());
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
    if (fip_snat && fip_dnat) {
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
        flow->UpdateFipStatsInfo(fip, intf_id);
        rflow->UpdateFipStatsInfo(r_fip, r_intf_id);
    }
}

//If a packet is trapped for ecmp resolve, dp might have already
//overwritten original packet(NAT case), hence get actual packet by
//overwritting packet with data in flow entry.
void PktFlowInfo::RewritePktInfo(uint32_t flow_index) {

    std::ostringstream ostr;
    ostr << "ECMP Resolve for flow index " << flow_index;
    PKTFLOW_TRACE(Err,ostr.str());
    FlowTableKSyncObject *obj = 
        Agent::GetInstance()->ksync()->flowtable_ksync_obj();

    FlowKey key;
    if (!obj->GetFlowKey(flow_index, &key)) {
        std::ostringstream ostr;
        ostr << "ECMP Resolve: unable to find flow index " << flow_index;
        PKTFLOW_TRACE(Err,ostr.str());
        return;
    }

    FlowEntry *flow = Agent::GetInstance()->pkt()->flow_table()->Find(key);
    if (!flow) {
        std::ostringstream ostr;  
        ostr << "ECMP Resolve: unable to find flow index " << flow_index;
        PKTFLOW_TRACE(Err,ostr.str());
        return;
    }

    pkt->ip_saddr = key.src_addr;
    pkt->ip_daddr = key.dst_addr;
    pkt->ip_proto = key.protocol;
    pkt->sport = key.src_port;
    pkt->dport = key.dst_port;
    pkt->agent_hdr.vrf = flow->data().vrf;
    pkt->agent_hdr.nh = key.nh;
    //Flow transition from Non ECMP to ECMP, use index 0
    if (flow->data().component_nh_idx == CompositeNH::kInvalidComponentNHIdx) {
        out_component_nh_idx = 0;
    } else {
        out_component_nh_idx = flow->data().component_nh_idx;
    }
    return;
}
