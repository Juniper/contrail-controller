/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>

#include "cmn/agent_cmn.h"
#include "cmn/agent_stats.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/agent_route.h"
#include "oper/vrf.h"
#include "pkt/pkt_handler.h"
#include "pkt/proto.h"
#include "pkt/flowtable.h"
#include "pkt/pkt_types.h"
#include "pkt/pkt_init.h"

#include "vr_types.h"
#include "vr_defs.h"
#include "vr_mpls.h"

#define PKT_TRACE(obj, ...)                                              \
do {                                                                     \
    Pkt##obj::TraceMsg(PacketTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
} while (false)                                                          \


PktHandler *PktHandler::instance_;
const std::size_t PktTrace::pkt_trace_size;

PktHandler::PktHandler(DB *db, const std::string &if_name,
                       boost::asio::io_service &io_serv, bool run_with_vrouter) 
                      : stats_(), db_(db) {
    if (run_with_vrouter)
        tap_ = new TapInterface(if_name, io_serv, 
                   boost::bind(&PktHandler::HandleRcvPkt, this, _1, _2));
    else
        tap_ = new TestTapInterface("test", io_serv,
                   boost::bind(&PktHandler::HandleRcvPkt, this, _1, _2));
    assert(tap_ != NULL);
}

void PktHandler::CreateHostInterface(std::string &if_name) {
    PacketInterface::CreateReq(Agent::GetInstance()->GetInterfaceTable(),
                            if_name);
    InterfaceNH::CreateHostPortReq(if_name);
}

// Check if the packet is destined to the VM's default GW
bool PktHandler::IsGwPacket(const Interface *intf, uint32_t dst_ip) {
    if (intf->type() != Interface::VM_INTERFACE)
        return false;

    const VmInterface *vm_intf = static_cast<const VmInterface *>(intf);
    const VnEntry *vn = vm_intf->vn();
    if (vn) {
        const std::vector<VnIpam> &ipam = vn->GetVnIpam();
        for (unsigned int i = 0; i < ipam.size(); ++i) {
            uint32_t mask = 
                ipam[i].plen ? (0xFFFFFFFF << (32 - ipam[i].plen)) : 0;
            if ((vm_intf->ip_addr().to_ulong() & mask)
                    != (ipam[i].ip_prefix.to_ulong() & mask))
                continue;
            return (ipam[i].default_gw.to_ulong() == dst_ip);
        }
    }

    return false;
}

void PktHandler::HandleRcvPkt(uint8_t *ptr, std::size_t len) {
    PktInfo *pkt_info(new PktInfo(ptr, len));
    PktType::Type pkt_type = PktType::INVALID;
    ModuleName mod = INVALID;
    Interface *intf = NULL;
    uint8_t *pkt;

    AgentStats::GetInstance()->incr_pkt_exceptions();
    if ((pkt = ParseAgentHdr(pkt_info)) == NULL) {
        PKT_TRACE(Err, "Error parsing Agent Header");
        AgentStats::GetInstance()->incr_pkt_invalid_agent_hdr();
        goto drop;
    }

    intf = InterfaceTable::GetInstance()->FindInterface(pkt_info->GetAgentHdr().ifindex);
    if (intf == NULL) {
        std::stringstream str;
        str << pkt_info->GetAgentHdr().ifindex;
        PKT_TRACE(Err, "Invalid interface index <" + str.str() + ">");
        AgentStats::GetInstance()->incr_pkt_invalid_interface();
        goto enqueue;
    }

    if (intf->type() == Interface::VM_INTERFACE) {
        VmInterface *vm_itf = static_cast<VmInterface *>(intf);
        if (!vm_itf->ipv4_forwarding()) {
            std::stringstream str;
            str << pkt_info->GetAgentHdr().ifindex;
            PKT_TRACE(Err, 
                 "ipv4 not enabled for interface index <" + str.str() + ">");
            AgentStats::GetInstance()->incr_pkt_dropped();
            goto drop;
        }
    }

    ParseUserPkt(pkt_info, intf, pkt_type, pkt);
    pkt_info->vrf = pkt_info->agent_hdr.vrf;

    // Handle ARP packet
    if (pkt_type == PktType::ARP) {
        mod = ARP;
        goto enqueue;
    }

    // Packets needing flow
    if ((pkt_info->GetAgentHdr().cmd == AGENT_TRAP_FLOW_MISS ||
         pkt_info->GetAgentHdr().cmd == AGENT_TRAP_ECMP_RESOLVE) && 
        pkt_info->ip) {
        mod = FLOW;
        goto enqueue;
    }
    // Look for DHCP and DNS packets if corresponding service is enabled
    // Service processing over-rides ACL/Flow
    if (intf->dhcp_enabled() && (pkt_type == PktType::UDP)) {
        if (pkt_info->dport == DHCP_SERVER_PORT || 
            pkt_info->sport == DHCP_CLIENT_PORT) {
            mod = DHCP;
            goto enqueue;
        }
    } 
    
    if (intf->dns_enabled() && (pkt_type == PktType::UDP)) {
        if (pkt_info->dport == DNS_SERVER_PORT) {
            mod = DNS;
            goto enqueue;
        }
    }

    // Look for IP packets that needs ARP resolution
    if (pkt_info->ip && pkt_info->GetAgentHdr().cmd == AGENT_TRAP_RESOLVE) {
        mod = ARP;
        goto enqueue;
    }

    // first ping packet will require flow handling, when policy is enabled
    if (pkt_type == PktType::ICMP && IsGwPacket(intf, pkt_info->ip_daddr)) {
        mod = ICMP;
        goto enqueue;
    }

    if (pkt_info->GetAgentHdr().cmd == AGENT_TRAP_DIAG && pkt_info->ip) {
        mod = DIAG;
        goto enqueue;
    }

enqueue:
    stats_.PktRcvd(mod);
    pkt_trace_.at(mod).AddPktTrace(PktTrace::In, 
            pkt_info->len, pkt_info->pkt);

    if (mod != INVALID) {
        if (!(enqueue_cb_.at(mod))(pkt_info)) {
            std::stringstream str;
            str << mod;
            PKT_TRACE(Err, "Threshold exceeded while enqueuing to module <" + str.str() + ">");
        }
        return;
    }
    AgentStats::GetInstance()->incr_pkt_no_handler();

drop:
    AgentStats::GetInstance()->incr_pkt_dropped();
    delete pkt_info;
    return;
}

uint8_t *PktHandler::ParseAgentHdr(PktInfo *pkt_info) {

    // Format of packet trapped is,
    // OUTER_ETH - AGENT_HDR - PAYLOAD
    // Enusure sanity of the packet
    if (pkt_info->len < (sizeof(ethhdr) + sizeof(agent_hdr) + sizeof(ethhdr))) {
        return NULL;
    }

    // packet comes with (outer) eth header, agent_hdr, actual eth packet
    pkt_info->eth = (ethhdr *) pkt_info->pkt;
    uint8_t *pkt = ((uint8_t *)pkt_info->eth) + sizeof(ethhdr);

    // Decode agent_hdr
    agent_hdr *agent = (agent_hdr *) pkt;
    pkt_info->agent_hdr.ifindex = ntohs(agent->hdr_ifindex);
    pkt_info->agent_hdr.vrf = ntohs(agent->hdr_vrf);
    pkt_info->agent_hdr.cmd = ntohs(agent->hdr_cmd);
    pkt_info->agent_hdr.cmd_param = ntohl(agent->hdr_cmd_param);
    pkt += sizeof(agent_hdr);
    return pkt;
}

void PktHandler::SetOuterIp(PktInfo *pkt_info, uint8_t *pkt) {
    iphdr *ip_hdr = (iphdr *)pkt;
    pkt_info->tunnel.ip_saddr = ntohl(ip_hdr->saddr);
    pkt_info->tunnel.ip_daddr = ntohl(ip_hdr->daddr);
}

uint8_t *PktHandler::ParseIpPacket(PktInfo *pkt_info,
                                   PktType::Type &pkt_type, uint8_t *pkt) {
    pkt_info->ip = (iphdr *) pkt;
    pkt_info->ip_saddr = ntohl(pkt_info->ip->saddr);
    pkt_info->ip_daddr = ntohl(pkt_info->ip->daddr);
    pkt_info->ip_proto = pkt_info->ip->protocol;
    pkt += (pkt_info->ip->ihl * 4);

    switch (pkt_info->ip_proto) {
    case IPPROTO_UDP : {
        pkt_info->transp.udp = (udphdr *) pkt;
        pkt += sizeof(udphdr);
        pkt_info->data = pkt;

        pkt_info->dport = ntohs(pkt_info->transp.udp->dest);
        pkt_info->sport = ntohs(pkt_info->transp.udp->source);
        pkt_type = PktType::UDP;
        break;
    }

    case IPPROTO_TCP : {
        pkt_info->transp.tcp = (tcphdr *) pkt;
        pkt += sizeof(tcphdr);
        pkt_info->data = pkt;

        pkt_info->dport = ntohs(pkt_info->transp.tcp->dest);
        pkt_info->sport = ntohs(pkt_info->transp.tcp->source);
        pkt_type = PktType::TCP;
        break;
    }

    case IPPROTO_ICMP: {
        pkt_info->transp.icmp = (icmphdr *) pkt;
        pkt_type = PktType::ICMP;

        icmphdr *icmp = (icmphdr *)pkt;

        pkt_info->dport = htons(icmp->type);
        if (icmp->type == ICMP_ECHO || icmp->type == ICMP_ECHOREPLY) {
            pkt_info->dport = ICMP_ECHOREPLY;
            pkt_info->sport = htons(icmp->un.echo.id);
        } else {
            pkt_info->sport = 0;
        }
        break;
    }

    default: {
        pkt_type = PktType::IPV4;
        pkt_info->dport = 0;
        pkt_info->sport = 0;
        break;
    }
    }

    return pkt;
}

// Parse MPLSoGRE header
int PktHandler::ParseMPLSoGRE(PktInfo *pkt_info, uint8_t *pkt) {
    GreHdr *gre = (GreHdr *)(pkt);
    if (gre->protocol != ntohs(VR_GRE_PROTO_MPLS)) {
        std::stringstream str;
        str << ntohs(gre->protocol);
        PKT_TRACE(Err, "Non-MPLS protocol <" + str.str() + "> in GRE header");
        return -1;
    }
    pkt_info->tunnel.type.SetType(TunnelType::MPLS_GRE);
    return sizeof(GreHdr);
}

// Parse MPLSoUDP header
int PktHandler::ParseMPLSoUDP(PktInfo *pkt_info, uint8_t *pkt) {
    if (pkt_info->dport != VR_MPLS_OVER_UDP_DST_PORT) {
        std::stringstream str;
        str << ntohs(pkt_info->dport);
        PKT_TRACE(Err, "Non-MPLS UDP dest-port <" + str.str() + ">");
        return -1;
    }
    pkt_info->tunnel.type.SetType(TunnelType::MPLS_UDP);
    return 0;
}

uint8_t *PktHandler::ParseUserPkt(PktInfo *pkt_info, Interface *intf,
                                  PktType::Type &pkt_type, uint8_t *pkt) {
    // get to the actual packet header
    pkt_info->eth = (ethhdr *) pkt;
    pkt_info->ether_type = ntohs(pkt_info->eth->h_proto);

    if (pkt_info->ether_type == VLAN_PROTOCOL) {
        pkt = ((uint8_t *)pkt_info->eth) + sizeof(ethhdr) + 4;
    } else {
        pkt = ((uint8_t *)pkt_info->eth) + sizeof(ethhdr);
    }

    // Parse payload
    if (pkt_info->ether_type == 0x806) {
        pkt_info->arp = (ether_arp *) pkt;
        pkt_type = PktType::ARP;
        return pkt;
    }

    // Identify NON-IP Packets
    if (pkt_info->ether_type != IP_PROTOCOL && 
            pkt_info->ether_type != VLAN_PROTOCOL) {
        pkt_info->data = pkt;
        pkt_type = PktType::NON_IPV4;
        return pkt;
    }

    SetOuterIp(pkt_info, pkt);
    // IP Packets
    pkt = ParseIpPacket(pkt_info, pkt_type, pkt);
    // If tunneling is not enabled on interface or if it is a DHCP packet,
    // dont parse any further
    if (intf->IsTunnelEnabled() == false || IsDHCPPacket(pkt_info)) {
        return pkt;
    }

    pkt_type = PktType::INVALID;
    // Decap only IP-DA is ours
    if (pkt_info->ip_daddr != Agent::GetInstance()->GetRouterId().to_ulong()) {
        PKT_TRACE(Err, "Tunnel packet not destined to me. Ignoring");
        return pkt;
    }

    int len = 0;
    // Look for supported headers
    switch (pkt_info->ip_proto) {
    case IPPROTO_GRE :
        // Parse MPLSoGRE tunnel
        len = ParseMPLSoGRE(pkt_info, pkt);
        break;

    case IPPROTO_UDP:
        // Parse MPLSoUDP tunnel
        len = ParseMPLSoUDP(pkt_info, pkt);
        break;

    default:
        break;
    }

    if (len < 0) {
       return pkt;
    }

    // Look for MPLS header without changing pkt
    MplsHdr *mpls = (MplsHdr *)(pkt + len);
    // MPLS Header validation. Check for,
    // - Valid label value. We have Label per VM Port for now
    // - There is single label
    uint32_t mpls_host = ntohl(mpls->hdr);
    pkt_info->tunnel.label = (mpls_host & 0xFFFFF000) >> 12;

    MplsLabel *label = 
        Agent::GetInstance()->GetMplsTable()->FindMplsLabel(pkt_info->tunnel.label);
    if (label == NULL) {
        std::stringstream str;
        str << pkt_info->tunnel.label;
        PKT_TRACE(Err, "Invalid MPLS Label <" + str.str() + ">. Ignoring");
        pkt_info->tunnel.label = MplsTable::kInvalidLabel;
        pkt_type = PktType::INVALID;
        return pkt;
    }

    if ((mpls_host & 0x100) == 0) {
        pkt_info->tunnel.label = MplsTable::kInvalidLabel;
        PKT_TRACE(Err, "Unexpected MPLS Label Stack. Ignoring");
        pkt_type = PktType::INVALID;
        return pkt;
    }

    // Point to IP header after GRE and MPLS
    pkt += len + sizeof(MplsHdr);
    ParseIpPacket(pkt_info, pkt_type, pkt);
    return pkt;
}

bool PktHandler::IsDHCPPacket(PktInfo *pkt_info) {
    if (pkt_info->dport == DHCP_SERVER_PORT || 
        pkt_info->sport == DHCP_CLIENT_PORT) {
        return true;
    }
    return false;
}

void PktHandler::SendMessage(ModuleName mod, IpcMsg *msg)
{
    if (mod < MAX_MODULES) {
        PktInfo *pkt_info(new PktInfo(msg));
        if (!(enqueue_cb_.at(mod))(pkt_info)) {
            std::stringstream str;
            str << mod;
            PKT_TRACE(Err, "Threshold exceeded while enqueuing IPC Message <" 
                      + str.str() + ">");
        }
    }
}

uint32_t PktHandler::GetModuleStats(ModuleName mod) {
    switch(mod) {
    case FLOW:
        return stats_.flow_rcvd;
        break;
    case ARP:
        return stats_.arp_rcvd;
        break;
    case DHCP:
        return stats_.dhcp_rcvd;
        break;
    case DNS:
        return stats_.dns_rcvd;
        break;
    case ICMP:
        return stats_.icmp_rcvd;
        break;
    case INVALID:
    case MAX_MODULES:
        return stats_.dropped;
        break;
    default:
        break;
    }
    return 0;
}

void PktHandler::PktStats::PktRcvd(ModuleName mod) {
    total_rcvd++;
    switch(mod) {
        case FLOW:
            flow_rcvd++;
            break;
        case ARP:
            arp_rcvd++;
            break;
        case DHCP:
            dhcp_rcvd++;
            break;
        case DNS:
            dns_rcvd++;
            break;
        case ICMP:
            icmp_rcvd++;
            break;
        case DIAG:
            diag_rcvd++;
            break;
        case INVALID:
        case MAX_MODULES:
            dropped++;
            break;
        default:
             assert(0);
    }
}

void PktHandler::PktStats::PktSent(ModuleName mod) {
    total_sent.fetch_and_increment();
    switch(mod) {
        case ARP:
            arp_sent++;
            break;
        case DHCP:
            dhcp_sent++;
            break;
        case DNS:
            dns_sent++;
            break;
        case ICMP:
            icmp_sent++;
            break;
        case DIAG:
            diag_sent++;
            break;
        default:
             assert(0);
    }
}
