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
    icmp_len_ = ntohs(pkt_info_->ip->ip_len) - (pkt_info_->ip->ip_hl * 4);
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
    switch (icmp_->icmp_type) {
        case ICMP_ECHO:
            if (CheckPacket()) {
                icmp_proto->IncrStatsGwPing();
                SendResponse(vm_itf);
            } else
                icmp_proto->IncrStatsGwPingErr();
            return true;

        default:
            icmp_proto->IncrStatsDrop();
            return true;
    }
}

bool IcmpHandler::CheckPacket() {
    if (pkt_info_->len < (EncapHeaderLen() + sizeof(ether_header) +
                          ntohs(pkt_info_->ip->ip_len)))
        return false;

    uint16_t checksum = icmp_->icmp_cksum;
    icmp_->icmp_cksum = 0;

    if (checksum == Csum((uint16_t *)icmp_, icmp_len_, 0))
        return true;

    return false;
}

void IcmpHandler::SendResponse(VmInterface *vm_intf) {
    // Max size of buffer
    char *ptr = (char *)pkt_info_->pkt;
    uint16_t buf_len = pkt_info_->max_pkt_len;

    // Copy the ICMP payload
    char icmp_payload[icmp_len_];
    memcpy(icmp_payload, icmp_, icmp_len_);

    // Retain the agent-header before ethernet header
    uint16_t len = (char *)pkt_info_->eth - (char *)pkt_info_->pkt;

    // Form ICMP Packet with following
    // EthHdr - IP Header - ICMP Header
    len += EthHdr(ptr + len, buf_len - len,
                  agent()->vhost_interface()->mac(),
                  MacAddress(pkt_info_->eth->ether_shost), IP_PROTOCOL, vm_intf->vlan_id());

    uint16_t ip_len = sizeof(struct ip) + icmp_len_;

    len += IpHdr(ptr + len, buf_len - len, ip_len, htonl(pkt_info_->ip_daddr),
                 htonl(pkt_info_->ip_saddr), IPPROTO_ICMP);

    // Restore the ICMP header copied earlier
    struct icmp *hdr = (struct icmp *) (ptr + len);
    memcpy(ptr + len, icmp_payload, icmp_len_);
    len += icmp_len_;

    // Change type to reply
    hdr->icmp_type = ICMP_ECHOREPLY;
    // Recompute ICMP checksum
    IcmpChecksum((char *)hdr, icmp_len_);

    Send(len, GetInterfaceIndex(), pkt_info_->vrf, AgentHdr::TX_SWITCH,
         PktHandler::ICMP);
}

