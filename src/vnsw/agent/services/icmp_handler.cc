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
    icmp_len_ = ntohs(pkt_info_->ip->tot_len) - (pkt_info_->ip->ihl * 4);
}

IcmpHandler::~IcmpHandler() {
}

bool IcmpHandler::Run() {
    IcmpProto *icmp_proto = agent()->GetIcmpProto();
    Interface *itf = agent()->GetInterfaceTable()->FindInterface(GetIntf());
    if (itf == NULL) {
        return true;
    }
    VmInterface *vm_itf = static_cast<VmInterface *>(itf);
    if (!vm_itf->ipv4_forwarding()) { 
        return true;
    } 
    switch (icmp_->type) {
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
    EthHdr(agent_vrrp_mac, pkt_info_->eth->h_source, 0x800);
    len += sizeof(ethhdr);

    Send(len, GetIntf(), pkt_info_->vrf, AGENT_CMD_SWITCH, PktHandler::ICMP);
}
