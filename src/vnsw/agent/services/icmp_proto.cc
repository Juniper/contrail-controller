/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/logging.h>
#include <cmn/agent_cmn.h>
#include <oper/interface.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>
#include "icmp_proto.h"

template<> Proto<IcmpHandler> *Proto<IcmpHandler>::instance_ = NULL;
IcmpHandler::IcmpStats IcmpHandler::stats_;

bool IcmpHandler::Run() {
    switch (icmp_->type) {
        case ICMP_ECHO:
            if (CheckPacket()) {
                stats_.IncrStatsGwPing();
                SendResponse();
            } else
                stats_.IncrStatsGwPingErr();
            return true;

        default:
            stats_.IncrStatsDrop();
            return true;
    }
}

bool IcmpHandler::CheckPacket() {
    if (pkt_info_->len < (IPC_HDR_LEN + sizeof(ethhdr) + 
                          ntohs(pkt_info_->ip->tot_len)))
        return false;

    uint16_t checksum = icmp_->checksum;
    icmp_->checksum = 0;
    if (checksum == Csum((uint16_t *)icmp_, icmp_len_, 0))
        return true;

    return false;
}

void IcmpHandler::SendResponse() {
    unsigned char src_mac[MAC_ALEN];
    unsigned char dest_mac[MAC_ALEN];

    memcpy(src_mac, pkt_info_->eth->h_dest, MAC_ALEN);
    memcpy(dest_mac, pkt_info_->eth->h_source, MAC_ALEN);

    // fill in the response
    uint16_t len = icmp_len_;
    icmp_->type = ICMP_ECHOREPLY;
    icmp_->checksum = Csum((uint16_t *)icmp_, icmp_len_, 0);

    len += sizeof(iphdr);
    IpHdr(len, htonl(pkt_info_->ip_daddr), 
          htonl(pkt_info_->ip_saddr), IPPROTO_ICMP);
    EthHdr(agent_vrrp_mac, pkt_info_->eth->h_source, 0x800);
    len += sizeof(ethhdr);

    Send(len, GetIntf(), pkt_info_->vrf, AGENT_CMD_SWITCH, PktHandler::ICMP);
}
