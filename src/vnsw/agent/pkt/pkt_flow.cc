/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include "route/route.h"

#include "cmn/agent_cmn.h"
#include "oper/interface.h"
#include "oper/nexthop.h"
#include "oper/inet4_ucroute.h"
#include "oper/vrf.h"
#include "oper/sg.h"

#include "filter/packet_header.h"
#include "filter/acl.h"

#include "pkt/proto.h"
#include "pkt/pkt_handler.h"
#include "pkt/flowtable.h"
#include "pkt/pkt_flow.h"
#include "pkt/pkt_sandesh_flow.h"
#include "ksync/flowtable_ksync.h"

SandeshTraceBufferPtr PktFlowTraceBuf(SandeshTraceBufferCreate("FlowHandler", 5000));
template <> Proto<FlowHandler> *Proto<FlowHandler>::instance_ = NULL;

void FlowHandler::Init(boost::asio::io_service &io) {
    Proto<FlowHandler>::Init("Agent::FlowHandler", PktHandler::FLOW, io);
}

void FlowHandler::Shutdown() {
    Proto<FlowHandler>::Shutdown();
}

static void LogError(const PktInfo *pkt, const char *str) {
    FLOW_TRACE(DetailErr, pkt->agent_hdr.cmd_param, pkt->agent_hdr.ifindex,
               pkt->agent_hdr.vrf, pkt->ip_saddr, pkt->ip_daddr, str);
}

static void PktFlowTrace(const struct PktInfo *pkt_info,
                  const PktFlowInfo *flow_info) {
    FlowHandlerFlow finfo;
    //MatchPolicy *policy = flow_info->match_p;
    std::string saddr = Ip4Address(pkt_info->ip_saddr).to_string();
    std::string daddr = Ip4Address(pkt_info->ip_daddr).to_string();
    finfo.set_vrf(pkt_info->vrf);
    finfo.set_src_ip(saddr);
    finfo.set_dst_ip(daddr);
    finfo.set_src_port(pkt_info->sport);
    finfo.set_dst_port(pkt_info->dport);
    finfo.set_proto(pkt_info->ip_proto);
    finfo.set_src_vn(*(flow_info->source_vn));
    finfo.set_src_vrf(flow_info->flow_source_vrf);
    finfo.set_dst_vn(*(flow_info->dest_vn));
    finfo.set_dst_vrf(flow_info->flow_dest_vrf);
    if (flow_info->source_sg_id_l->size()) {
        finfo.set_src_sg_id_l(*(flow_info->source_sg_id_l));
    }
    if (flow_info->dest_sg_id_l) {
        finfo.set_dst_sg_id_l(*(flow_info->dest_sg_id_l));
    }
    //finfo.set_num_acls(policy->m_acl_l.size());
    //finfo.set_num_sgs(policy->m_sg_acl_l.size());
    finfo.set_ingress(flow_info->ingress);
    //finfo.set_implicit_deny(policy->action_info.implicit_deny);
    finfo.set_short_flow(flow_info->short_flow);
    PKTFLOW_TRACE(Trace, "Packet flow tracing:", finfo);
}

// Traffic from IPFabric to VM is treated as EGRESS
// Any other traffic is INGRESS
bool PktFlowInfo::ComputeDirection(const Interface *intf) {
    bool ret = true;
    if (intf->GetType() == Interface::ETH) {
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
            vrf = intf->GetVrf();
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
        if (info->out_component_nh_idx == CompositeNH::kInvalidComponentNHIdx) {
            info->out_component_nh_idx = 
                comp_nh->GetComponentNHList()->hash(pkt->hash());
        }
        nh = comp_nh->GetNH(info->out_component_nh_idx);
        if (nh->IsActive() == false) {
            return false;
        }
    }

    switch (nh->GetType()) {
    case NextHop::INTERFACE:
        out->intf_ = static_cast<const InterfaceNH*>(nh)->GetInterface();
        out->vrf_ = static_cast<const InterfaceNH*>(nh)->GetVrf();
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
        } else if (force_vmport && out->intf_->GetType() != Interface::VMPORT) {
            out->intf_ = NULL;
            ret = false;
        }
    }

    if (out->vrf_ && (out->vrf_->IsActive() == false)) {
        out->vrf_ = NULL;
        ret = false;
    }

    return ret;
}

// Decode route and get Interface / ECMP information
static bool RouteToOutInfo(const Inet4UcRoute *rt, const PktInfo *pkt,
                           PktFlowInfo *info, PktControlInfo *out,
                           bool force_vmport) {
    const AgentPath *path = rt->GetActivePath();
    if (path == NULL)
        return false;

    const NextHop *nh = static_cast<const NextHop *>(path->GetNextHop());
    if (nh == NULL)
        return false;

    if (nh->IsActive() == false) {
        return false;
    }

    return NhDecode(nh, pkt, info, out, force_vmport);
}

static const VnEntry *InterfaceToVn(const Interface *intf) {
    if (intf->GetType() != Interface::VMPORT)
        return NULL;

    const VmPortInterface *vm_port = static_cast<const VmPortInterface *>(intf);
    return vm_port->GetVnEntry();
}

static const VmEntry *InterfaceToVm(const Interface *intf) {
    if (intf->GetType() != Interface::VMPORT)
        return NULL;

    const VmPortInterface *vm_port = static_cast<const VmPortInterface *>(intf);
    return vm_port->GetVmEntry();
}

static bool IntfHasFloatingIp(const Interface *intf) {
    if (intf->GetType() != Interface::VMPORT)
        return NULL;

    const VmPortInterface *vm_port = static_cast<const VmPortInterface *>(intf);
    return vm_port->HasFloatingIp();
}

static bool IsMdataRoute(const Inet4UcRoute *rt) {
    const AgentPath *path = rt->GetActivePath();
    if (path && path->GetPeer() == Agent::GetMdataPeer())
        return true;

    return false;
}

static const string *RouteToVn(const Inet4UcRoute *rt) {
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
        const VmPortInterface *vm_port = 
            static_cast<const VmPortInterface *>(in->intf_);
        const VrfEntry *vrf = 
            Agent::GetVrfTable()->FindVrfFromId(pkt->vrf);
        if (vm_port->HasServiceVlan() && vm_port->GetVrf() != vrf) {
            //Packet came on service VRF
            label = vm_port->GetServiceVlanLabel(vrf);
            uint32_t vlan = vm_port->GetServiceVlanTag(vrf);

            VlanNHKey key(vm_port->GetUuid(), vlan);
            component_nh_ptr =
                static_cast<NextHop *>
                (Agent::GetNextHopTable()->FindActiveEntry(&key));
        } else {
            InterfaceNHKey key(static_cast<InterfaceKey *>(vm_port->GetDBRequestKey().release()),
                               false, false);
            component_nh_ptr =
                static_cast<NextHop *>
                (Agent::GetNextHopTable()->FindActiveEntry(&key));
            label = vm_port->GetLabel();
        }
    } else {
        //Packet from fabric
        Ip4Address dest_ip(pkt->tunnel.ip_saddr);
        TunnelNHKey key(Agent::GetDefaultVrf(), Agent::GetRouterId(), dest_ip,
                        false, pkt->tunnel.type);
        //Get component NH pointer
        component_nh_ptr =
            static_cast<NextHop *>(Agent::GetNextHopTable()->FindActiveEntry(&key));
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
        if (in->rt_->FindPath(Agent::GetLocalVmPeer())) {
            nh = in->rt_->FindPath(Agent::GetLocalVmPeer())->GetNextHop();
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
    if (out->intf_ && out->intf_->GetType() == Interface::VMPORT) {
        dest_vrf = out->vrf_->GetVrfId();
    } else {
        dest_vrf = pkt->vrf;
    }
    nat_vrf = dest_vrf;
    nat_dest_vrf = pkt->vrf;
}

void PktFlowInfo::MdataServiceFromVm(const PktInfo *pkt, PktControlInfo *in,
                                     PktControlInfo *out) {
    const VmPortInterface *vm_port = 
        static_cast<const VmPortInterface *>(in->intf_);
    bool drop = false;

    // Allow metadata request (tcp, port=8775) or ICMP to Mdata IP only
    if (pkt->ip_daddr != METADATA_IP_ADDR) {
        drop = true;
    }

    if (pkt->ip_proto != IPPROTO_TCP && pkt->ip_proto != IPPROTO_ICMP) {
        drop = true;
    }

    if (pkt->ip_proto == IPPROTO_TCP && pkt->dport != METADATA_NAT_PORT) {
        drop = true;
    }

    if (drop) {
        in->rt_ = NULL;
        out->rt_ = NULL;
        return;
    }

    out->vrf_ = Agent::GetVrfTable()->FindVrfFromName(Agent::GetDefaultVrf());
    dest_vrf = out->vrf_->GetVrfId();

    // Set NAT flow fields
    mdata_flow = true;
    nat_done = true;
    nat_ip_saddr = vm_port->GetMdataIpAddr().to_ulong();
    nat_ip_daddr = Agent::GetRouterId().to_ulong();
    if (pkt->ip_proto == IPPROTO_TCP) {
        nat_dport = METADATA_PORT;
    } else {
        nat_dport = pkt->dport;
    }

    nat_sport = pkt->sport;
    nat_vrf = dest_vrf;
    nat_dest_vrf = vm_port->GetVrfId();

    out->rt_ = out->vrf_->GetUcRoute(Ip4Address(nat_ip_daddr));
    return;
}

void PktFlowInfo::MdataServiceFromHost(const PktInfo *pkt, PktControlInfo *in,
                                       PktControlInfo *out) {
    if (RouteToOutInfo(out->rt_, pkt, this, out, false) == false) {
        return;
    }

    const VmPortInterface *vm_port = 
        static_cast<const VmPortInterface *>(out->intf_);
    if (vm_port == NULL) {
        // Force implicit deny
        in->rt_ = NULL;
        out->rt_ = NULL;
        return;
    }

    if ((pkt->ip_daddr != vm_port->GetMdataIpAddr().to_ulong()) ||
        (pkt->ip_saddr != Agent::GetRouterId().to_ulong())) {
        // Force implicit deny
        in->rt_ = NULL;
        out->rt_ = NULL;
        return;
    }

    dest_vrf = vm_port->GetVrfId();
    out->vrf_ = vm_port->GetVrf();

    mdata_flow = true;
    nat_done = true;
    nat_ip_saddr = METADATA_IP_ADDR;
    nat_ip_daddr = vm_port->GetIpAddr().to_ulong();
    nat_dport = pkt->dport;
    if (pkt->sport == METADATA_PORT) {
        nat_sport = METADATA_NAT_PORT;
    } else {
        nat_sport = pkt->sport;
    }
    nat_vrf = dest_vrf;
    nat_dest_vrf = pkt->vrf;
    return;
}

void PktFlowInfo::MdataServiceTranslate(const PktInfo *pkt, PktControlInfo *in,
                                        PktControlInfo *out) {
    if (in->intf_->GetType() == Interface::VMPORT) {
        MdataServiceFromVm(pkt, in, out);
    } else {
        MdataServiceFromHost(pkt, in, out);
    }
}

// DestNAT for packets entering into a VM with floating-ip.
// Can come here in two paths,
// - Packet originated on local vm.
// - Packet originated from remote vm
void PktFlowInfo::FloatingIpDNat(const PktInfo *pkt, PktControlInfo *in,
                                 PktControlInfo *out) {
    const VmPortInterface *vm_port = 
        static_cast<const VmPortInterface *>(out->intf_);
    const VmPortInterface::FloatingIpList &fip_list =
        vm_port->GetFloatingIpList();

    // We must NAT if the IP-DA is not same as Primary-IP on interface
    if (pkt->ip_daddr == vm_port->GetIpAddr().to_ulong()) {
        return;
    }

    // Look for matching floating-ip
    VmPortInterface::FloatingIpList::const_iterator it = fip_list.begin();
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
    out->rt_ = out->intf_->GetVrf()->GetUcRoute(vm_port->GetIpAddr());
    out->vn_ = it->vn_.get();
    dest_vn = &(it->vn_.get()->GetName());
    dest_vrf = out->intf_->GetVrf()->GetVrfId();

    // Translate the Dest-IP
    nat_done = true;
    nat_ip_saddr = pkt->ip_saddr;
    nat_ip_daddr = vm_port->GetIpAddr().to_ulong();
    nat_sport = pkt->sport;
    nat_dport = pkt->dport;
    nat_vrf = dest_vrf;

    if (in->rt_) {
        flow_source_vrf = static_cast<const Inet4Route *>(in->rt_)->GetVrfId();
    } else {
        flow_source_vrf = VrfEntry::kInvalidIndex;
    }
    flow_dest_vrf = it->vrf_.get()->GetVrfId();

    return;
}

void PktFlowInfo::FloatingIpSNat(const PktInfo *pkt, PktControlInfo *in,
                                 PktControlInfo *out) {
    const VmPortInterface *intf = 
        static_cast<const VmPortInterface *>(in->intf_);
    const VmPortInterface::FloatingIpList &fip_list = intf->GetFloatingIpList();
    VmPortInterface::FloatingIpList::const_iterator it = fip_list.begin();
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
    if (RouteToOutInfo(out->rt_, pkt, this, out, true) == false) {
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
    if (in->intf_->GetType() != Interface::VMPORT &&
        in->intf_->GetType() != Interface::VHOST) {
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
        if (RouteToOutInfo(out->rt_, pkt, this, out, true)) {
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
        if (RouteToOutInfo(out->rt_, pkt, this, out, true)) {
            if (out->intf_ && IntfHasFloatingIp(out->intf_)) {
                FloatingIpDNat(pkt, in, out);
            }
        }
    }

    // Packets needing metadata service will have route added by Mdata peer
    if ((in->rt_ && IsMdataRoute(in->rt_)) || 
        (out->rt_ && IsMdataRoute(out->rt_))) {
        MdataServiceTranslate(pkt, in, out);
    }

    // If out-interface was not found, get it based on out-route
    if (out->intf_ == NULL && out->rt_) {
        RouteToOutInfo(out->rt_, pkt, this, out, true);
    }

    return;
}

void PktFlowInfo::EgressProcess(const PktInfo *pkt, PktControlInfo *in,
                                PktControlInfo *out) {
    MplsLabel *mpls = Agent::GetMplsTable()->FindMplsLabel(pkt->tunnel.label);
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

    in->intf_ = InterfaceTable::FindInterface(pkt->agent_hdr.ifindex);
    if (in->intf_ == NULL || in->intf_->GetActiveState() == false) {
        LogError(pkt, "Invalid or Inactive ifindex");
        return false;
    }

    in->vrf_ = Agent::GetVrfTable()->FindVrfFromId(pkt->agent_hdr.vrf);
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

    flow_source_vrf = static_cast<const Inet4Route *>(in->rt_)->GetVrfId();
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

    dest_intf = out->intf_;
    return true;
}

void PktFlowInfo::Add(const PktInfo *pkt, PktControlInfo *in,
                      PktControlInfo *out) {
    FlowKey key(pkt->vrf, pkt->ip_saddr, pkt->ip_daddr,
                pkt->ip_proto, pkt->sport, pkt->dport);
    FlowEntryPtr flow(FlowTable::GetFlowTableObject()->Allocate(key));

    FlowEntryPtr rflow(NULL);
    if (nat_done) {
        FlowKey rkey(nat_vrf, nat_ip_daddr, nat_ip_saddr,
                     pkt->ip_proto, nat_dport, nat_sport);
        rflow = FlowTable::GetFlowTableObject()->Allocate(rkey);
    } else {
        FlowKey rkey(dest_vrf, pkt->ip_daddr, pkt->ip_saddr,
                     pkt->ip_proto, pkt->dport, pkt->sport);
        rflow = FlowTable::GetFlowTableObject()->Allocate(rkey);
    }

    InitFwdFlow(flow.get(), pkt, in);
    InitRevFlow(rflow.get(), pkt, out);

    PktFlowTrace(pkt, this);
    FlowTable::GetFlowTableObject()->Add(flow.get(), rflow.get());
}

void PktFlowInfo::RewritePktInfo(uint32_t flow_index) {
    FlowKey key;
    if (!FlowTableKSyncObject::GetKSyncObject()->GetFlowKey(flow_index, key)) {
        PKTFLOW_TRACE(Err,
                string("ECMP Resolve: unable to find flow index %u", flow_index));
        return;
    }

    FlowEntry *flow = FlowTable::GetFlowTableObject()->Find(key);
    if (!flow) {
        PKTFLOW_TRACE(Err,
                string("ECMP Resolve unable to find flow entry %u", flow_index));
        return;
    }

    pkt->vrf = key.vrf;
    pkt->ip_saddr = key.src.ipv4;
    pkt->ip_daddr = key.dst.ipv4;
    pkt->ip_proto = key.protocol;
    pkt->sport = key.src_port;
    pkt->dport = key.dst_port;
    pkt->agent_hdr.ifindex = flow->data.intf_entry->GetInterfaceId();
    pkt->agent_hdr.vrf = key.vrf;
    //Flow transition from Non ECMP to ECMP, use index 0
    if (flow->data.component_nh_idx == CompositeNH::kInvalidComponentNHIdx) {
        out_component_nh_idx = 0;
    }
    return;
}

bool FlowHandler::Run() {
    if (pkt_info_->ip == NULL) {
        LogError(pkt_info_, "Flow : Non-IP packet. Dropping");
        return true;
    }

    PktControlInfo in;
    PktControlInfo out;
    PktFlowInfo info(pkt_info_);

    MatchPolicy m_policy;

    SecurityGroupList empty_sg_id_l;
    info.source_sg_id_l = &empty_sg_id_l;
    info.dest_sg_id_l = &empty_sg_id_l;

    if (info.Process(pkt_info_, &in, &out) == false) {
        info.short_flow = true;
    }

    if (in.rt_) {
        const AgentPath *path = in.rt_->GetActivePath();
        info.source_sg_id_l = &(path->GetSecurityGroupList());
    }

    if (out.rt_) {
        const AgentPath *path = out.rt_->GetActivePath();
        info.dest_sg_id_l = &(path->GetSecurityGroupList());
    }

    if (info.source_vn == NULL)
        info.source_vn = FlowHandler::UnknownVn();

    if (info.dest_vn == NULL)
        info.dest_vn = FlowHandler::UnknownVn();

    if (in.intf_ && in.intf_->GetType() != Interface::VMPORT) {
        in.intf_ = NULL;
    }

    if (in.intf_ && out.intf_) {
        info.local_flow = true;
    }

    if (in.intf_ == NULL) {
        in.intf_ = out.intf_;
    } else if (out.intf_ == NULL) {
        out.intf_ = in.intf_;
    }

    if (in.vn_ == NULL) {
        in.vn_ = out.vn_;
    } else if (out.vn_ == NULL) {
        out.vn_ = in.vn_;
    }

    if (in.intf_) {
        in.vm_ = InterfaceToVm(in.intf_);
    }

    if (out.intf_) {
        out.vm_ = InterfaceToVm(out.intf_);
    }

    info.Add(pkt_info_, &in, &out);
    return true;
}

bool PktFlowInfo::InitFlowCmn(FlowEntry *flow, PktControlInfo *ctrl) {
    if (flow->last_modified_time) {
        if (flow->nat != nat_done) {
            flow->MakeShortFlow();
            return false;
        }
    }

    flow->last_modified_time = UTCTimestampUsec();
    flow->mdata_flow = mdata_flow;
    flow->nat = nat_done;
    flow->short_flow = short_flow;
    flow->local_flow = local_flow;

    flow->data.vn_entry = ctrl->vn_;
    flow->data.intf_entry = ctrl->intf_;
    flow->data.vm_entry = ctrl->vm_;
    return true;
}

void PktFlowInfo::InitFwdFlow(FlowEntry *flow, const PktInfo *pkt,
                              PktControlInfo *ctrl) {
    if (flow->flow_handle != pkt->GetAgentHdr().cmd_param) {
        if (flow->flow_handle != FlowEntry::kInvalidFlowHandle) {
            LOG(DEBUG, "Flow index changed from " << flow->flow_handle 
                << " to " << pkt->GetAgentHdr().cmd_param);
        }
        flow->flow_handle = pkt->GetAgentHdr().cmd_param;
    }

    if (InitFlowCmn(flow, ctrl) == false) {
        return;
    }
    flow->is_reverse_flow = false;
    flow->intf_in = pkt->GetAgentHdr().ifindex;

    flow->data.ingress = ingress;
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

    flow->data.ecmp = ecmp;
    flow->data.component_nh_idx = out_component_nh_idx;
}

void PktFlowInfo::InitRevFlow(FlowEntry *flow, const PktInfo *pkt,
                              PktControlInfo *ctrl) {
    if (InitFlowCmn(flow, ctrl) == false) {
        return;
    }
    flow->is_reverse_flow = true;
    if (ctrl->intf_) {
        flow->intf_in = ctrl->intf_->GetInterfaceId();
    } else {
        flow->intf_in = Interface::kInvalidIndex;
    }

    // Compute reverse flow fields
    flow->data.ingress = false;
    if (ctrl->intf_) {
        flow->data.ingress = ComputeDirection(ctrl->intf_);
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
    flow->data.ecmp = ecmp;
    flow->data.component_nh_idx = in_component_nh_idx;
}
