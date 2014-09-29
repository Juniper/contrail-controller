/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

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
    ethhdr *eth = (ethhdr *)buff;
    uint16_t encap_len = sizeof(ethhdr);

    if (vlan_id != VmInterface::kInvalidVlanId) {
        encap_len += 4;
    }

    if (len < encap_len) {
        return 0;
    }

    dest.ToArray(eth->h_dest, sizeof(eth->h_dest));
    src.ToArray(eth->h_source, sizeof(eth->h_source));

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
    EthHdr((char *)pkt_info_->eth, sizeof(ethhdr), src, dest, proto,
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
    iphdr *ip = (iphdr *)buff;
    if (buf_len < sizeof(iphdr))
        return 0;

    ip->ihl = 5;
    ip->version = 4;
    ip->tos = 0;
    ip->tot_len = htons(len);
    ip->id = 0;
    ip->frag_off = 0;
    ip->ttl = 16;
    ip->protocol = protocol;
    ip->check = 0; 
    ip->saddr = src; 
    ip->daddr = dest;

    ip->check = Csum((uint16_t *)ip, ip->ihl * 4, 0);
    return sizeof(iphdr);
}

void ProtoHandler::IpHdr(uint16_t len, in_addr_t src, in_addr_t dest, 
                         uint8_t protocol) {

    IpHdr((char *)pkt_info_->ip, sizeof(iphdr), len, src, dest, protocol);
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
    udp->source = htons(src_port);
    udp->dest = htons(dest_port);
    udp->len = htons(len);
    udp->check = 0; 
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
    icmphdr *hdr = ((icmphdr *)buff);
    if (buf_len < sizeof(hdr))
        return 0;

    bzero(hdr, sizeof(icmphdr));

    hdr->type = type;
    hdr->code = code;
    assert(type == ICMP_DEST_UNREACH);
    hdr->un.frag.mtu = htons(word2);
    hdr->checksum = 0;
    return sizeof(icmphdr);
}

void ProtoHandler::IcmpChecksum(char *buff, uint16_t buf_len) {
    icmphdr *hdr = ((icmphdr *)buff);
    hdr->checksum = Csum((uint16_t *)buff, buf_len, 0);
}

void ProtoHandler::UdpHdr(uint16_t len, const uint8_t *src, uint16_t src_port,
                          const uint8_t *dest, uint16_t dest_port,
                          uint8_t next_hdr) {
    FillUdpHdr(pkt_info_->transp.udp, len, src_port, dest_port);
    pkt_info_->transp.udp->check = Ipv6Csum(src, dest, len, next_hdr,
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
