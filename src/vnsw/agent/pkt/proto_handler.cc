/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vr_defs.h"
#include "pkt/proto_handler.h"
#include "pkt/pkt_init.h"

///////////////////////////////////////////////////////////////////////////////

ProtoHandler::ProtoHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                           boost::asio::io_service &io)
    : agent_(agent), pkt_info_(info), io_(io) {}

ProtoHandler::~ProtoHandler() { 
}

// send packet to the pkt0 interface
void ProtoHandler::Send(uint16_t len, uint16_t itf, uint16_t vrf, 
                        uint16_t cmd, PktHandler::PktModuleName mod) {
    // update the outer header
    struct ethhdr *eth = (ethhdr *)pkt_info_->pkt;
    std::string tmp_str((char *)eth->h_source, ETH_ALEN);
    memcpy(eth->h_source, eth->h_dest, ETH_ALEN);
    memcpy(eth->h_dest, tmp_str.data(), ETH_ALEN);
    eth->h_proto = htons(0x800);

    // add agent header
    agent_hdr *agent = (agent_hdr *) (eth + 1);
    agent->hdr_ifindex = htons(itf);
    agent->hdr_vrf = htons(vrf);
    agent->hdr_cmd = htons(cmd);
    len += IPC_HDR_LEN;

    if (agent_->pkt()->pkt_handler()) {
        agent_->pkt()->pkt_handler()->Send(pkt_info_->pkt, len, mod);
    } else {
        delete [] pkt_info_->pkt;
    }

    pkt_info_->pkt = NULL;
}

uint16_t ProtoHandler::EthHdr(char *buff, uint8_t len, const unsigned char *src,
                              const unsigned char *dest, const uint16_t proto,
                              uint16_t vlan_id) {
    ethhdr *eth = (ethhdr *)buff;
    uint16_t encap_len = sizeof(ethhdr);

    if (vlan_id != VmInterface::kInvalidVlanId) {
        encap_len += 4;
    }

    if (len < encap_len) {
        return 0;
    }

    memcpy(eth->h_dest, dest, ETH_ALEN);
    memcpy(eth->h_source, src, ETH_ALEN);

    uint16_t *ptr = (uint16_t *) (buff + ETH_ALEN * 2);
    if (vlan_id != VmInterface::kInvalidVlanId) {
        *ptr = htons(ETH_P_8021Q);
        ptr++;
        *ptr = (vlan_id & 0xFFF);
    }

    *ptr = htons(proto);
    return encap_len;
}

void ProtoHandler::EthHdr(const unsigned char *src, const unsigned char *dest,
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

uint16_t ProtoHandler::UdpHdr(char *buff, uint16_t buf_len, uint16_t len,
                              in_addr_t src, uint16_t src_port, in_addr_t dest,
                              uint16_t dest_port) {
    if (buf_len < sizeof(udphdr))
        return 0;

    udphdr *udp = (udphdr *) buff;
    udp->source = htons(src_port);
    udp->dest = htons(dest_port);
    udp->len = htons(len);
    udp->check = 0; 
    
#ifdef VNSW_AGENT_UDP_CSUM
    udp->check = UdpCsum(src, dest, len, udp);
#endif
    return sizeof(udphdr);
}

void ProtoHandler::UdpHdr(uint16_t len, in_addr_t src, uint16_t src_port,
                          in_addr_t dest, uint16_t dest_port) {
    UdpHdr((char *)pkt_info_->transp.udp, sizeof(udphdr), len, src, src_port,
           dest, dest_port);
}

uint16_t ProtoHandler::IcmpHdr(char *buff, uint16_t buf_len, uint8_t type,
                               uint8_t code, uint16_t word1, uint16_t word2) {
    icmphdr *hdr = ((icmphdr *)buff);
    if (buf_len < sizeof(hdr))
        return 0;

    memset(hdr, sizeof(icmphdr), 0);

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
    PseudoUdpHdr phdr(src, dest, 0x11, htons(len));
    sum = Sum((uint16_t *)&phdr, sizeof(PseudoUdpHdr), sum);
    return Csum((uint16_t *)udp, len, sum);
}

///////////////////////////////////////////////////////////////////////////////
