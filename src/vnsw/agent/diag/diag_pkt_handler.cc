/*
 *  * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 *   */

#include <stdint.h>
#include "base/os.h"
#include <map>
#include "vr_defs.h"
#include "base/timer.h"
#include "cmn/agent_cmn.h"
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "diag/diag.h"
#include "diag/diag_proto.h"
#include "diag/ping.h"
#include "oper/mirror_table.h"


void DiagPktHandler::SetReply() {
    AgentDiagPktData *ad = (AgentDiagPktData *)pkt_info_->data;
    ad->op_ = htonl(AgentDiagPktData::DIAG_REPLY);
}

void DiagPktHandler::SetDiagChkSum() {
    switch(pkt_info_->ip->ip_p) {
        case IPPROTO_TCP:
            pkt_info_->transp.tcp->th_sum = 0xffff;
            break;

        case IPPROTO_UDP:
            pkt_info_->transp.udp->uh_sum = 0xffff;
            break;

        case IPPROTO_ICMP:
            pkt_info_->transp.icmp->icmp_cksum = 0xffff;
            break;

        default:
            break;
    }
}

void DiagPktHandler::Reply() {
    SetReply();
    Swap();
    SetDiagChkSum();
    Send(GetInterfaceIndex(), GetVrfIndex(), AgentHdr::TX_ROUTE,
         CMD_PARAM_PACKET_CTRL, CMD_PARAM_1_DIAG, PktHandler::DIAG);
}

bool DiagPktHandler::IsTraceRoutePacket() {
    if (pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_ZERO_TTL ||
        pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_ICMP_ERROR)
        return true;

    return false;
}

bool DiagPktHandler::HandleTraceRoutePacket() {

    if (pkt_info_->ip == NULL) {
        // we only send IPv4 trace route packets; ignore other packets
        return true;
    }

    if (pkt_info_->ip->ip_ttl == 0) {
        SendTimeExceededPacket();
        return true;
    }

    return HandleTraceRouteResponse();
}

// Send time exceeded ICMP packet
void DiagPktHandler::SendTimeExceededPacket() {
    // send IP header + 128 bytes from incoming pkt in the icmp response
    uint16_t icmp_len = pkt_info_->ip->ip_hl * 4 + 128;
    if (ntohs(pkt_info_->ip->ip_len) < icmp_len)
        icmp_len = ntohs(pkt_info_->ip->ip_len);
    uint8_t icmp_payload[icmp_len];
    memcpy(icmp_payload, pkt_info_->ip, icmp_len);
    DiagEntry::DiagKey key = -1;
    if (!ParseIcmpData(icmp_payload, icmp_len, (uint16_t *)&key))
        return;

    char *ptr = (char *)pkt_info_->pkt;
    uint16_t buf_len = pkt_info_->max_pkt_len;

    // Retain the agent-header before ethernet header
    uint16_t len = (char *)pkt_info_->eth - (char *)pkt_info_->pkt;

    // Form ICMP Packet with EthHdr - IP Header - ICMP Header
    len += EthHdr(ptr + len, buf_len - len, agent()->vhost_interface()->mac(),
                  MacAddress(pkt_info_->eth->ether_shost), ETHERTYPE_IP,
                  VmInterface::kInvalidVlanId);

    uint16_t ip_len = sizeof(iphdr) + 8 + icmp_len;
    len += IpHdr(ptr + len, buf_len - len, ip_len,
                 htonl(agent()->router_id().to_ulong()),
                 htonl(pkt_info_->ip_saddr.to_v4().to_ulong()),
                 IPPROTO_ICMP, DEFAULT_IP_ID, DEFAULT_IP_TTL);

    struct icmp *hdr = (struct icmp *) (ptr + len);
    memset((uint8_t *)hdr, 0, 8);
    hdr->icmp_type = ICMP_TIME_EXCEEDED;
    hdr->icmp_code = ICMP_EXC_TTL;
    memcpy(ptr + len + 8, icmp_payload, icmp_len);
    IcmpChecksum((char *)hdr, 8 + icmp_len);
    len += 8 + icmp_len;

    pkt_info_->set_len(len);

    Send(GetInterfaceIndex(), pkt_info_->vrf, AgentHdr::TX_SWITCH,
         PktHandler::ICMP);
}

bool DiagPktHandler::HandleTraceRouteResponse() {
    uint8_t *data = (uint8_t *)(pkt_info_->transp.icmp) + 8;
    uint16_t len = pkt_info_->len - (data - pkt_info_->pkt);
    DiagEntry::DiagKey key = -1;
    if (!ParseIcmpData(data, len, (uint16_t *)&key))
        return true;

    DiagEntry *entry = diag_table_->Find(key);
    if (!entry) return true;

    address_ = pkt_info_->ip_saddr.to_v4().to_string();
    entry->HandleReply(this);

    DiagEntryOp *op;
    if (IsDone()) {
        op = new DiagEntryOp(DiagEntryOp::DELETE, entry);
    } else {
        op = new DiagEntryOp(DiagEntryOp::RETRY, entry);
    }
    entry->diag_table()->Enqueue(op);

    return true;
}

bool DiagPktHandler::ParseIcmpData(const uint8_t *data, uint16_t data_len,
                                   uint16_t *key) {
    if (data_len < sizeof(struct ip))
        return false;
    struct ip *ip_hdr = (struct ip *)(data);
    *key = ntohs(ip_hdr->ip_id);
    uint16_t len = sizeof(struct ip);

    if (ip_hdr->ip_src.s_addr == htonl(agent()->router_id().to_ulong()) ||
        ip_hdr->ip_dst.s_addr == htonl(agent()->router_id().to_ulong())) {
        // Check inner header
        switch (ip_hdr->ip_p) {
            case IPPROTO_GRE : {
                if (data_len < len + sizeof(GreHdr))
                    return true;
                len += sizeof(GreHdr);
                break;
            }

            case IPPROTO_UDP : {
                if (data_len < len + sizeof(udphdr))
                    return true;
                len += sizeof(udphdr);
                break;
            }

            default: {
                return true;
            }
        }
        len += sizeof(MplsHdr);
        if (data_len < len + sizeof(struct ip))
            return true;
        ip_hdr = (struct ip *)(data + len);
        len += sizeof(struct ip);
        switch (ip_hdr->ip_p) {
            case IPPROTO_TCP:
                len += sizeof(tcphdr);
                break;

            case IPPROTO_UDP:
                len += sizeof(udphdr);
                break;

            case IPPROTO_ICMP:
                len += 8;
                break;

            default:
                return true;
        }
        if (data_len < len + sizeof(AgentDiagPktData))
            return true;

        AgentDiagPktData *agent_data = (AgentDiagPktData *)(data + len);
        if (ntohl(agent_data->op_) == AgentDiagPktData::DIAG_REQUEST) {
            agent_data->op_ = htonl(AgentDiagPktData::DIAG_REPLY);
        } else if (ntohl(agent_data->op_) == AgentDiagPktData::DIAG_REPLY) {
            *key = ntohs(agent_data->key_);
            done_ = true;
        }
    }

    return true;
}

bool DiagPktHandler::Run() {
    if (IsTraceRoutePacket()) {
        return HandleTraceRoutePacket();
    }

    AgentDiagPktData *ad = (AgentDiagPktData *)pkt_info_->data;

    if (!ad) {
        //Ignore if packet doesnt have proper L4 header
        return true;
    }
    if (pkt_info_->ether_type == ETHERTYPE_IPV6) {
        //Ignore IPv6 packets until it is supported
        return true;
    }
    if (ntohl(ad->op_) == AgentDiagPktData::DIAG_REQUEST) {
        //Request received swap the packet
        //and dump the packet back
        Reply();
        return true;
    }

    if (ntohl(ad->op_) != AgentDiagPktData::DIAG_REPLY) {
        return true;
    }
    //Reply for a query we sent
    DiagEntry::DiagKey key = ntohs(ad->key_);
    DiagEntry *entry = diag_table_->Find(key);
    if (!entry) {
        return true;
    }

    entry->HandleReply(this);

    if (entry->GetSeqNo() == entry->GetMaxAttempts()) {
        DiagEntryOp *op;
        op = new DiagEntryOp(DiagEntryOp::DELETE, entry);
        entry->diag_table()->Enqueue(op);
    } else {
        entry->Retry();
    }

    return true;
}

void DiagPktHandler::TcpHdr(in_addr_t src, uint16_t sport, in_addr_t dst,
                            uint16_t dport, bool is_syn, uint32_t seq_no,
                            uint16_t len) {
    struct tcphdr *tcp = pkt_info_->transp.tcp;
    tcp->th_sport = htons(sport);
    tcp->th_dport = htons(dport);

    if (is_syn) {
        tcp->th_flags &= ~TH_ACK;
    } else {
        //If not sync, by default we are sending an ack
        tcp->th_flags &= ~TH_SYN;
    }

    tcp->th_seq = htons(seq_no);
    tcp->th_ack = htons(seq_no + 1);
    //Just a random number;
    tcp->th_win = htons(1000);
    tcp->th_off = 5;
    tcp->th_sum = 0;
    tcp->th_sum = TcpCsum(src, dst, len, tcp);
}

uint16_t DiagPktHandler::TcpCsum(in_addr_t src, in_addr_t dest, uint16_t len,
                                 tcphdr *tcp) {
    uint32_t sum = 0;
    PseudoTcpHdr phdr(src, dest, htons(len));
    sum = Sum((uint16_t *)&phdr, sizeof(PseudoTcpHdr), sum);
    return Csum((uint16_t *)tcp, len, sum);
}

void DiagPktHandler::SwapL4() {
    if (pkt_info_->ip_proto == IPPROTO_TCP) {
        tcphdr *tcp = pkt_info_->transp.tcp;
        TcpHdr(htonl(pkt_info_->ip_daddr.to_v4().to_ulong()),
               ntohs(tcp->th_dport),
               htonl(pkt_info_->ip_saddr.to_v4().to_ulong()),
               ntohs(tcp->th_sport), false, ntohs(tcp->th_ack),
               ntohs(pkt_info_->ip->ip_len) - sizeof(struct ip));

    } else if(pkt_info_->ip_proto == IPPROTO_UDP) {
        udphdr *udp = pkt_info_->transp.udp;
        UdpHdr(ntohs(udp->uh_ulen), pkt_info_->ip_daddr.to_v4().to_ulong(),
               ntohs(udp->uh_dport), pkt_info_->ip_saddr.to_v4().to_ulong(),
               ntohs(udp->uh_sport));
    }
}

void DiagPktHandler::SwapIpHdr() {
    //IpHdr expects IP address to be in network format
    struct ip *ip = pkt_info_->ip;
    IpHdr(ntohs(ip->ip_len), ip->ip_dst.s_addr,
          ip->ip_src.s_addr, ip->ip_p, DEFAULT_IP_ID, DEFAULT_IP_TTL);
}

void DiagPktHandler::SwapEthHdr() {
    struct ether_header *eth = pkt_info_->eth;
    EthHdr(MacAddress(eth->ether_dhost), MacAddress(eth->ether_shost), ntohs(eth->ether_type));
}

void DiagPktHandler::Swap() {
    SwapL4();
    SwapIpHdr();
    SwapEthHdr();
}
