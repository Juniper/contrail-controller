/*
 *  * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 *   */

#include <map>
#include <sys/types.h>
#include "net/bsdudp.h"
#include "net/bsdtcp.h"
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
    pkt_info_->ip->ip_sum = 0xffff;
}

void DiagPktHandler::Reply() {
    SetReply();
    Swap();
    SetDiagChkSum();
    Send(GetLength() - (2 * EncapHeaderLen()), GetInterfaceIndex(), GetVrfIndex(),
         AgentHdr::TX_ROUTE, PktHandler::DIAG);
}

bool DiagPktHandler::Run() {
    AgentDiagPktData *ad = (AgentDiagPktData *)pkt_info_->data;

    if (!ad) {
        //Ignore if packet doesnt have proper L4 header
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
    DiagEntry::DiagKey key = ntohl(ad->key_);
    DiagEntry *entry = diag_table_->Find(key);
    if (!entry) {
        return true;
    }

    entry->HandleReply(this);

    if (entry->GetSeqNo() == entry->GetCount()) {
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
	tcp->th_flags |= TH_SYN;
	tcp->th_flags &= ~TH_ACK;
    } else {
        //If not sync, by default we are sending an ack
	tcp->th_flags |= TH_ACK;
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
        TcpHdr(htonl(pkt_info_->ip_daddr), ntohs(tcp->th_dport),
               htonl(pkt_info_->ip_saddr), ntohs(tcp->th_sport),
               false, ntohs(tcp->th_ack),
               ntohs(pkt_info_->ip->ip_len) - sizeof(struct ip));
    } else if(pkt_info_->ip_proto == IPPROTO_UDP) {
        udphdr *udp = pkt_info_->transp.udp;
        UdpHdr(ntohs(udp->uh_ulen), pkt_info_->ip_daddr, ntohs(udp->uh_dport),
               pkt_info_->ip_saddr, ntohs(udp->uh_sport));
    }
}

void DiagPktHandler::SwapIpHdr() {
    //IpHdr expects IP address to be in network format
    ip *ip = pkt_info_->ip;
    IpHdr(ntohs(ip->ip_len), ip->ip_dst.s_addr, ip->ip_src.s_addr, ip->ip_p);
}

void DiagPktHandler::SwapEthHdr() {
    ether_header *eth = pkt_info_->eth;
    EthHdr(MacAddress(eth->ether_dhost), MacAddress(eth->ether_shost),
           ntohs(eth->ether_type));
}

void DiagPktHandler::Swap() {
    SwapL4();
    SwapIpHdr();
    SwapEthHdr();
}

