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
#if defined(__linux__)
    struct ethhdr *eth = (ethhdr *)pkt_info_->pkt;
//.de.byte.breaker ugly...
    std::string tmp_str((char *)eth->h_source, ETH_ALEN);
    memcpy(eth->h_source, eth->h_dest, ETH_ALEN);
    memcpy(eth->h_dest, tmp_str.data(), ETH_ALEN);
    eth->h_proto = htons(0x800);
#elif defined(__FreeBSD__)
    struct ether_header *eth = (ether_header*)pkt_info_->pkt;
//.de.byte.breaker ugly...
    std::string tmp_str((char *)eth->ether_shost, ETHER_ADDR_LEN);
    memcpy(eth->ether_shost, eth->ether_dhost, ETHER_ADDR_LEN);
    memcpy(eth->ether_dhost, tmp_str.data(), ETHER_ADDR_LEN);
    eth->ether_type = htons(0x800);
#else
#error "Unsupported platform"
#endif

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
#if defined(__linux__)
    ethhdr *eth = pkt_info_->eth;

    memcpy(eth->h_dest, dest, ETH_ALEN);
    memcpy(eth->h_source, src, ETH_ALEN);
    eth->h_proto = htons(proto);
#elif defined(__FreeBSD__)
    ether_header *eth = pkt_info_->eth;

    memcpy(eth->ether_dhost, dest, ETHER_ADDR_LEN);
    memcpy(eth->ether_shost, src, ETHER_ADDR_LEN);
    eth->ether_type = htons(proto);
#else
#error "Unsupported platform"
#endif
}

void ProtoHandler::IpHdr(uint16_t len, in_addr_t src, in_addr_t dest, 
                         uint8_t protocol) {
#if defined(__linux__)
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
#elif defined(__FreeBSD__)
    ip *ip = pkt_info_->ip;

    ip->ip_hl = 5;
    ip->ip_v= 4;
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
#else
#error "Unsupported platform"
#endif

}

void ProtoHandler::UdpHdr(uint16_t len, in_addr_t src, uint16_t src_port, 
                          in_addr_t dest, uint16_t dest_port) {
    udphdr *udp = pkt_info_->transp.udp;
#if defined(__linux__)
    udp->source = htons(src_port);
    udp->dest = htons(dest_port);
    udp->len = htons(len);
    udp->check = 0; 
#elif defined(__FreeBSD__)
    udp->uh_sport = htons(src_port);
    udp->uh_dport = htons(dest_port);
    udp->uh_ulen = htons(len);
    udp->uh_sum = 0; 
#else
#error "Unsupported platform"
#endif
    
#ifdef VNSW_AGENT_UDP_CSUM
    udp->check = UdpCsum(src, dest, len, udp);
#endif
}

void ProtoHandler::TcpHdr(in_addr_t src, uint16_t sport, in_addr_t dst, 
                          uint16_t dport, bool is_syn, uint32_t seq_no,
                          uint16_t len) {
//.de.byte.breaker
#if defined(__linux__)
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
#elif defined(__FreeBSD__)
    struct tcphdr *tcp = pkt_info_->transp.tcp;
    tcp->th_sport = htons(sport);
    tcp->th_dport = htons(dport);

    if (is_syn) {
	tcp->th_flags = TH_SYN;
    } else {
        //If not sync, by default we are sending an ack
	tcp->th_flags = TH_ACK;
    }

    tcp->th_seq = htons(seq_no);
    tcp->th_ack = htons(seq_no + 1);
    //Just a random number;
    tcp->th_win = htons(1000);
    tcp->th_off = 5;
    tcp->th_sum = 0;
    tcp->th_sum = TcpCsum(src, dst, len, tcp);
#else
#error "Unsupported platform"
#endif
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
//.de.byte.breaker
#if defined(__linux__)
        TcpHdr(htonl(pkt_info_->ip_daddr), ntohs(tcp->dest), 
               htonl(pkt_info_->ip_saddr), ntohs(tcp->source), 
               false, ntohs(tcp->ack_seq), 
               ntohs(pkt_info_->ip->tot_len) - sizeof(iphdr));
#elif defined(__FreeBSD__)
        TcpHdr(htonl(pkt_info_->ip_daddr), ntohs(tcp->th_dport), 
               htonl(pkt_info_->ip_saddr), ntohs(tcp->th_sport), 
               false, ntohs(tcp->th_ack), 
               ntohs(pkt_info_->ip->ip_len) - sizeof(ip));
#else
#error "Unsupported platform"
#endif

    } else if(pkt_info_->ip_proto == IPPROTO_UDP) {
        udphdr *udp = pkt_info_->transp.udp;
//.de.byte.breaker
#if defined(__linux__)
        UdpHdr(ntohs(udp->len), pkt_info_->ip_daddr, ntohs(udp->dest),
               pkt_info_->ip_saddr, ntohs(udp->source));
#elif defined(__FreeBSD__)
        UdpHdr(ntohs(udp->uh_ulen), pkt_info_->ip_daddr, ntohs(udp->uh_dport),
               pkt_info_->ip_saddr, ntohs(udp->uh_sport));
#else
#error "Unsupported platform"
#endif
    }
}

void ProtoHandler::SwapIpHdr() {
    //IpHdr expects IP address to be in network format
//.de.byte.breaker
#if defined(__linux__)
    iphdr *ip = pkt_info_->ip;

    IpHdr(ntohs(ip->tot_len), ip->daddr, ip->saddr, ip->protocol);
#elif defined(__FreeBSD__)
    ip *ip = pkt_info_->ip;

    IpHdr(ntohs(ip->ip_len), ip->ip_dst.s_addr, ip->ip_src.s_addr, ip->ip_p);
#else
#error "Unsupported platform"
#endif
}

void ProtoHandler::SwapEthHdr() {
//.de.byte.breaker
#if defined(__linux__)
    ethhdr *eth = pkt_info_->eth;
    EthHdr(eth->h_dest, eth->h_source, ntohs(eth->h_proto));
#elif defined(__FreeBSD__)
    ether_header *eth = pkt_info_->eth;
    EthHdr(eth->ether_dhost, eth->ether_shost, ntohs(eth->ether_type));
#endif
}

void ProtoHandler::Swap() {
    ProtoHandler::SwapL4();
    ProtoHandler::SwapIpHdr();
    ProtoHandler::SwapEthHdr();
}

///////////////////////////////////////////////////////////////////////////////
