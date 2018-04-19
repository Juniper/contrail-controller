/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "base/os.h"
#include <netinet/icmp6.h>
#include <vr_defs.h>
#include <cmn/agent_cmn.h>
#include <oper/interface_common.h>
#include <oper/vn.h>
#include <pkt/pkt_init.h>
#include <services/icmpv6_error_proto.h>
#include <services/icmpv6_error_handler.h>
#include <services/icmpv6_handler.h>
#include <services/services_init.h>
#include <services/icmpv6_proto.h>


extern SandeshTraceBufferPtr Icmpv6TraceBuf;

Icmpv6ErrorHandler::Icmpv6ErrorHandler(Agent *agent, Icmpv6ErrorProto *proto,
                                       boost::shared_ptr<PktInfo> info,
                                       boost::asio::io_service *io) :
    ProtoHandler(agent, info, *io), proto_(proto) {
}

Icmpv6ErrorHandler::~Icmpv6ErrorHandler() {
}

bool Icmpv6ErrorHandler::ValidatePacket() {
    if (pkt_info_->len < (sizeof(struct ether_header) + sizeof(struct ip6_hdr)))
        return false;
    return true;
}

bool Icmpv6ErrorHandler::Run() {

    if (ValidatePacket() == false) {
        proto_->increment_drops();
        return true;
    }

    VmInterface *vm_itf = dynamic_cast<VmInterface *>
        (agent()->interface_table()->FindInterface(GetInterfaceIndex()));
    if (vm_itf == NULL || vm_itf->layer3_forwarding() == false ||
        vm_itf->IsActive() == false) {
        proto_->increment_interface_errors();
        return true;
    }
    return SendIcmpv6Error(vm_itf);
}

// Generate ICMP error
bool Icmpv6ErrorHandler::SendIcmpv6Error(VmInterface *intf) {

    char *buff = (char *)pkt_info_->pkt;
    uint16_t buff_len = pkt_info_->packet_buffer()->data_len();
    char data[ICMPV6_PAYLOAD_LEN];
    int len = ntohs(pkt_info_->ip6->ip6_plen);

    if (len > ICMPV6_PAYLOAD_LEN)
        len = ICMPV6_PAYLOAD_LEN;

    memcpy(data, pkt_info_->ip6, len);

    Ip6Address source_address;
    boost::system::error_code ec;
    const VnIpam *ipam = intf->vn()->GetIpam(pkt_info_->ip_saddr.to_v6());
    if (ipam != NULL) {
        if (ipam->default_gw.is_v6())
            source_address = ipam->default_gw.to_v6();
    }
    // If source address is not found, use vhost address mapped v6 address.
    // vrouter forwards the icmp error packets as part of the original flow.
    if (source_address.is_unspecified())
        source_address = Ip6Address::v4_mapped(agent()->router_id());

    uint32_t interface =
        (pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_TOR_CONTROL_PKT) ?
        pkt_info_->agent_hdr.cmd_param : GetInterfaceIndex();

    uint16_t eth_len = EthHdr(buff, buff_len, interface,
                              agent()->vhost_interface()->mac(),
                              MacAddress(pkt_info_->eth->ether_shost),
                              ETHERTYPE_IPV6);

    pkt_info_->ip6 = (struct ip6_hdr *)(buff + eth_len);
    Ip6Hdr(pkt_info_->ip6, len+ICMP_UNREACH_HDR_LEN, IPV6_ICMP_NEXT_HEADER,
           255, source_address.to_bytes().data(),
           pkt_info_->ip_saddr.to_v6().to_bytes().data());

    icmp6_hdr *icmp = pkt_info_->transp.icmp6 =
        (icmp6_hdr *)(pkt_info_->pkt + eth_len
                      + sizeof(ip6_hdr));
    icmp->icmp6_type = ICMP6_PACKET_TOO_BIG;
    icmp->icmp6_code = 0;
    icmp->icmp6_mtu = htonl(pkt_info_->agent_hdr.mtu);
    icmp->icmp6_cksum = 0;
    memcpy(buff + sizeof(ip6_hdr) + eth_len+ICMP_UNREACH_HDR_LEN, data, len);
    icmp->icmp6_cksum =
        Icmpv6Csum(source_address.to_bytes().data(),
                   pkt_info_->ip_saddr.to_v6().to_bytes().data(),
                   icmp, len + ICMP_UNREACH_HDR_LEN);
    pkt_info_->set_len(len + sizeof(ip6_hdr) + eth_len+ICMP_UNREACH_HDR_LEN);

    uint16_t command =
        (pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_TOR_CONTROL_PKT) ?
        (uint16_t)AgentHdr::TX_ROUTE : AgentHdr::TX_SWITCH;

    Send(GetInterfaceIndex(), pkt_info_->vrf, command,
         PktHandler::ICMPV6);
    return true;
}
