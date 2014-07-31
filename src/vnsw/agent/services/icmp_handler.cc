/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vr_defs.h>
#include <cmn/agent_cmn.h>
#include <pkt/pkt_init.h>
#include <oper/interface_common.h>
#include <services/icmp_proto.h>

IcmpHandler::IcmpHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                         boost::asio::io_service &io) 
    : ProtoHandler(agent, info, io), icmp_(pkt_info_->transp.icmp) {
#if defined(__linux__)
    icmp_len_ = ntohs(pkt_info_->ip->tot_len) - (pkt_info_->ip->ihl * 4);
#elif defined(__FreeBSD__)
    icmp_len_ = ntohs(pkt_info_->ip->ip_len) - (pkt_info_->ip->ip_hl * 4);
#else
#error "Unsupported platform"
#endif
}

IcmpHandler::~IcmpHandler() {
}

bool IcmpHandler::Run() {
    IcmpProto *icmp_proto = agent()->GetIcmpProto();
    Interface *itf =
        agent()->interface_table()->FindInterface(GetInterfaceIndex());
    if (itf == NULL) {
        return true;
    }
    VmInterface *vm_itf = static_cast<VmInterface *>(itf);
    if (!vm_itf->ipv4_forwarding()) { 
        return true;
    } 
#if defined(__linux__)
    switch (icmp_->type) {
#elif defined(__FreeBSD__)
    switch (icmp_->icmp_type) {
#else
#error "Unsuppored platform"
#endif
        case ICMP_ECHO:
            if (CheckPacket()) {
                icmp_proto->IncrStatsGwPing();
                SendResponse();
            } else
                icmp_proto->IncrStatsGwPingErr();
            return true;

        default:
            icmp_proto->IncrStatsDrop();
            return true;
    }
}

bool IcmpHandler::CheckPacket() {
#if defined(__linux__)
    if (pkt_info_->len < (IPC_HDR_LEN + sizeof(ethhdr) +
                          ntohs(pkt_info_->ip->tot_len)))
        return false;

    uint16_t checksum = icmp_->checksum;
    icmp_->checksum = 0;
#elif defined(__FreeBSD__)
    if (pkt_info_->len < (IPC_HDR_LEN + sizeof(ether_header) +
                          ntohs(pkt_info_->ip->ip_len)))
        return false;

    uint16_t checksum = icmp_->icmp_cksum;
    icmp_->icmp_cksum = 0;
#else
#error "Unsupported platform"
#endif

    if (checksum == Csum((uint16_t *)icmp_, icmp_len_, 0))
        return true;

    return false;
}

void IcmpHandler::SendResponse() {
#if defined(__linux__)
    unsigned char src_mac[ETH_ALEN];
    unsigned char dest_mac[ETH_ALEN];

    memcpy(src_mac, pkt_info_->eth->h_dest, ETH_ALEN);
    memcpy(dest_mac, pkt_info_->eth->h_source, ETH_ALEN);

    // fill in the response
    uint16_t len = icmp_len_;
    icmp_->type = ICMP_ECHOREPLY;
    icmp_->checksum = Csum((uint16_t *)icmp_, icmp_len_, 0);

    len += sizeof(iphdr);
    IpHdr(len, htonl(pkt_info_->ip_daddr),
          htonl(pkt_info_->ip_saddr), IPPROTO_ICMP);
    EthHdr(agent()->vhost_interface()->mac().ether_addr_octet,
           pkt_info_->eth->h_source, IP_PROTOCOL);
    len += sizeof(ethhdr);
#elif defined(__FreeBSD__)
    unsigned char src_mac[ETHER_ADDR_LEN];
    unsigned char dest_mac[ETHER_ADDR_LEN];

    memcpy(src_mac, pkt_info_->eth->ether_dhost, ETHER_ADDR_LEN);
    memcpy(dest_mac, pkt_info_->eth->ether_shost, ETHER_ADDR_LEN);

    // fill in the response
    uint16_t len = icmp_len_;
    icmp_->icmp_type = ICMP_ECHOREPLY;
    icmp_->icmp_cksum = Csum((uint16_t *)icmp_, icmp_len_, 0);

    len += sizeof(ip);
    IpHdr(len, htonl(pkt_info_->ip_daddr),
          htonl(pkt_info_->ip_saddr), IPPROTO_ICMP);
    EthHdr(agent()->vhost_interface()->mac().octet,
           pkt_info_->eth->ether_shost, IP_PROTOCOL);
    len += sizeof(ether_header);
#else
#error "Unsupported platform"
#endif

    Send(len, GetInterfaceIndex(), pkt_info_->vrf,
         AGENT_CMD_SWITCH, PktHandler::ICMP);
}

