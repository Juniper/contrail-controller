/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>

#include "cmn/agent_cmn.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/route_common.h"
#include "oper/vrf.h"
#include "pkt/control_interface.h"
#include "pkt/pkt_handler.h"
#include "pkt/proto.h"
#include "pkt/flow_table.h"
#include "pkt/pkt_types.h"
#include "pkt/pkt_init.h"
#include "pkt/agent_stats.h"
#include "pkt/packet_buffer.h"

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

PktHandler::PktHandler(Agent *agent, PktModule *pkt_module) :
    stats_(), agent_(agent), pkt_module_(pkt_module) {
    for (int i = 0; i < MAX_MODULES; ++i) {
        if (i == PktHandler::DHCP || i == PktHandler::DHCPV6 ||
            i == PktHandler::DNS)
            pkt_trace_.at(i).set_pkt_trace_size(512);
        else
            pkt_trace_.at(i).set_pkt_trace_size(128);
    }
}

PktHandler::~PktHandler() {
}

void PktHandler::Register(PktModuleName type, RcvQueueFunc cb) {
    enqueue_cb_.at(type) = cb;
}

uint32_t PktHandler::EncapHeaderLen() const {
    return agent_->pkt()->control_interface()->EncapsulationLength();
}

// Send packet to tap interface
void PktHandler::Send(const AgentHdr &hdr, const PacketBufferPtr &buff) {
    stats_.PktSent(PktHandler::PktModuleName(buff->module()));
    pkt_trace_.at(buff->module()).AddPktTrace(PktTrace::Out, buff->data_len(),
                                              buff->data(), &hdr);
    if (agent_->pkt()->control_interface()->Send(hdr, buff) <= 0) {
        PKT_TRACE(Err, "Error sending packet");
    }
    return;
}

// Process the packet received from tap interface
void PktHandler::HandleRcvPkt(const AgentHdr &hdr, const PacketBufferPtr &buff){
    boost::shared_ptr<PktInfo> pkt_info (new PktInfo(buff));
    PktType::Type pkt_type = PktType::INVALID;
    PktModuleName mod = INVALID;
    Interface *intf = NULL;
    uint8_t *pkt = buff->data();

    pkt_info->agent_hdr = hdr;
    agent_->stats()->incr_pkt_exceptions();
    intf = agent_->interface_table()->FindInterface(hdr.ifindex);
    if (intf == NULL) {
        PKT_TRACE(Err, "Invalid interface index <" << hdr.ifindex << ">");
        agent_->stats()->incr_pkt_invalid_interface();
        goto enqueue;
    }

    if (intf->type() == Interface::VM_INTERFACE) {
        VmInterface *vm_itf = static_cast<VmInterface *>(intf);
        if (!vm_itf->layer3_forwarding()) {
            PKT_TRACE(Err, "ipv4 not enabled for interface index <" <<
                      hdr.ifindex << ">");
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
    if ((hdr.cmd == AgentHdr::TRAP_FLOW_MISS ||
         hdr.cmd == AgentHdr::TRAP_ECMP_RESOLVE)) {
        
        if ((pkt_info->ip && pkt_info->family == Address::INET) ||
            (pkt_info->ip6 && pkt_info->family == Address::INET6)) {
            mod = FLOW;
            goto enqueue;
        }
    }

    // Look for DHCP and DNS packets if corresponding service is enabled
    // Service processing over-rides ACL/Flow
    if (intf->dhcp_enabled() && (pkt_type == PktType::UDP)) {
        if (pkt_info->dport == DHCP_SERVER_PORT ||
            pkt_info->sport == DHCP_CLIENT_PORT) {
            mod = DHCP;
            goto enqueue;
        }
        if (pkt_info->dport == DHCPV6_SERVER_PORT ||
            pkt_info->sport == DHCPV6_CLIENT_PORT) {
            mod = DHCPV6;
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
    if (pkt_info->ip && hdr.cmd == AgentHdr::TRAP_RESOLVE) {
        mod = ARP;
        goto enqueue;
    }

    if (pkt_type == PktType::ICMP && IsGwPacket(intf, pkt_info->ip_daddr)) {
        mod = ICMP;
        goto enqueue;
    }

    if (pkt_type == PktType::ICMPV6) {
        mod = ICMPV6;
        goto enqueue;
    }

    if (pkt_info->ip && hdr.cmd == AgentHdr::TRAP_HANDLE_DF) {
        mod = ICMP_ERROR;
        goto enqueue;
    }

    if (hdr.cmd == AgentHdr::TRAP_DIAG && pkt_info->ip) {
        mod = DIAG;
        goto enqueue;
    }

enqueue:
    pkt_info->packet_buffer()->set_module(mod);
    stats_.PktRcvd(mod);
    pkt_trace_.at(mod).AddPktTrace(PktTrace::In, pkt_info->len, pkt_info->pkt, 
                                   &pkt_info->agent_hdr);

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

void PktHandler::SetOuterIp(PktInfo *pkt_info, uint8_t *pkt) {
    if (pkt_info->ether_type != ETHERTYPE_IP) {
        return;
    }
    struct ip *ip_hdr = (struct ip *)pkt;
    pkt_info->tunnel.ip_saddr = ntohl(ip_hdr->ip_src.s_addr);
    pkt_info->tunnel.ip_daddr = ntohl(ip_hdr->ip_dst.s_addr);
}

static bool InterestedIPv6Protocol(uint8_t proto) {
    if (proto == IPPROTO_UDP || proto == IPPROTO_TCP ||
        proto == IPPROTO_ICMPV6) {
        return true;
    }

    return false;
}
uint8_t *PktHandler::ParseIpPacket(PktInfo *pkt_info,
                                   PktType::Type &pkt_type, uint8_t *pkt) {
    if (pkt_info->ether_type == ETHERTYPE_IP) {
        struct ip *ip = (struct ip *)pkt;
        pkt_info->ip = ip;
        pkt_info->family = Address::INET;
        pkt_info->ip_saddr = IpAddress(Ip4Address(ntohl(ip->ip_src.s_addr)));
        pkt_info->ip_daddr = IpAddress(Ip4Address(ntohl(ip->ip_dst.s_addr)));
        pkt_info->ip_proto = ip->ip_p;
        pkt += (ip->ip_hl << 2);
    } else if (pkt_info->ether_type == ETHERTYPE_IPV6) {
        pkt_info->family = Address::INET6;
        ip6_hdr *ip = (ip6_hdr *)pkt;
        pkt_info->ip6 = ip;
        pkt_info->ip = NULL;
        Ip6Address::bytes_type addr;

        for (int i = 0; i < 16; i++) {
            addr[i] = ip->ip6_src.s6_addr[i];
        }
        pkt_info->ip_saddr = IpAddress(Ip6Address(addr));

        for (int i = 0; i < 16; i++) {
            addr[i] = ip->ip6_dst.s6_addr[i];
        }
        pkt_info->ip_daddr = IpAddress(Ip6Address(addr));

        // Look for known transport headers. Fallback to the last header if
        // no known header is found
        uint8_t proto = ip->ip6_ctlun.ip6_un1.ip6_un1_nxt;
        uint16_t len = sizeof(ip6_hdr);
        while (InterestedIPv6Protocol(proto) == false) {
            struct ip6_ext *ext = (ip6_ext *)(pkt + len);
            proto = ext->ip6e_nxt;
            len += (ext->ip6e_len * 8);
            if (ext->ip6e_len == 0) {
                proto = 0;
                break;
            }
            if (len >= pkt_info->len) {
                proto = 0;
                break;
            }
        }
        pkt += len;
        pkt_info->ip_proto = proto;
    } else {
        assert(0);
    }

    switch (pkt_info->ip_proto) {
    case IPPROTO_UDP : {
        pkt_info->transp.udp = (udphdr *) pkt;
        pkt += sizeof(udphdr);
        pkt_info->data = pkt;

        pkt_info->dport = ntohs(pkt_info->transp.udp->uh_dport);
        pkt_info->sport = ntohs(pkt_info->transp.udp->uh_sport);
        pkt_type = PktType::UDP;
        break;
    }

    case IPPROTO_TCP : {
        pkt_info->transp.tcp = (tcphdr *) pkt;
        pkt += sizeof(tcphdr);
        pkt_info->data = pkt;

        pkt_info->dport = ntohs(pkt_info->transp.tcp->th_dport);
        pkt_info->sport = ntohs(pkt_info->transp.tcp->th_sport);
        pkt_info->tcp_ack = pkt_info->transp.tcp->th_flags & TH_ACK;
        pkt_type = PktType::TCP;
        break;
    }

    case IPPROTO_ICMP: {
        pkt_info->transp.icmp = (struct icmp *) pkt;
        pkt_type = PktType::ICMP;

        struct icmp *icmp = (struct icmp *)pkt;

        pkt_info->dport = htons(icmp->icmp_type);
        if (icmp->icmp_type == ICMP_ECHO || icmp->icmp_type == ICMP_ECHOREPLY) {
            pkt_info->dport = ICMP_ECHOREPLY;
            pkt_info->sport = htons(icmp->icmp_id);
        } else {
            pkt_info->sport = 0;
        }
        break;
    }

    case IPPROTO_ICMPV6: {
        pkt_type = PktType::ICMPV6;
        icmp6_hdr *icmp = (icmp6_hdr *)pkt;
        pkt_info->transp.icmp6 = icmp;

        pkt_info->dport = htons(icmp->icmp6_type);
        if (icmp->icmp6_type == ICMP6_ECHO_REQUEST ||
            icmp->icmp6_type == ICMP6_ECHO_REPLY) {
            pkt_info->dport = ICMP6_ECHO_REPLY;
            pkt_info->sport = htons(icmp->icmp6_id);
        } else {
            pkt_info->sport = 0;
        }
        break;
    }

    default: {
        pkt_type = PktType::IP;
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
    pkt_info->eth = (struct ether_header *) pkt;
    pkt_info->ether_type = ntohs(pkt_info->eth->ether_type);

    if (pkt_info->ether_type == ETHERTYPE_VLAN) {
        pkt = ((uint8_t *)pkt_info->eth) + sizeof(struct ether_header) + 4;
        uint16_t *tmp = ((uint16_t *)pkt) - 1;
        pkt_info->ether_type = ntohs(*tmp);
    } else {
        pkt = ((uint8_t *)pkt_info->eth) + sizeof(struct ether_header);
    }

    // Parse payload
    if (pkt_info->ether_type == ETHERTYPE_ARP) {
        pkt_info->arp = (ether_arp *) pkt;
        pkt_type = PktType::ARP;
        return pkt;
    }

    // Identify NON-IP Packets
    if (pkt_info->ether_type != ETHERTYPE_IP &&
        pkt_info->ether_type != ETHERTYPE_IPV6) {
        pkt_info->data = pkt;
        pkt_type = PktType::NON_IP;
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
    if (pkt_info->family != Address::INET ||
        pkt_info->ip_daddr.to_v4() != agent_->router_id()) {
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

    // Find IPv4/IPv6 Packet based on first nibble in payload
    if ((pkt[0] & 0x60) == 0x60) {
        pkt_info->ether_type = ETHERTYPE_IPV6;
    } else {
        pkt_info->ether_type = ETHERTYPE_IP;
    }

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
        // we dont handle DHCPv6 coming from fabric
        return true;
    }
    return false;
}

// Check if the packet is destined to the VM's default GW
bool PktHandler::IsGwPacket(const Interface *intf, const IpAddress &dst_ip) {
    if (!intf || intf->type() != Interface::VM_INTERFACE)
        return false;

    const VmInterface *vm_intf = static_cast<const VmInterface *>(intf);
    if (dst_ip.is_v6() && vm_intf->ip6_addr().is_unspecified())
        return false;
    else if (dst_ip.is_v4() && vm_intf->ip_addr().is_unspecified())
        return false;
    const VnEntry *vn = vm_intf->vn();
    if (vn) {
        const std::vector<VnIpam> &ipam = vn->GetVnIpam();
        for (unsigned int i = 0; i < ipam.size(); ++i) {
            if (dst_ip.is_v4()) {
                if (!ipam[i].IsV4()) {
                    continue;
                }
                if (IsIp4SubnetMember(vm_intf->ip_addr(),
                                      ipam[i].ip_prefix.to_v4(), ipam[i].plen))
                return (ipam[i].default_gw == dst_ip);
            } else {
                if (!ipam[i].IsV6()) {
                    continue;
                }
                if (IsIp6SubnetMember(vm_intf->ip6_addr(),
                                      ipam[i].ip_prefix.to_v6(), ipam[i].plen))
                    return (ipam[i].default_gw == dst_ip);
            }

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

PktInfo::PktInfo(const PacketBufferPtr &buff) :
    pkt(buff->data()), len(buff->data_len()), max_pkt_len(buff->buffer_len()),
    data(), ipc(), family(Address::UNSPEC), type(PktType::INVALID), agent_hdr(),
    ether_type(-1), ip_saddr(), ip_daddr(), ip_proto(), sport(), dport(),
    tcp_ack(false), tunnel(), eth(), arp(), ip(), ip6(), packet_buffer_(buff) {
    transp.tcp = 0;
}

PktInfo::PktInfo(Agent *agent, uint32_t buff_len, uint32_t module,
                 uint32_t mdata) :
    len(), max_pkt_len(), data(), ipc(), family(Address::UNSPEC),
    type(PktType::INVALID), agent_hdr(), ether_type(-1), ip_saddr(), ip_daddr(),
    ip_proto(), sport(), dport(), tcp_ack(false), tunnel(), eth(), arp(),
    ip(), ip6() {

    packet_buffer_ = agent->pkt()->packet_buffer_manager()->Allocate
        (module, buff_len, mdata);
    pkt = packet_buffer_->data();
    len = packet_buffer_->data_len();
    max_pkt_len = packet_buffer_->buffer_len();

    transp.tcp = 0;
}

PktInfo::PktInfo(InterTaskMsg *msg) :
    pkt(), len(), max_pkt_len(0), data(), ipc(msg), family(Address::UNSPEC),
    type(PktType::MESSAGE), agent_hdr(), ether_type(-1), ip_saddr(), ip_daddr(),
    ip_proto(), sport(), dport(), tcp_ack(false), tunnel(), eth(), arp(), ip(),
    ip6(), packet_buffer_() {
    transp.tcp = 0;
}

PktInfo::~PktInfo() {
}

const AgentHdr &PktInfo::GetAgentHdr() const {
    return agent_hdr;
}

void PktInfo::AllocPacketBuffer(Agent *agent, uint32_t module, uint16_t len,
                                uint32_t mdata) {
    packet_buffer_ = agent->pkt()->packet_buffer_manager()->Allocate
        (module, len, mdata);
    pkt = packet_buffer_->data();
    len = packet_buffer_->data_len();
    max_pkt_len = packet_buffer_->buffer_len();
}

void PktInfo::set_len(uint32_t x) {
    packet_buffer_->set_len(x);
    len = x;
}

void PktInfo::UpdateHeaderPtr() {
    eth = (struct ether_header *)(pkt);
    ip = (struct ip *)(eth + 1);
    transp.tcp = (struct tcphdr *)(ip + 1);
}

std::size_t PktInfo::hash() const {
    std::size_t seed = 0;
    if (family == Address::INET) {
        boost::hash_combine(seed, ip_saddr.to_v4().to_ulong());
        boost::hash_combine(seed, ip_daddr.to_v4().to_ulong());
    } else if (family == Address::INET6) {
        uint32_t *words;

        words = (uint32_t *) (ip_saddr.to_v6().to_bytes().c_array());
        boost::hash_combine(seed, words[0]);
        boost::hash_combine(seed, words[1]);
        boost::hash_combine(seed, words[2]);
        boost::hash_combine(seed, words[3]);

        words = (uint32_t *) (ip_daddr.to_v6().to_bytes().c_array());
        boost::hash_combine(seed, words[0]);
        boost::hash_combine(seed, words[1]);
        boost::hash_combine(seed, words[2]);
        boost::hash_combine(seed, words[3]);
    } else {
        assert(0);
    }
    boost::hash_combine(seed, ip_proto);
    boost::hash_combine(seed, sport);
    boost::hash_combine(seed, dport);
    return seed;
}

///////////////////////////////////////////////////////////////////////////////
void PktTrace::Pkt::Copy(Direction d, std::size_t l, uint8_t *msg,
                         std::size_t pkt_trace_size, const AgentHdr *hdr) {
    uint16_t hdr_len = sizeof(AgentHdr);
    dir = d;
    len = l + hdr_len;
    memcpy(pkt, hdr, hdr_len);
    memcpy(pkt + hdr_len, msg, std::min(l, (pkt_trace_size - hdr_len)));
}

void PktTrace::AddPktTrace(Direction dir, std::size_t len, uint8_t *msg,
                           const AgentHdr *hdr) {
    if (num_buffers_) {
        end_ = (end_ + 1) % num_buffers_;
        pkt_buffer_[end_].Copy(dir, len, msg, pkt_trace_size_, hdr);
        count_ = std::min((count_ + 1), (uint32_t) num_buffers_);
    }
}
