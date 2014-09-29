/*
 *  * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 *   */

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
    pkt_info_->ip->check = 0xffff;
}

void DiagPktHandler::Reply() {
    SetReply();
    Swap();
    SetDiagChkSum();
    pkt_info_->set_len(GetLength() - (2 * EncapHeaderLen()));
    Send(GetInterfaceIndex(), GetVrfIndex(), AgentHdr::TX_ROUTE,
         PktHandler::DIAG);
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
        TcpHdr(htonl(pkt_info_->ip_daddr.to_v4().to_ulong()), ntohs(tcp->dest),
               htonl(pkt_info_->ip_saddr.to_v4().to_ulong()),
               ntohs(tcp->source), false, ntohs(tcp->ack_seq),
               ntohs(pkt_info_->ip->tot_len) - sizeof(iphdr));

    } else if(pkt_info_->ip_proto == IPPROTO_UDP) {
        udphdr *udp = pkt_info_->transp.udp;
        UdpHdr(ntohs(udp->len), pkt_info_->ip_daddr.to_v4().to_ulong(),
               ntohs(udp->dest), pkt_info_->ip_saddr.to_v4().to_ulong(),
               ntohs(udp->source));
    }
}

void DiagPktHandler::SwapIpHdr() {
    //IpHdr expects IP address to be in network format
    iphdr *ip = pkt_info_->ip;
    IpHdr(ntohs(ip->tot_len), ip->daddr, ip->saddr, ip->protocol);
}

void DiagPktHandler::SwapEthHdr() {
    ethhdr *eth = pkt_info_->eth;
    EthHdr(MacAddress(eth->h_dest), MacAddress(eth->h_source), ntohs(eth->h_proto));
}

void DiagPktHandler::Swap() {
    SwapL4();
    SwapIpHdr();
    SwapEthHdr();
}
