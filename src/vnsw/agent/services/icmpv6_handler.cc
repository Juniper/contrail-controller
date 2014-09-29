/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <netinet/icmp6.h>

#include <vr_defs.h>
#include <cmn/agent_cmn.h>
#include <pkt/pkt_init.h>
#include <oper/interface_common.h>
#include "services/services_types.h"
#include <services/services_init.h>
#include <services/icmpv6_proto.h>
#include <oper/vn.h>

Icmpv6Handler::Icmpv6Handler(Agent *agent, boost::shared_ptr<PktInfo> info,
                             boost::asio::io_service &io)
    : ProtoHandler(agent, info, io), icmp_(pkt_info_->transp.icmp6) {
    // payload length - length of ipv6 extension headers
    if (icmp_)
        icmp_len_ = ntohs(pkt_info_->ip6->ip6_plen) + sizeof(ip6_hdr) -
                    ((uint8_t *)icmp_ - (uint8_t *)pkt_info_->ip6);
    else
        icmp_len_ = 0;
}

Icmpv6Handler::~Icmpv6Handler() {
}

bool Icmpv6Handler::Run() {
    Icmpv6Proto *icmpv6_proto = agent()->icmpv6_proto();
    Interface *itf =
        agent()->interface_table()->FindInterface(GetInterfaceIndex());
    if (itf == NULL) {
        icmpv6_proto->IncrementStatsDrop();
        ICMPV6_TRACE(Trace, "Received ICMP from invalid interface");
        return true;
    }
    if (itf->type() != Interface::VM_INTERFACE) {
        icmpv6_proto->IncrementStatsDrop();
        ICMPV6_TRACE(Trace, "Received ICMP from non-vm interface");
        return true;
    }
    VmInterface *vm_itf = static_cast<VmInterface *>(itf);
    if (!vm_itf->layer3_forwarding()) {
        icmpv6_proto->IncrementStatsDrop();
        ICMPV6_TRACE(Trace, "Received ICMP with l3 disabled");
        return true;
    }
    switch (icmp_->icmp6_type) {
        case ND_ROUTER_SOLICIT:
            icmpv6_proto->IncrementStatsRouterSolicit();
            if (CheckPacket()) {
                Ip6Address prefix;
                uint8_t plen;
                if (vm_itf->vn() &&
                    vm_itf->vn()->GetPrefix(vm_itf->ip6_addr(), &prefix, &plen)) {
                    boost::system::error_code ec;
                    Ip6Address src_addr = Ip6Address::from_string(PKT0_LINKLOCAL_ADDRESS, ec);
                    SendRAResponse(pkt_info_->GetAgentHdr().ifindex,
                                   pkt_info_->vrf,
                                   src_addr.to_bytes().data(),
                                   pkt_info_->ip_saddr.to_v6().to_bytes().data(),
                                   MacAddress(pkt_info_->eth->h_source), prefix, plen);
                    icmpv6_proto->IncrementStatsRouterAdvert();
                    return true;
                }
                ICMPV6_TRACE(Trace, "Ignoring ND Router Solicit : VN / prefix not present");
            } else {
                ICMPV6_TRACE(Trace, "Ignoring Echo request with wrong cksum");
            }
            break;

        case ICMP6_ECHO_REQUEST:
            icmpv6_proto->IncrementStatsPingRequest();
            if (CheckPacket()) {
                SendPingResponse();
                icmpv6_proto->IncrementStatsPingResponse();
                return true;
            }
            ICMPV6_TRACE(Trace, "Ignoring Echo request with wrong cksum");
            break;

        default:
            break;
    }
    icmpv6_proto->IncrementStatsDrop();
    return true;
}

bool Icmpv6Handler::RouterAdvertisement(Icmpv6Proto *proto) {
    const Icmpv6Proto::VmInterfaceSet &interfaces = proto->vm_interfaces();
    boost::system::error_code ec;
    Ip6Address src_addr = Ip6Address::from_string(PKT0_LINKLOCAL_ADDRESS, ec);
    Ip6Address dest_addr = Ip6Address::from_string(IPV6_ALL_NODES_ADDRESS, ec);
    // Ethernet mcast address corresponding to IPv6 mcast address ff02::1
    MacAddress dest_mac(0x33, 0x33, 0x00, 0x00, 0x00, 0x01);
    for (Icmpv6Proto::VmInterfaceSet::const_iterator it = interfaces.begin();
         it != interfaces.end(); ++it) {
        if ((*it)->IsIpv6Active()) {
            pkt_info_->AllocPacketBuffer(agent(), PktHandler::ICMPV6, ICMP_PKT_SIZE, 0);
            pkt_info_->eth = (ethhdr *)(pkt_info_->pkt);
            pkt_info_->ip6 = (ip6_hdr *)(pkt_info_->pkt + sizeof(ethhdr));
            icmp_ = pkt_info_->transp.icmp6 =
                (icmp6_hdr *)(pkt_info_->pkt + sizeof(ethhdr) + sizeof(ip6_hdr));
            Ip6Address prefix;
            uint8_t plen;
            if ((*it)->vn()->GetPrefix((*it)->ip6_addr(), &prefix, &plen)) {
                SendRAResponse((*it)->id(), (*it)->vrf()->vrf_id(),
                               src_addr.to_bytes().data(),
                               dest_addr.to_bytes().data(),
                               dest_mac, prefix, plen);
                proto->IncrementStatsRouterAdvert();
            }
        }
    }

    return true;
}

bool Icmpv6Handler::CheckPacket() {
    if (pkt_info_->len < (sizeof(ethhdr) + sizeof(ip6_hdr) +
                          ntohs(pkt_info_->ip6->ip6_plen)))
        return false;

    uint16_t checksum = icmp_->icmp6_cksum;
    icmp_->icmp6_cksum = 0;
    if (checksum == Icmpv6Csum(pkt_info_->ip6->ip6_src.s6_addr,
                               pkt_info_->ip6->ip6_dst.s6_addr,
                               icmp_, icmp_len_))
        return true;

    return false;
}

uint16_t Icmpv6Handler::FillRouterAdvertisement(uint8_t *buf, uint8_t *src,
                                                uint8_t *dest,
                                                const Ip6Address &prefix,
                                                uint8_t plen) {
    nd_router_advert *icmp = (nd_router_advert *)buf;
    icmp->nd_ra_type = ND_ROUTER_ADVERT;
    icmp->nd_ra_code = 0;
    icmp->nd_ra_cksum = 0;
    icmp->nd_ra_curhoplimit = 64;
    icmp->nd_ra_flags_reserved = ND_RA_FLAG_MANAGED | ND_RA_FLAG_OTHER; //DHCPv6
    icmp->nd_ra_router_lifetime = htons(9000);
    icmp->nd_ra_reachable = 0;
    icmp->nd_ra_retransmit = 0;

    // add source linklayer address information
    uint16_t offset = sizeof(nd_router_advert);
    nd_opt_hdr *src_linklayer_addr = (nd_opt_hdr *)(buf + offset);
    src_linklayer_addr->nd_opt_type = ND_OPT_SOURCE_LINKADDR;
    src_linklayer_addr->nd_opt_len = 1;
    //XXX instead of ETHER_ADDR_LEN, actual buffer size should be given
    //to preven buffer overrun.
    agent()->vrrp_mac().ToArray(buf + offset + 2, ETHER_ADDR_LEN);

    // add prefix information
    offset += sizeof(nd_opt_hdr) + ETH_ALEN;
    nd_opt_prefix_info *prefix_info = (nd_opt_prefix_info *)(buf + offset);
    prefix_info->nd_opt_pi_type = ND_OPT_PREFIX_INFORMATION;
    prefix_info->nd_opt_pi_len = 4;
    prefix_info->nd_opt_pi_prefix_len = plen;
    // not setting ND_OPT_PI_FLAG_AUTO or ND_OPT_PI_FLAG_RADDR
    prefix_info->nd_opt_pi_flags_reserved = ND_OPT_PI_FLAG_ONLINK;
    prefix_info->nd_opt_pi_valid_time = htonl(0xFFFFFFFF);
    prefix_info->nd_opt_pi_preferred_time = htonl(0xFFFFFFFF);
    prefix_info->nd_opt_pi_reserved2 = 0;
    memcpy(prefix_info->nd_opt_pi_prefix.s6_addr, prefix.to_bytes().data(), 16);

    offset += sizeof(nd_opt_prefix_info);
    icmp->nd_ra_cksum = Icmpv6Csum(src, dest, (icmp6_hdr *)icmp, offset);

    return offset;
}

void Icmpv6Handler::SendRAResponse(uint16_t ifindex, uint16_t vrfindex,
                                   uint8_t *src_ip, uint8_t *dest_ip,
                                   const MacAddress &dest_mac,
                                   const Ip6Address &prefix, uint8_t plen) {
    // fill in the response
    uint16_t len = FillRouterAdvertisement((uint8_t *)icmp_, src_ip, dest_ip,
                                           prefix, plen);
    SendIcmpv6Response(ifindex, vrfindex, src_ip, dest_ip, dest_mac, len);
}

void Icmpv6Handler::SendPingResponse() {
    icmp_->icmp6_type = ICMP6_ECHO_REPLY;
    icmp_->icmp6_cksum = 0;
    icmp_->icmp6_cksum =
        Icmpv6Csum(pkt_info_->ip_daddr.to_v6().to_bytes().data(),
                   pkt_info_->ip_saddr.to_v6().to_bytes().data(),
                   icmp_, pkt_info_->ip6->ip6_plen);
    SendIcmpv6Response(pkt_info_->GetAgentHdr().ifindex, pkt_info_->vrf,
                       pkt_info_->ip_daddr.to_v6().to_bytes().data(),
                       pkt_info_->ip_saddr.to_v6().to_bytes().data(),
                       MacAddress(pkt_info_->eth->h_source),
                       pkt_info_->ip6->ip6_plen);
}

void Icmpv6Handler::SendIcmpv6Response(uint16_t ifindex, uint16_t vrfindex,
                                       uint8_t *src_ip, uint8_t *dest_ip,
                                       const MacAddress &dest_mac,
                                       uint16_t len) {
    Ip6Hdr(pkt_info_->ip6, len, IPV6_ICMP_NEXT_HEADER, 255, src_ip, dest_ip);
    len += sizeof(ip6_hdr);
    EthHdr(agent()->vrrp_mac(), dest_mac, ETHERTYPE_IPV6);
    len += sizeof(ethhdr);
    pkt_info_->set_len(len);

    Send(ifindex, vrfindex, AgentHdr::TX_SWITCH, PktHandler::ICMPV6);
}
