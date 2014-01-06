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
#include "oper/agent_route.h"
#include "oper/vrf.h"
#include "oper/sg.h"
#include "oper/global_vrouter.h"
#include "oper/operdb_init.h"

#include "filter/packet_header.h"
#include "filter/acl.h"

#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "pkt/pkt_handler.h"
#include "pkt/flow_table.h"
#include "pkt/flow_proto.h"
#include "pkt/pkt_sandesh_flow.h"
#include "ksync/flowtable_ksync.h"

static void LogError(const PktInfo *pkt, const char *str) {
    FLOW_TRACE(DetailErr, pkt->agent_hdr.cmd_param, pkt->agent_hdr.ifindex,
               pkt->agent_hdr.vrf, pkt->ip_saddr, pkt->ip_daddr, str);
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
        vrf = (static_cast<const CompositeNH *>(nh))->GetVrf();
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

    return vrf->GetVrfId();
}

// Get interface from a NH. Also, decode ECMP information from NH
static bool NhDecode(const NextHop *nh, const PktInfo *pkt, PktFlowInfo *info,
                     PktControlInfo *out, bool force_vmport) {
    bool ret = true;

    if (!nh->IsActive())
        return false;

    // If its composite NH, find interface information from the component NH
    if (nh->GetType() == NextHop::COMPOSITE) {
        info->ecmp = true;
        const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
        if (info->out_component_nh_idx == CompositeNH::kInvalidComponentNHIdx ||
           (comp_nh->GetNH(info->out_component_nh_idx) == NULL)) {
            if (info->out_component_nh_idx != CompositeNH::kInvalidComponentNHIdx) {
                //Dont trap reverse flow, upon flow establishment
                //To be removed once trapped packets are retransmitted
                info->trap_rev_flow = true;
            } 

            info->out_component_nh_idx = 
                comp_nh->GetComponentNHList()->hash(pkt->hash());
        }
        nh = comp_nh->GetNH(info->out_component_nh_idx);
        if (nh->IsActive() == false) {
            return false;
        }
    } else {
        info->out_component_nh_idx = CompositeNH::kInvalidComponentNHIdx;
    }

    switch (nh->GetType()) {
    case NextHop::INTERFACE:
        out->intf_ = static_cast<const InterfaceNH*>(nh)->GetInterface();
        out->vrf_ = static_cast<const InterfaceNH*>(nh)->GetVrf();
        break;

    case NextHop::RECEIVE:
        out->intf_ = static_cast<const ReceiveNH *>(nh)->GetInterface();
        out->vrf_ = out->intf_->vrf();
        break;

    case NextHop::VLAN: {
        const VlanNH *vlan_nh = static_cast<const VlanNH*>(nh);
        out->intf_ = vlan_nh->GetInterface();
        out->vlan_nh_ = true;
        out->vlan_tag_ = vlan_nh->GetVlanTag();
        out->vrf_ = vlan_nh->GetVrf();
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
static bool RouteToOutInfo(const Inet4UnicastRouteEntry *rt, const PktInfo *pkt,
                           PktFlowInfo *info, PktControlInfo *out) {
    const AgentPath *path = rt->GetActivePath();
    if (path == NULL)
        return false;

    const NextHop *nh = static_cast<const NextHop *>(path->GetNextHop());
    if (nh == NULL)
        return false;

    if (nh->IsActive() == false) {
        return false;
    }

    return NhDecode(nh, pkt, info, out, false);
}

static const VnEntry *InterfaceToVn(const Interface *intf) {
    if (intf->type() != Interface::VM_INTERFACE)
        return NULL;

    const VmInterface *vm_port = static_cast<const VmInterface *>(intf);
    return vm_port->vn();
}

static bool IntfHasFloatingIp(const Interface *intf) {
    if (intf->type() != Interface::VM_INTERFACE)
        return NULL;

    const VmInterface *vm_port = static_cast<const VmInterface *>(intf);
    return vm_port->HasFloatingIp();
}

static bool IsLinkLocalRoute(const Inet4UnicastRouteEntry *rt) {
    const AgentPath *path = rt->GetActivePath();
    if (path && path->GetPeer() == Agent::GetInstance()->GetLinkLocalPeer())
        return true;

    return false;
}

static const string *RouteToVn(const Inet4UnicastRouteEntry *rt) {
    const AgentPath *path = NULL;
    if (rt) {
        path = rt->GetActivePath();
    }
    if (path == NULL) {
        return NULL;
    }

    return &path->GetDestVnName();
}

static void SetInEcmpIndex(const PktInfo *pkt, PktFlowInfo *flow_info,
                           PktControlInfo *in, PktControlInfo *out) {
    if (!in->rt_) {
        return;
    }

    if (in->rt_->GetActiveNextHop()->GetType() != NextHop::COMPOSITE) {
        return;
    }

    NextHop *component_nh_ptr = NULL;
    uint32_t label;
    //Frame key for component NH
    if (flow_info->ingress) {
        //Ingress flow
        const VmInterface *vm_port = 
            static_cast<const VmInterface *>(in->intf_);
        const VrfEntry *vrf = 
            Agent::GetInstance()->GetVrfTable()->FindVrfFromId(pkt->vrf);
        if (vm_port->HasServiceVlan() && vm_port->vrf() != vrf) {
            //Packet came on service VRF
            label = vm_port->GetServiceVlanLabel(vrf);
            uint32_t vlan = vm_port->GetServiceVlanTag(vrf);

            VlanNHKey key(vm_port->GetUuid(), vlan);
            component_nh_ptr =
                static_cast<NextHop *>
                (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&key));
        } else {
            InterfaceNHKey key(static_cast<InterfaceKey *>(vm_port->GetDBRequestKey().release()),
                               false, InterfaceNHFlags::INET4);
            component_nh_ptr =
                static_cast<NextHop *>
                (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&key));
            label = vm_port->label();
        }
    } else {
        //Packet from fabric
        Ip4Address dest_ip(pkt->tunnel.ip_saddr);
        TunnelNHKey key(Agent::GetInstance()->GetDefaultVrf(), 
                        Agent::GetInstance()->GetRouterId(), dest_ip,
                        false, pkt->tunnel.type);
        //Get component NH pointer
        component_nh_ptr =
            static_cast<NextHop *>(Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&key));
        //Get Label to be used to reach destination server
        const CompositeNH *nh = 
            static_cast<const CompositeNH *>(in->rt_->GetActiveNextHop());
        label = nh->GetRemoteLabel(dest_ip);
    }
    ComponentNH component_nh(label, component_nh_ptr);

    const NextHop *nh = NULL;
    if (out->intf_) {
        //Local destination, use active path
        nh = in->rt_->GetActiveNextHop();
    } else {
        //Destination on remote server
        //Choose local path, which will also pointed by MPLS label
        if (in->rt_->FindPath(Agent::GetInstance()->GetLocalVmPeer())) {
            nh = in->rt_->FindPath(Agent::GetInstance()->GetLocalVmPeer())->GetNextHop();
        } else {
            const CompositeNH *comp_nh = static_cast<const CompositeNH *>
                (in->rt_->GetActiveNextHop());
            //Aggregarated routes may not have local path
            //Derive local path
            nh = comp_nh->GetLocalCompositeNH();
        }
    }

    if (nh && nh->GetType() == NextHop::COMPOSITE) {
        const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
        //Find component entry index in composite NH
        uint32_t idx;
        if (comp_nh->GetComponentNHList()->Find(component_nh, idx)) {
            flow_info->in_component_nh_idx = idx;
            flow_info->ecmp = true;
        }
    } else {
        //Ideally this case is not ecmp, as on reverse flow we are hitting 
        //a interface NH and not composite NH, install reverse flow for consistency
        flow_info->ecmp = true;
    }
}

void PktFlowInfo::SetEcmpFlowInfo(const PktInfo *pkt, const PktControlInfo *in,
                                  const PktControlInfo *out) {
    nat_ip_daddr = pkt->ip_daddr;
    nat_ip_saddr = pkt->ip_saddr;
    nat_dport = pkt->dport;
    nat_sport = pkt->sport;
    if (out->intf_ && out->intf_->type() == Interface::VM_INTERFACE) {
        dest_vrf = out->vrf_->GetVrfId();
    } else {
        dest_vrf = pkt->vrf;
    }
    nat_vrf = dest_vrf;
    nat_dest_vrf = pkt->vrf;
}

void PktFlowInfo::LinkLocalServiceFromVm(const PktInfo *pkt, PktControlInfo *in,
                                         PktControlInfo *out) {
    const VmInterface *vm_port = 
        static_cast<const VmInterface *>(in->intf_);

    uint16_t nat_port;
    Ip4Address nat_server;
    std::string service_name;
    if (!Agent::GetInstance()->oper_db()->global_vrouter()->
        FindLinkLocalService(Ip4Address(pkt->ip_daddr), pkt->dport,
                             &service_name, &nat_server, &nat_port)) {
        // link local service not configured, drop the request
        in->rt_ = NULL; 
        out->rt_ = NULL; 
        return; 
    }

    out->vrf_ = Agent::GetInstance()->GetVrfTable()->
                FindVrfFromName(Agent::GetInstance()->GetDefaultVrf());
    dest_vrf = out->vrf_->GetVrfId();

    // Set NAT flow fields
    linklocal_flow = true;
    nat_done = true;
    if (nat_server == Agent::GetInstance()->GetRouterId()) {
        // In case of metadata or when link local destination is local host,
        // set VM's metadata address as NAT source address. This is required
        // to avoid response from the linklocal service being looped back and
        // the packet not coming to vrouter for reverse NAT.
        // Destination would be local host (FindLinkLocalService returns this)
        nat_ip_saddr = vm_port->mdata_ip_addr().to_ulong();
    } else {
        nat_ip_saddr = Agent::GetInstance()->GetRouterId().to_ulong();
    }
    nat_ip_daddr = nat_server.to_ulong();
    nat_sport = pkt->sport;
    nat_dport = nat_port;

    nat_vrf = dest_vrf;
    nat_dest_vrf = vm_port->GetVrfId();

    out->rt_ = out->vrf_->GetUcRoute(nat_server);
    return;
}

void PktFlowInfo::LinkLocalServiceFromHost(const PktInfo *pkt, PktControlInfo *in,
                                           PktControlInfo *out) {
    if (RouteToOutInfo(out->rt_, pkt, this, out) == false) {
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

    if ((pkt->ip_daddr != vm_port->mdata_ip_addr().to_ulong()) ||
        (pkt->ip_saddr != Agent::GetInstance()->GetRouterId().to_ulong())) {
        // Force implicit deny
        in->rt_ = NULL;
        out->rt_ = NULL;
        return;
    }

    dest_vrf = vm_port->GetVrfId();
    out->vrf_ = vm_port->vrf();

    linklocal_flow = true;
    nat_done = true;
    nat_ip_saddr = METADATA_IP_ADDR;
    nat_ip_daddr = vm_port->ip_addr().to_ulong();
    nat_dport = pkt->dport;
    if (pkt->sport == Agent::GetInstance()->GetMetadataServerPort()) {
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
    if (pkt->ip_daddr == vm_port->ip_addr().to_ulong()) {
        return;
    }

    // Look for matching floating-ip
    VmInterface::FloatingIpSet::const_iterator it = fip_list.begin();
    for ( ; it != fip_list.end(); ++it) {

        if (it->vrf_.get() == NULL) {
            continue;
        }

        if (pkt->ip_daddr == it->floating_ip_.to_ulong()) {
            break;
        }
    }

    if (it == fip_list.end()) {
        // No matching floating ip for destination-ip
        return;
    }

    in->vn_ = NULL;
    if (nat_done == false) {
        in->rt_ = it->vrf_.get()->GetUcRoute(Ip4Address(pkt->ip_saddr));
        nat_dest_vrf = it->vrf_.get()->GetVrfId();
    }
    out->rt_ = it->vrf_.get()->GetUcRoute(Ip4Address(pkt->ip_daddr));
    out->vn_ = it->vn_.get();
    dest_vn = &(it->vn_.get()->GetName());
    dest_vrf = out->intf_->vrf()->GetVrfId();

    // Translate the Dest-IP
    nat_done = true;
    nat_ip_saddr = pkt->ip_saddr;
    nat_ip_daddr = vm_port->ip_addr().to_ulong();
    nat_sport = pkt->sport;
    nat_dport = pkt->dport;
    nat_vrf = dest_vrf;

    if (in->rt_) {
        flow_source_vrf = static_cast<const RouteEntry *>(in->rt_)->GetVrfId();
    } else {
        flow_source_vrf = VrfEntry::kInvalidIndex;
    }
    flow_dest_vrf = it->vrf_.get()->GetVrfId();

    return;
}

void PktFlowInfo::FloatingIpSNat(const PktInfo *pkt, PktControlInfo *in,
                                 PktControlInfo *out) {
    const VmInterface *intf = 
        static_cast<const VmInterface *>(in->intf_);
    const VmInterface::FloatingIpSet &fip_list = intf->floating_ip_list().list_;
    VmInterface::FloatingIpSet::const_iterator it = fip_list.begin();
    // Find Floating-IP matching destination-ip
    for ( ; it != fip_list.end(); ++it) {
        if (it->vrf_.get() == NULL) {
            continue;
        }

        out->rt_ = it->vrf_.get()->GetUcRoute(Ip4Address(pkt->ip_daddr));
        if (out->rt_ != NULL) {
            break;
        }
    }

    if (out->rt_ == NULL) {
        // No floating-ip found
        return;
    }

    // Compute out-intf and ECMP info from out-route
    if (RouteToOutInfo(out->rt_, pkt, this, out) == false) {
        return;
    }

    // Floating-ip found. We will change src-ip to floating-ip. Recompute route
    // for new source-ip. All policy decisions will be based on this new route
    in->rt_ = it->vrf_.get()->GetUcRoute(it->floating_ip_);
    if (in->rt_ == NULL) {
        return;
    }

    dest_vrf = it->vrf_.get()->GetVrfId();
    in->vn_ = it->vn_.get();
    // Source-VN for policy processing is based on floating-ip VN
    // Dest-VN will be based on out->rt_ and computed below
    source_vn = &(it->vn_.get()->GetName());

    // Setup reverse flow to translate sip.
    nat_done = true;
    nat_ip_saddr = it->floating_ip_.to_ulong();
    nat_ip_daddr = pkt->ip_daddr;
    nat_sport = pkt->sport;
    nat_dport = pkt->dport;

    // Compute VRF for reverse flow
    if (out->intf_) {
        // Egress-vm present on same compute node, take VRF from vm-port
        nat_vrf = out->vrf_->GetVrfId();
        out->vn_ = InterfaceToVn(out->intf_);
    } else {
        // Egress-vm is remote. Find VRF from the NH for source-ip
        nat_vrf = NhToVrf(in->rt_->GetActiveNextHop());
    }

    // Dest VRF for reverse flow is In-Port VRF
    nat_dest_vrf = intf->GetVrfId();

    flow_source_vrf = pkt->vrf;
    if (out->rt_) {
        flow_dest_vrf = dest_vrf;
    } else {
        flow_dest_vrf = VrfEntry::kInvalidIndex;
    }
    return;
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
    in->rt_ = in->vrf_->GetUcRoute(Ip4Address(pkt->ip_saddr));
    in->vn_ = InterfaceToVn(in->intf_);

    // Compute Out-VRF and Route for dest-ip
    out->vrf_ = in->vrf_;
    out->rt_ = out->vrf_->GetUcRoute(Ip4Address(pkt->ip_daddr));
    if (out->rt_) {
        // Compute out-intf and ECMP info from out-route
        if (RouteToOutInfo(out->rt_, pkt, this, out)) {
            if (out->intf_) {
                out->vn_ = InterfaceToVn(out->intf_);
                if (out->vrf_) {
                    dest_vrf = out->vrf_->GetVrfId();
                }
            }
        }
    }

    // If no route for DA and floating-ip configured try floating-ip SNAT
    if (out->rt_ == NULL) {
        if (IntfHasFloatingIp(in->intf_)) {
            FloatingIpSNat(pkt, in, out);
        }
    } 
    
    if (out->rt_ != NULL) {
        // Route is present. If IP-DA is a floating-ip, we need DNAT
        if (RouteToOutInfo(out->rt_, pkt, this, out)) {
            if (out->intf_ && IntfHasFloatingIp(out->intf_)) {
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
        RouteToOutInfo(out->rt_, pkt, this, out);
    }

    return;
}

void PktFlowInfo::EgressProcess(const PktInfo *pkt, PktControlInfo *in,
                                PktControlInfo *out) {
    MplsLabel *mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(pkt->tunnel.label);
    if (mpls == NULL) {
        LogError(pkt, "Invalid Label in egress flow");
        return;
    }

    // Get VMPort Interface from NH
    if (NhDecode(mpls->GetNextHop(), pkt, this, out, true) == false) {
        return;
    }

    out->rt_ = out->vrf_->GetUcRoute(Ip4Address(pkt->ip_daddr));
    in->rt_ = out->vrf_->GetUcRoute(Ip4Address(pkt->ip_saddr));
    if (out->intf_) {
        out->vn_ = InterfaceToVn(out->intf_);
    }

    // If no route for DA and floating-ip configured try floating-ip DNAT
    if (out->rt_ == NULL) {
        if (IntfHasFloatingIp(out->intf_)) {
            FloatingIpDNat(pkt, in, out);
        }
    }

    return;
}

bool PktFlowInfo::Process(const PktInfo *pkt, PktControlInfo *in,
                          PktControlInfo *out) {
    if (pkt->agent_hdr.cmd == AGENT_TRAP_ECMP_RESOLVE) {
        RewritePktInfo(pkt->agent_hdr.cmd_param);
    }

    in->intf_ = InterfaceTable::GetInstance()->FindInterface(pkt->agent_hdr.ifindex);
    if (in->intf_ == NULL || in->intf_->active() == false) {
        LogError(pkt, "Invalid or Inactive ifindex");
        return false;
    }

    if (in->intf_->type() == Interface::VM_INTERFACE) {
        const VmInterface *vm_intf = 
            static_cast<const VmInterface *>(in->intf_);
        if (!vm_intf->ipv4_forwarding()) {
            LogError(pkt, "ipv4 service not enabled for ifindex");
            return false;
        }
    }

    in->vrf_ = Agent::GetInstance()->GetVrfTable()->FindVrfFromId(pkt->agent_hdr.vrf);
    if (in->vrf_ == NULL || !in->vrf_->IsActive()) {
        LogError(pkt, "Invalid or Inactive VRF");
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
        return false;
    }

    if (out->rt_ == NULL) {
        LogError(pkt, "Flow : No route for Dst-IP");
        return false;
    }

    if (source_vn == NULL) {
        source_vn = RouteToVn(in->rt_);
    }

    if (dest_vn == NULL) {
        dest_vn = RouteToVn(out->rt_);
    }

    flow_source_vrf = static_cast<const RouteEntry *>(in->rt_)->GetVrfId();
    flow_dest_vrf = out->rt_->GetVrfId();

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
    FlowKey key(pkt->vrf, pkt->ip_saddr, pkt->ip_daddr,
                pkt->ip_proto, pkt->sport, pkt->dport);
    FlowEntryPtr flow(Agent::GetInstance()->pkt()->flow_table()->Allocate(key));

    FlowEntryPtr rflow(NULL);
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
        FlowKey rkey(nat_vrf, nat_ip_daddr, nat_ip_saddr,
                     pkt->ip_proto, r_sport, r_dport);
        rflow = Agent::GetInstance()->pkt()->flow_table()->Allocate(rkey);
    } else {
        FlowKey rkey(dest_vrf, pkt->ip_daddr, pkt->ip_saddr,
                     pkt->ip_proto, r_sport, r_dport);
        rflow = Agent::GetInstance()->pkt()->flow_table()->Allocate(rkey);
    }

    InitFwdFlow(flow.get(), pkt, in, out);
    InitRevFlow(rflow.get(), pkt, out, in);

    Agent::GetInstance()->pkt()->flow_table()->Add(flow.get(), rflow.get());
}

//If a packet is trapped for ecmp resolve, dp might have already
//overwritten original packet(NAT case), hence get actual packet by
//overwritting packet with data in flow entry.
void PktFlowInfo::RewritePktInfo(uint32_t flow_index) {

    std::ostringstream ostr;
    ostr << "ECMP Resolve for flow index " << flow_index;
    PKTFLOW_TRACE(Err,ostr.str());

    FlowKey key;
    if (!FlowTableKSyncObject::GetKSyncObject()->GetFlowKey(flow_index, key)) {
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

    pkt->vrf = key.vrf;
    pkt->ip_saddr = key.src.ipv4;
    pkt->ip_daddr = key.dst.ipv4;
    pkt->ip_proto = key.protocol;
    pkt->sport = key.src_port;
    pkt->dport = key.dst_port;
    pkt->agent_hdr.vrf = key.vrf;
    //Flow transition from Non ECMP to ECMP, use index 0
    if (flow->data.component_nh_idx == CompositeNH::kInvalidComponentNHIdx) {
        out_component_nh_idx = 0;
    } else {
        out_component_nh_idx = flow->data.component_nh_idx;
    }
    return;
}

bool PktFlowInfo::InitFlowCmn(FlowEntry *flow, PktControlInfo *ctrl,
                              PktControlInfo *rev_ctrl) {
    if (flow->stats().last_modified_time) {
        if (flow->nat_flow() != nat_done) {
            flow->MakeShortFlow();
            return false;
        }
    }

    flow->stats().last_modified_time = UTCTimestampUsec();
    flow->set_linklocal_flow(linklocal_flow);
    flow->set_nat_flow(nat_done);
    flow->set_short_flow(short_flow);
    flow->set_local_flow(local_flow);

    flow->data.intf_entry = ctrl->intf_ ? ctrl->intf_ : rev_ctrl->intf_;
    flow->data.vn_entry = ctrl->vn_ ? ctrl->vn_ : rev_ctrl->vn_;
    flow->data.vm_entry = ctrl->vm_ ? ctrl->vm_ : rev_ctrl->vm_;
    SetRpfNH(flow, ctrl);

    return true;
}

void PktFlowInfo::SetRpfNH(FlowEntry *flow, const PktControlInfo *ctrl) {
    if (ctrl->rt_ == NULL) {
        return;
    }
    const NextHop *nh = ctrl->rt_->GetActiveNextHop();
    if (nh->GetType() == NextHop::COMPOSITE && !flow->local_flow() && 
        ctrl->intf_ && ctrl->intf_->type() == Interface::VM_INTERFACE) {
            //Logic for RPF check for ecmp
            //  Get reverse flow, and its corresponding ecmp index
            //  Check if source matches composite nh in reverse flow ecmp index,
            //  if not DP would trap packet for ECMP resolve.
            //  If there is only one instance of ECMP in compute node, then 
            //  RPF NH would only point to local interface NH.
            //  If there are multiple instances of ECMP in local server
            //  then RPF NH would point to local composite NH(containing 
            //  local members only)
        const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
        nh = comp_nh->GetLocalNextHop();
    }

    if (!nh) {
        flow->data.nh_state_ = NULL;
        return;
    }
    flow->data.nh_state_ = static_cast<const NhState *>(
                           nh->GetState(Agent::GetInstance()->GetNextHopTable(),
                           Agent::GetInstance()->pkt()->flow_table()->
                           nh_listener_id()));
}

void PktFlowInfo::InitFwdFlow(FlowEntry *flow, const PktInfo *pkt,
                              PktControlInfo *ctrl, PktControlInfo *rev_ctrl) {
    if (flow->flow_handle() != pkt->GetAgentHdr().cmd_param) {
        if (flow->flow_handle() != FlowEntry::kInvalidFlowHandle) {
            LOG(DEBUG, "Flow index changed from " << flow->flow_handle() 
                << " to " << pkt->GetAgentHdr().cmd_param);
        }
        flow->set_flow_handle(pkt->GetAgentHdr().cmd_param);
    }

    if (InitFlowCmn(flow, ctrl, rev_ctrl) == false) {
        return;
    }
    flow->set_reverse_flow(false);
    flow->stats().intf_in = pkt->GetAgentHdr().ifindex;

    flow->set_ingress(ingress);
    flow->data.source_vn = *(source_vn);
    flow->data.dest_vn = *(dest_vn);
    flow->data.source_sg_id_l = *(source_sg_id_l);
    flow->data.dest_sg_id_l = *(dest_sg_id_l);
    flow->data.flow_source_vrf = flow_source_vrf;
    flow->data.flow_dest_vrf = flow_dest_vrf;
    flow->data.dest_vrf = dest_vrf;
    if (flow->data.vn_entry && flow->data.vn_entry->GetVrf()) {
        flow->data.mirror_vrf = flow->data.vn_entry->GetVrf()->GetVrfId();
    }

    flow->set_ecmp(ecmp);
    flow->data.component_nh_idx = out_component_nh_idx;
    flow->set_trap(false);
    flow->data.source_plen = source_plen;
    flow->data.dest_plen = dest_plen;
}

void PktFlowInfo::InitRevFlow(FlowEntry *flow, const PktInfo *pkt,
                              PktControlInfo *ctrl, PktControlInfo *rev_ctrl) {
    if (InitFlowCmn(flow, ctrl, rev_ctrl) == false) {
        return;
    }
    flow->set_reverse_flow(true);
    if (ctrl->intf_) {
        flow->stats().intf_in = ctrl->intf_->id();
    } else {
        flow->stats().intf_in = Interface::kInvalidIndex;
    }

    // Compute reverse flow fields
    flow->set_ingress(false);
    if (ctrl->intf_) {
        flow->set_ingress(ComputeDirection(ctrl->intf_));
    }
    flow->data.source_vn = *(dest_vn);
    flow->data.dest_vn = *(source_vn);
    flow->data.source_sg_id_l = *(dest_sg_id_l);
    flow->data.dest_sg_id_l = *(source_sg_id_l);
    flow->data.flow_source_vrf = flow_dest_vrf;
    flow->data.flow_dest_vrf = flow_source_vrf;
    flow->data.dest_vrf = nat_dest_vrf;
    if (flow->data.vn_entry && flow->data.vn_entry->GetVrf()) {
        flow->data.mirror_vrf = flow->data.vn_entry->GetVrf()->GetVrfId();
    }
    flow->set_ecmp(ecmp);
    flow->data.component_nh_idx = in_component_nh_idx;
    flow->set_trap(trap_rev_flow);
    flow->data.source_plen = dest_plen;
    flow->data.dest_plen = source_plen;
}
