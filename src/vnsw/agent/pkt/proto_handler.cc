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

void ProtoHandler::EthHdr(const unsigned char *src, const unsigned char *dest, 
                          const uint16_t proto) {
    ethhdr *eth = pkt_info_->eth;

    memcpy(eth->h_dest, dest, ETH_ALEN);
    memcpy(eth->h_source, src, ETH_ALEN);
    eth->h_proto = htons(proto);
}

void ProtoHandler::IpHdr(uint16_t len, in_addr_t src, in_addr_t dest, 
                         uint8_t protocol) {
    iphdr *ip = pkt_info_->ip;

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
}

void ProtoHandler::UdpHdr(uint16_t len, in_addr_t src, uint16_t src_port, 
                          in_addr_t dest, uint16_t dest_port) {
    udphdr *udp = pkt_info_->transp.udp;
    udp->source = htons(src_port);
    udp->dest = htons(dest_port);
    udp->len = htons(len);
    udp->check = 0; 
    
#ifdef VNSW_AGENT_UDP_CSUM
    udp->check = UdpCsum(src, dest, len, udp);
#endif
}

void ProtoHandler::TcpHdr(in_addr_t src, uint16_t sport, in_addr_t dst, 
                          uint16_t dport, bool is_syn, uint32_t seq_no,
                          uint16_t len) {
    struct tcphdr *tcp = pkt_info_->transp.tcp;
    tcp->source = htons(sport);
    tcp->dest = htons(dport);

    if (is_syn) {
        tcp->syn = 1;
        tcp->ack = 0;
    } else {
        //If not sync, by default we are sending an ack
        tcp->ack = 1;
        tcp->syn = 0;
    }

    tcp->seq = htons(seq_no);
    tcp->ack_seq = htons(seq_no + 1);
    //Just a random number;
    tcp->window = htons(1000);
    tcp->doff = 5;
    tcp->check = 0;
    tcp->check = TcpCsum(src, dst, len, tcp);
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

uint16_t ProtoHandler::TcpCsum(in_addr_t src, in_addr_t dest, uint16_t len, 
                               tcphdr *tcp) {
    uint32_t sum = 0;
    PseudoTcpHdr phdr(src, dest, htons(len));
    sum = Sum((uint16_t *)&phdr, sizeof(PseudoTcpHdr), sum);
    return Csum((uint16_t *)tcp, len, sum);
}

void ProtoHandler::SwapL4() {
    if (pkt_info_->ip_proto == IPPROTO_TCP) {
        tcphdr *tcp = pkt_info_->transp.tcp;
        TcpHdr(htonl(pkt_info_->ip_daddr), ntohs(tcp->dest), 
               htonl(pkt_info_->ip_saddr), ntohs(tcp->source), 
               false, ntohs(tcp->ack_seq), 
               ntohs(pkt_info_->ip->tot_len) - sizeof(iphdr));

    } else if(pkt_info_->ip_proto == IPPROTO_UDP) {
        udphdr *udp = pkt_info_->transp.udp;
        UdpHdr(ntohs(udp->len), pkt_info_->ip_daddr, ntohs(udp->dest),
               pkt_info_->ip_saddr, ntohs(udp->source));
    }
}

void ProtoHandler::SwapIpHdr() {
    //IpHdr expects IP address to be in network format
    iphdr *ip = pkt_info_->ip;
    IpHdr(ntohs(ip->tot_len), ip->daddr, ip->saddr, ip->protocol);
}

void ProtoHandler::SwapEthHdr() {
    ethhdr *eth = pkt_info_->eth;
    EthHdr(eth->h_dest, eth->h_source, ntohs(eth->h_proto));
}

void ProtoHandler::Swap() {
    ProtoHandler::SwapL4();
    ProtoHandler::SwapIpHdr();
    ProtoHandler::SwapEthHdr();
}

///////////////////////////////////////////////////////////////////////////////
