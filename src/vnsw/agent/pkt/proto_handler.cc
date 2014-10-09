/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "vr_defs.h"
#include "pkt/proto_handler.h"
#include "pkt/pkt_init.h"
#include "pkt/packet_buffer.h"

///////////////////////////////////////////////////////////////////////////////

ProtoHandler::ProtoHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                           boost::asio::io_service &io)
    : agent_(agent), pkt_info_(info), io_(io) {}

ProtoHandler::~ProtoHandler() {
}

uint32_t ProtoHandler::EncapHeaderLen() const {
    return agent_->pkt()->pkt_handler()->EncapHeaderLen();
}

// send packet to the pkt0 interface
void ProtoHandler::Send(uint16_t itf, uint16_t vrf, uint16_t cmd,
                        PktHandler::PktModuleName mod) {
    // If pkt_info_->pkt is non-NULL, pkt is freed in destructor of pkt_info_
    if (agent_->pkt()->pkt_handler() == NULL) {
        return;
    }

    AgentHdr hdr(itf, vrf, cmd);
    agent_->pkt()->pkt_handler()->Send(hdr, pkt_info_->packet_buffer_ptr());
}

uint16_t ProtoHandler::EthHdr(char *buff, uint8_t len, const MacAddress &src,
                              const MacAddress &dest, const uint16_t proto,
                              uint16_t vlan_id) {
    struct ether_header *eth = (struct ether_header *)buff;
    uint16_t encap_len = sizeof(struct ether_header);

    if (vlan_id != VmInterface::kInvalidVlanId) {
        encap_len += 4;
    }

    if (len < encap_len) {
        return 0;
    }

    dest.ToArray(eth->ether_dhost, sizeof(eth->ether_dhost));
    src.ToArray(eth->ether_shost, sizeof(eth->ether_shost));

    uint16_t *ptr = (uint16_t *) (buff + ETHER_ADDR_LEN * 2);
    if (vlan_id != VmInterface::kInvalidVlanId) {
        *ptr = htons(ETH_P_8021Q);
        ptr++;
        *ptr = (vlan_id & 0xFFF);
    }

    *ptr = htons(proto);
    return encap_len;
}

void ProtoHandler::EthHdr(const MacAddress &src, const MacAddress &dest,
                          const uint16_t proto) {
    EthHdr((char *)pkt_info_->eth, sizeof(struct ether_header), src, dest, proto,
           VmInterface::kInvalidVlanId);
}

void ProtoHandler::VlanHdr(uint8_t *ptr, uint16_t tci) {
    vlanhdr *vlan = reinterpret_cast<vlanhdr *>(ptr);
    vlan->tpid = htons(0x8100);
    vlan->tci = htons(tci);
    vlan += 1;
    vlan->tpid = htons(0x800);
    return;
}

uint16_t ProtoHandler::IpHdr(char *buff, uint16_t buf_len, uint16_t len,
                             in_addr_t src, in_addr_t dest, uint8_t protocol) {
    struct ip *ip = (struct ip *)buff;
    if (buf_len < sizeof(struct ip))
        return 0;

    ip->ip_hl = 5;
    ip->ip_v = 4;
    ip->ip_tos = 0;
    ip->ip_len = htons(len);
    ip->ip_id = 0;
    ip->ip_off = 0;
    ip->ip_ttl = 16;
    ip->ip_p = protocol;
    ip->ip_sum = 0;
    ip->ip_src.s_addr = src;
    ip->ip_dst.s_addr = dest;

    ip->ip_sum = Csum((uint16_t *)ip, ip->ip_hl * 4, 0);
    return sizeof(struct ip);
}

void ProtoHandler::IpHdr(uint16_t len, in_addr_t src, in_addr_t dest,
                         uint8_t protocol) {

    IpHdr((char *)pkt_info_->ip, sizeof(struct ip), len, src, dest, protocol);
}

void ProtoHandler::Ip6Hdr(ip6_hdr *ip, uint16_t plen, uint8_t next_header,
                          uint8_t hlim, uint8_t *src, uint8_t *dest) {
    ip->ip6_flow = htonl(0x60000000); // version 6, TC and Flow set to 0
    ip->ip6_plen = htons(plen);
    ip->ip6_nxt = next_header;
    ip->ip6_hlim= hlim;
    memcpy(ip->ip6_src.s6_addr, src, 16);
    memcpy(ip->ip6_dst.s6_addr, dest, 16);
}

void ProtoHandler::FillUdpHdr(udphdr *udp, uint16_t len,
                              uint16_t src_port, uint16_t dest_port) {
    udp->uh_sport = htons(src_port);
    udp->uh_dport = htons(dest_port);
    udp->uh_ulen = htons(len);
    udp->uh_sum = 0;
}

uint16_t ProtoHandler::UdpHdr(udphdr *udp, uint16_t buf_len, uint16_t len,
                              in_addr_t src, uint16_t src_port, in_addr_t dest,
                              uint16_t dest_port) {
    if (buf_len < sizeof(udphdr))
        return 0;

    FillUdpHdr(udp, len, src_port, dest_port);
#ifdef VNSW_AGENT_UDP_CSUM
    udp->check = UdpCsum(src, dest, len, pkt_info_->transp.udp);
#endif

    return sizeof(udphdr);
}

void ProtoHandler::UdpHdr(uint16_t len, in_addr_t src, uint16_t src_port,
                          in_addr_t dest, uint16_t dest_port) {
    UdpHdr(pkt_info_->transp.udp, sizeof(udphdr), len, src, src_port,
           dest, dest_port);
}

uint16_t ProtoHandler::IcmpHdr(char *buff, uint16_t buf_len, uint8_t type,
                               uint8_t code, uint16_t word1, uint16_t word2) {
    struct icmp *hdr = ((struct icmp *)buff);
    if (buf_len < sizeof(hdr))
        return 0;

    bzero(hdr, sizeof(struct icmp));

    hdr->icmp_type = type;
    hdr->icmp_code = code;
    assert(type == ICMP_DEST_UNREACH);
    hdr->icmp_nextmtu = htons(word2);
    hdr->icmp_cksum = 0;
    return sizeof(struct icmp);
}

void ProtoHandler::IcmpChecksum(char *buff, uint16_t buf_len) {
    struct icmp *hdr = ((struct icmp *)buff);
    hdr->icmp_cksum = Csum((uint16_t *)buff, buf_len, 0);
}

void ProtoHandler::UdpHdr(uint16_t len, const uint8_t *src, uint16_t src_port,
                          const uint8_t *dest, uint16_t dest_port,
                          uint8_t next_hdr) {
    FillUdpHdr(pkt_info_->transp.udp, len, src_port, dest_port);
    pkt_info_->transp.udp->uh_sum = Ipv6Csum(src, dest, len, next_hdr,
                                            (uint16_t *)pkt_info_->transp.udp);
}

uint32_t ProtoHandler::Sum(uint16_t *ptr, std::size_t len, uint32_t sum) {
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
        if (sum & 0x80000000)
            sum = (sum & 0xFFFF) + (sum >> 16);
    }

    if (len > 0)
        sum += *(uint8_t *)ptr;

    return sum;
}

uint16_t ProtoHandler::Csum(uint16_t *ptr, std::size_t len, uint32_t sum) {
    sum = Sum(ptr, len, sum);

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return ~sum;
}

uint16_t ProtoHandler::UdpCsum(in_addr_t src, in_addr_t dest, 
                               std::size_t len, udphdr *udp) {
    uint32_t sum = 0;
    PseudoUdpHdr phdr(src, dest, IPPROTO_UDP, htons(len));
    sum = Sum((uint16_t *)&phdr, sizeof(PseudoUdpHdr), sum);
    return Csum((uint16_t *)udp, len, sum);
}

uint16_t ProtoHandler::Ipv6Csum(const uint8_t *src, const uint8_t *dest,
                                uint16_t plen, uint8_t next_hdr, uint16_t *hdr) {
    uint32_t len = htonl((uint32_t)plen);
    uint32_t next = htonl((uint32_t)next_hdr);

    uint32_t pseudo = 0;
    pseudo = Sum((uint16_t *)src, 16, 0);
    pseudo = Sum((uint16_t *)dest, 16, pseudo);
    pseudo = Sum((uint16_t *)&len, 4, pseudo);
    pseudo = Sum((uint16_t *)&next, 4, pseudo);
    return Csum(hdr, plen, pseudo);
}

uint16_t ProtoHandler::Icmpv6Csum(const uint8_t *src, const uint8_t *dest,
                                  icmp6_hdr *icmp, uint16_t plen) {
    return Ipv6Csum(src, dest, plen, IPPROTO_ICMPV6, (uint16_t *)icmp);
}

///////////////////////////////////////////////////////////////////////////////
