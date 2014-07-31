/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#include "cmn/agent_cmn.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/route_common.h"
#include "oper/vrf.h"
#include "pkt/tap_interface.h"
#include "pkt/test_tap_interface.h"
#include "pkt/pkt_handler.h"
#include "pkt/proto.h"
#include "pkt/flow_table.h"
#include "pkt/pkt_types.h"
#include "pkt/pkt_init.h"
#include "pkt/agent_stats.h"

#include "vr_types.h"
#include "vr_defs.h"
#include "vr_mpls.h"

#define PKT_TRACE(obj, arg)                                              \
do {                                                                     \
    std::ostringstream _str;                                             \
    _str << arg;                                                         \
    Pkt##obj::TraceMsg(PacketTraceBuf, __FILE__, __LINE__, _str.str());  \
} while (false)                                                          \

const std::size_t PktTrace::kPktMaxTraceSize;

////////////////////////////////////////////////////////////////////////////////

PktHandler::PktHandler(Agent *agent, const std::string &if_name,
                       boost::asio::io_service &io_serv, bool run_with_vrouter) 
                      : stats_(), agent_(agent) {
    for (int i = 0; i < MAX_MODULES; ++i) {
        if (i == PktHandler::DHCP || i == PktHandler::DNS)
            pkt_trace_.at(i).set_pkt_trace_size(512);
        else
            pkt_trace_.at(i).set_pkt_trace_size(128);
    }

    if (run_with_vrouter)
        tap_interface_.reset(new TapInterface(agent, if_name, io_serv, 
                             boost::bind(&PktHandler::HandleRcvPkt,
                                         this, _1, _2, _3)));
    else
        tap_interface_.reset(new TestTapInterface(agent, "test", io_serv,
                             boost::bind(&PktHandler::HandleRcvPkt,
                                         this, _1, _2, _3)));
    tap_interface_->Init();
}

PktHandler::~PktHandler() {
}

void PktHandler::Init() {
}

void PktHandler::IoShutdown() {
    tap_interface_->IoShutdown();
}

void PktHandler::Shutdown() {
}

void PktHandler::Register(PktModuleName type, RcvQueueFunc cb) {
    enqueue_cb_.at(type) = cb;
}

const unsigned char *PktHandler::mac_address() {
    return tap_interface_->mac_address();
}

void PktHandler::CreateInterfaces(const std::string &if_name) {
    PacketInterface::Create(agent_->interface_table(), if_name);
}

// Send packet to tap interface
void PktHandler::Send(uint8_t *msg, std::size_t len, PktModuleName mod) {
    stats_.PktSent(mod);
    pkt_trace_.at(mod).AddPktTrace(PktTrace::Out, len, msg);
    tap_interface_->AsyncWrite(msg, len);
}
 
// Process the packet received from tap interface
void PktHandler::HandleRcvPkt(uint8_t *ptr, std::size_t len,
                              std::size_t max_len) {
    boost::shared_ptr<PktInfo> pkt_info(new PktInfo(ptr, len, max_len));
    PktType::Type pkt_type = PktType::INVALID;
    PktModuleName mod = INVALID;
    Interface *intf = NULL;
    uint8_t *pkt;

    agent_->stats()->incr_pkt_exceptions();
    if ((pkt = ParseAgentHdr(pkt_info.get())) == NULL) {
        PKT_TRACE(Err, "Error parsing Agent Header");
        agent_->stats()->incr_pkt_invalid_agent_hdr();
        goto drop;
    }

    intf = agent_->interface_table()->FindInterface(pkt_info->GetAgentHdr().
                                                      ifindex);
    if (intf == NULL) {
        PKT_TRACE(Err, "Invalid interface index <" <<
                  pkt_info->GetAgentHdr().ifindex << ">");
        agent_->stats()->incr_pkt_invalid_interface();
        goto enqueue;
    }

    if (intf->type() == Interface::VM_INTERFACE) {
        VmInterface *vm_itf = static_cast<VmInterface *>(intf);
        if (!vm_itf->ipv4_forwarding()) {
            PKT_TRACE(Err, "ipv4 not enabled for interface index <" <<
                      pkt_info->GetAgentHdr().ifindex << ">");
            agent_->stats()->incr_pkt_dropped();
            goto drop;
        }
    }

    ParseUserPkt(pkt_info.get(), intf, pkt_type, pkt);
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

    // Look for IP packets that need ARP resolution
    if (pkt_info->ip && pkt_info->GetAgentHdr().cmd == AGENT_TRAP_RESOLVE) {
        mod = ARP;
        goto enqueue;
    }

    if (pkt_type == PktType::ICMP && IsGwPacket(intf, pkt_info->ip_daddr)) {
        mod = ICMP;
        goto enqueue;
    }

    if (pkt_info->ip &&
        pkt_info->GetAgentHdr().cmd == AGENT_TRAP_HANDLE_DF) {
        mod = ICMP_ERROR;
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
            stats_.PktQThresholdExceeded(mod);
        }
        return;
    }
    agent_->stats()->incr_pkt_no_handler();

drop:
    agent_->stats()->incr_pkt_dropped();
    return;
}

uint8_t *PktHandler::ParseAgentHdr(PktInfo *pkt_info) {
    // Format of packet trapped is,
    // OUTER_ETH - AGENT_HDR - PAYLOAD
    // Enusure sanity of the packet
#if defined(__linux__)
    if (pkt_info->len < (sizeof(ethhdr) + sizeof(agent_hdr) + sizeof(ethhdr))) {
#elif defined(__FreeBSD__)
    if (pkt_info->len < (sizeof(ether_header) + sizeof(agent_hdr) + sizeof(ether_header))) {
#else
#error "Unsupported platform"
#endif
        return NULL;
    }

    // packet comes with (outer) eth header, agent_hdr, actual eth packet
    pkt_info->eth = (ether_header *) pkt_info->pkt;
    uint8_t *pkt = ((uint8_t *)pkt_info->eth) + sizeof(ether_header);

    // Decode agent_hdr
    agent_hdr *agent = (agent_hdr *) pkt;
    pkt_info->agent_hdr.ifindex = ntohs(agent->hdr_ifindex);
    pkt_info->agent_hdr.vrf = ntohs(agent->hdr_vrf);
    pkt_info->agent_hdr.cmd = ntohs(agent->hdr_cmd);
    pkt_info->agent_hdr.cmd_param = ntohl(agent->hdr_cmd_param);
    pkt_info->agent_hdr.nh = ntohl(agent->hdr_cmd_param_1);
    if (pkt_info->agent_hdr.cmd == AGENT_TRAP_HANDLE_DF) {
        pkt_info->agent_hdr.mtu = ntohl(agent->hdr_cmd_param);
        pkt_info->agent_hdr.flow_index = ntohl(agent->hdr_cmd_param_1);
    }
    pkt += sizeof(agent_hdr);
    return pkt;
}

void PktHandler::SetOuterIp(PktInfo *pkt_info, uint8_t *pkt) {
#if defined(__linux__)
    iphdr *ip_hdr = (iphdr *)pkt;
    pkt_info->tunnel.ip_saddr = ntohl(ip_hdr->saddr);
    pkt_info->tunnel.ip_daddr = ntohl(ip_hdr->daddr);
#elif defined(__FreeBSD__)
    ip *ip_hdr = (ip *)pkt;
    pkt_info->tunnel.ip_saddr = ntohl(ip_hdr->ip_src.s_addr);
    pkt_info->tunnel.ip_daddr = ntohl(ip_hdr->ip_dst.s_addr);
#else
#error "Unsupported platform"
#endif
}

uint8_t *PktHandler::ParseIpPacket(PktInfo *pkt_info,
                                   PktType::Type &pkt_type, uint8_t *pkt) {
    pkt_info->ip = (ip *) pkt;
    pkt_info->ip_saddr = ntohl(pkt_info->ip->ip_src.s_addr);
    pkt_info->ip_daddr = ntohl(pkt_info->ip->ip_dst.s_addr);
    pkt_info->ip_proto = pkt_info->ip->ip_p;
    pkt += (pkt_info->ip->ip_hl << 2);

    switch (pkt_info->ip_proto) {
    case IPPROTO_UDP : {
        pkt_info->transp.udp = (udphdr *) pkt;
        pkt += sizeof(udphdr);
        pkt_info->data = pkt;
#if defined(__linux__)
        pkt_info->dport = ntohs(pkt_info->transp.udp->dest);
        pkt_info->sport = ntohs(pkt_info->transp.udp->source);
#elif defined(__FreeBSD__)
        pkt_info->dport = ntohs(pkt_info->transp.udp->uh_dport);
        pkt_info->sport = ntohs(pkt_info->transp.udp->uh_sport);
#else
#error "Unsuppoted platform"
#endif
        pkt_type = PktType::UDP;
        break;
    }

    case IPPROTO_TCP : {
        pkt_info->transp.tcp = (tcphdr *) pkt;
        pkt += sizeof(tcphdr);
        pkt_info->data = pkt;

#if defined(__linux__)
        pkt_info->dport = ntohs(pkt_info->transp.tcp->dest);
        pkt_info->sport = ntohs(pkt_info->transp.tcp->source);
        pkt_info->tcp_ack = pkt_info->transp.tcp->ack;
#elif defined(__FreeBSD__)
        pkt_info->dport = ntohs(pkt_info->transp.tcp->th_dport);
        pkt_info->sport = ntohs(pkt_info->transp.tcp->th_sport);
        pkt_info->tcp_ack = ntohl(pkt_info->transp.tcp->th_ack);
#else
#error "Unsupported platform"
#endif
        pkt_type = PktType::TCP;
        break;
    }

    case IPPROTO_ICMP: {
        pkt_type = PktType::ICMP;
        pkt_info->transp.icmp = (icmp *)pkt;
#if defined(__linux__)
        icmphdr *icmp = (icmphdr *)pkt;

        pkt_info->dport = htons(icmp->type);
        if (icmp->type == ICMP_ECHO || icmp->type == ICMP_ECHOREPLY) {
            pkt_info->sport = htons(icmp->un.echo.id);
#elif defined(__FreeBSD__)
        struct icmp *icmp = (struct icmp *)pkt;

        pkt_info->dport = htons(icmp->icmp_type);
        if (icmp->icmp_type == ICMP_ECHO || icmp->icmp_type == ICMP_ECHOREPLY) {
            pkt_info->sport = htons(icmp->icmp_hun.ih_idseq.icd_id);
#else
#error "Unsupported platform"
#endif
            pkt_info->dport = ICMP_ECHOREPLY;
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
        PKT_TRACE(Err, "Non-MPLS protocol <" << ntohs(gre->protocol) <<
                  "> in GRE header");
        return -1;
    }
    pkt_info->tunnel.type.SetType(TunnelType::MPLS_GRE);
    return sizeof(GreHdr);
}

// Parse MPLSoUDP header
int PktHandler::ParseMPLSoUDP(PktInfo *pkt_info, uint8_t *pkt) {
    if (pkt_info->dport != VR_MPLS_OVER_UDP_DST_PORT) {
        PKT_TRACE(Err, "Non-MPLS UDP dest-port <" <<
                  ntohs(pkt_info->dport) << ">");
        return -1;
    }
    pkt_info->tunnel.type.SetType(TunnelType::MPLS_UDP);
    return 0;
}

uint8_t *PktHandler::ParseUserPkt(PktInfo *pkt_info, Interface *intf,
                                  PktType::Type &pkt_type, uint8_t *pkt) {
    // get to the actual packet header
    pkt_info->eth = (ether_header *) pkt;
    pkt_info->ether_type = ntohs(pkt_info->eth->ether_type);

    if (pkt_info->ether_type == VLAN_PROTOCOL) {
#if defined(__linux__)
        pkt = ((uint8_t *)pkt_info->eth) + sizeof(ethhdr) + 4;
#elif defined(__FreeBSD__)
        pkt = ((uint8_t *)pkt_info->eth) + sizeof(ether_header) + 4;
#else
#error "Unsupported platform"
#endif
    } else {
#if defined(__linux__)
        pkt = ((uint8_t *)pkt_info->eth) + sizeof(ethhdr);
#elif defined(__FreeBSD__)
        pkt = ((uint8_t *)pkt_info->eth) + sizeof(ether_header);
#else
#error "Unsupported platform"
#endif
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
    if (pkt_info->ip_daddr != agent_->router_id().to_ulong()) {
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
        agent_->mpls_table()->FindMplsLabel(pkt_info->tunnel.label);
    if (label == NULL) {
        PKT_TRACE(Err, "Invalid MPLS Label <" <<
                  pkt_info->tunnel.label << ">. Ignoring");
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

// Enqueue an inter-task message to the specified module
void PktHandler::SendMessage(PktModuleName mod, InterTaskMsg *msg) {
    if (mod < MAX_MODULES) {
        boost::shared_ptr<PktInfo> pkt_info(new PktInfo(msg));
        if (!(enqueue_cb_.at(mod))(pkt_info)) {
            PKT_TRACE(Err, "Threshold exceeded while enqueuing IPC Message <" <<
                      mod << ">");
        }
    }
}

bool PktHandler::IsDHCPPacket(PktInfo *pkt_info) {
    if (pkt_info->dport == DHCP_SERVER_PORT || 
        pkt_info->sport == DHCP_CLIENT_PORT) {
        return true;
    }
    return false;
}

// Check if the packet is destined to the VM's default GW
bool PktHandler::IsGwPacket(const Interface *intf, uint32_t dst_ip) {
    if (!intf || intf->type() != Interface::VM_INTERFACE)
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

void PktHandler::PktTraceIterate(PktModuleName mod, PktTraceCallback cb) {
    if (cb) {
        PktTrace &pkt(pkt_trace_.at(mod));
        pkt.Iterate(cb);
    }
}

void PktHandler::PktStats::PktRcvd(PktModuleName mod) {
    if (mod < MAX_MODULES)
        received[mod]++;
}

void PktHandler::PktStats::PktSent(PktModuleName mod) {
    if (mod < MAX_MODULES)
        sent[mod]++;
}

void PktHandler::PktStats::PktQThresholdExceeded(PktModuleName mod) {
    if (mod < MAX_MODULES)
        q_threshold_exceeded[mod]++;
}

///////////////////////////////////////////////////////////////////////////////

PktInfo::PktInfo(uint8_t *msg, std::size_t msg_size, std::size_t max_len) : 
    pkt(msg), len(msg_size), max_pkt_len(max_len), data(), ipc(),
    type(PktType::INVALID), agent_hdr(), ether_type(-1), ip_saddr(),
    ip_daddr(), ip_proto(), sport(), dport(), tcp_ack(false), tunnel(), eth(),
    arp(), ip() {
    transp.tcp = 0;
}

PktInfo::PktInfo(InterTaskMsg *msg) :
    pkt(), len(), max_pkt_len(0), data(), ipc(msg), type(PktType::MESSAGE),
    agent_hdr(), ether_type(-1), ip_saddr(), ip_daddr(), ip_proto(), sport(),
    dport(), tcp_ack(false), tunnel(), eth(), arp(), ip() {
    transp.tcp = 0;
}

PktInfo::~PktInfo() {
    if (pkt) delete [] pkt;
}

const AgentHdr &PktInfo::GetAgentHdr() const {return agent_hdr;};

void PktInfo::UpdateHeaderPtr() {
    eth = (struct ether_header *)(pkt + IPC_HDR_LEN);
    ip = (struct ip *)(eth + 1);
    transp.tcp = (struct tcphdr *)(ip + 1);
}

std::size_t PktInfo::hash() const {
    std::size_t seed = 0;
    boost::hash_combine(seed, ip_saddr);
    boost::hash_combine(seed, ip_daddr);
    boost::hash_combine(seed, ip_proto);
    boost::hash_combine(seed, sport);
    boost::hash_combine(seed, dport);
    return seed;
}

