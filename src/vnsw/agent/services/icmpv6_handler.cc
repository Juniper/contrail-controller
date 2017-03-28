/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <netinet/icmp6.h>

#include <vr_defs.h>
#include <cmn/agent_cmn.h>
#include <pkt/pkt_init.h>
#include <oper/interface_common.h>
#include "services/services_types.h"
#include <services/services_init.h>
#include <services/icmpv6_proto.h>
#include <oper/route_common.h>
#include <oper/operdb_init.h>
#include <oper/path_preference.h>
#include <oper/vn.h>

const boost::array<uint8_t, 16> Icmpv6Handler::kPrefix =
    { {0xFF, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0xFF, 0, 0, 0} };
const boost::array<uint8_t, 16> Icmpv6Handler::kSuffix =
    { {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF} };
const Ip6Address Icmpv6Handler::kSolicitedNodeIpPrefix(kPrefix);
const Ip6Address Icmpv6Handler::kSolicitedNodeIpSuffixMask(kSuffix);

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
    if (!vm_itf->layer3_forwarding() || !vm_itf->ipv6_active()) {
        icmpv6_proto->IncrementStatsDrop();
        ICMPV6_TRACE(Trace, "Received ICMP with l3 disabled / ipv6 inactive");
        return true;
    }
    nd_neighbor_advert *icmp = (nd_neighbor_advert *)icmp_;
    switch (icmp_->icmp6_type) {
        case ND_ROUTER_SOLICIT:
            icmpv6_proto->IncrementStatsRouterSolicit(vm_itf);
            if (CheckPacket()) {
                Ip6Address prefix;
                uint8_t plen;
                if (vm_itf->vn() &&
                    vm_itf->vn()->GetPrefix(vm_itf->primary_ip6_addr(),
                                            &prefix, &plen)) {
                    boost::system::error_code ec;
                    Ip6Address src_addr = Ip6Address::from_string(PKT0_LINKLOCAL_ADDRESS, ec);
                    uint32_t interface =
                        (pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_TOR_CONTROL_PKT) ?
                        pkt_info_->agent_hdr.cmd_param : GetInterfaceIndex();
                    VmInterface *vmi = NULL;
                    Interface *intf =
                        agent()->interface_table()->FindInterface(interface);
                    if (intf->type() == Interface::VM_INTERFACE) {
                        vmi = static_cast<VmInterface *>(intf);
                    }
                    SendRAResponse(interface,
                                   pkt_info_->vrf,
                                   src_addr.to_bytes().data(),
                                   pkt_info_->ip_saddr.to_v6().to_bytes().data(),
                                   MacAddress(pkt_info_->eth->ether_shost), prefix, plen);
                    icmpv6_proto->IncrementStatsRouterAdvert(vmi);
                    return true;
                }
                ICMPV6_TRACE(Trace, "Ignoring ND Router Solicit : VN / prefix not present");
            } else {
                ICMPV6_TRACE(Trace, "Ignoring Echo request with wrong cksum");
            }
            break;

        case ICMP6_ECHO_REQUEST:
            icmpv6_proto->IncrementStatsPingRequest(vm_itf);
            if (CheckPacket()) {
                SendPingResponse();
                icmpv6_proto->IncrementStatsPingResponse(vm_itf);
                return true;
            }
            ICMPV6_TRACE(Trace, "Ignoring Echo request with wrong cksum");
            break;

        case ND_NEIGHBOR_ADVERT:
            if (icmp->nd_na_flags_reserved & ND_NA_FLAG_SOLICITED) {
                icmpv6_proto->IncrementStatsNeighborAdvertSolicited(vm_itf);
            } else {
                icmpv6_proto->IncrementStatsNeighborAdvertUnSolicited(vm_itf);
            }
            if (CheckPacket()) {
                boost::array<uint8_t, 16> bytes;
                for (int i = 0; i < 16; i++) {
                    bytes[i] = icmp->nd_na_target.s6_addr[i];
                }
                Ip6Address addr(bytes);
                uint16_t offset = sizeof(nd_neighbor_advert);
                nd_opt_hdr *opt = (nd_opt_hdr *) (((uint8_t *)icmp) + offset);
                if (opt->nd_opt_type != ND_OPT_TARGET_LINKADDR) {
                    ICMPV6_TRACE(Trace, "Ignoring Neighbor Advert with no"
                                 "Target Link-layer address option");
                    icmpv6_proto->IncrementStatsDrop();
                    return true;
                }

                uint8_t *buf = (((uint8_t *)icmp) + offset + 2);
                MacAddress mac(buf);

                //Enqueue a request to trigger state machine
                agent()->oper_db()->route_preference_module()->
                    EnqueueTrafficSeen(addr, 128, itf->id(),
                                       itf->vrf()->vrf_id(), mac);
                return true;
            }
            ICMPV6_TRACE(Trace, "Ignoring Neighbor Solicit with wrong cksum");
            break;
        default:
            break;
    }
    icmpv6_proto->IncrementStatsDrop();
    return true;
}

bool Icmpv6Handler::RouterAdvertisement(Icmpv6Proto *proto) {
    const Icmpv6Proto::VmInterfaceMap &interfaces = proto->vm_interfaces();
    boost::system::error_code ec;
    Ip6Address src_addr = Ip6Address::from_string(PKT0_LINKLOCAL_ADDRESS, ec);
    Ip6Address dest_addr = Ip6Address::from_string(IPV6_ALL_NODES_ADDRESS, ec);
    // Ethernet mcast address corresponding to IPv6 mcast address ff02::1
    MacAddress dest_mac(0x33, 0x33, 0x00, 0x00, 0x00, 0x01);
    for (Icmpv6Proto::VmInterfaceMap::const_iterator it = interfaces.begin();
         it != interfaces.end(); ++it) {
        VmInterface *vmi = it->first;
        if (vmi->IsIpv6Active() && !vmi->HasServiceVlan()) {
            pkt_info_->AllocPacketBuffer(agent(), PktHandler::ICMPV6, ICMP_PKT_SIZE, 0);
            pkt_info_->eth = (struct ether_header *)(pkt_info_->pkt);
            pkt_info_->ip6 = (ip6_hdr *)(pkt_info_->pkt + sizeof(struct ether_header));
            uint32_t vlan_offset = 0;
            if (vmi->tx_vlan_id() != VmInterface::kInvalidVlanId)
                vlan_offset += 4;
            icmp_ = pkt_info_->transp.icmp6 =
                (icmp6_hdr *)(pkt_info_->pkt + sizeof(struct ether_header) +
                              vlan_offset + sizeof(ip6_hdr));
            Ip6Address prefix;
            uint8_t plen;
            if (vmi->vn()->GetPrefix(vmi->primary_ip6_addr(), &prefix, &plen)) {
                SendRAResponse(vmi->id(), vmi->vrf()->vrf_id(),
                               src_addr.to_bytes().data(),
                               dest_addr.to_bytes().data(),
                               dest_mac, prefix, plen);
                proto->IncrementStatsRouterAdvert(vmi);
            }
        }
    }

    return true;
}

bool Icmpv6Handler::CheckPacket() {
    if (pkt_info_->len < (sizeof(struct ether_header) + sizeof(ip6_hdr) +
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

uint16_t Icmpv6Handler::FillRouterAdvertisement(uint8_t *buf, uint32_t ifindex,
                                                uint8_t *src, uint8_t *dest,
                                                const Ip6Address &prefix,
                                                uint8_t plen) {
    nd_router_advert *icmp = (nd_router_advert *)buf;
    icmp->nd_ra_type = ND_ROUTER_ADVERT;
    icmp->nd_ra_code = 0;
    icmp->nd_ra_cksum = 0;
    icmp->nd_ra_curhoplimit = 64;
    icmp->nd_ra_flags_reserved = ND_RA_FLAG_MANAGED | ND_RA_FLAG_OTHER; //DHCPv6
    icmp->nd_ra_reachable = 0;
    icmp->nd_ra_retransmit = 0;

    bool def_gw = IsDefaultGatewayConfigured(ifindex, prefix);
    if (def_gw) {
        icmp->nd_ra_router_lifetime = htons(9000);
    } else {
        icmp->nd_ra_router_lifetime = 0;
    }

    // add source linklayer address information
    uint16_t offset = sizeof(nd_router_advert);
    nd_opt_hdr *src_linklayer_addr = (nd_opt_hdr *)(buf + offset);
    src_linklayer_addr->nd_opt_type = ND_OPT_SOURCE_LINKADDR;
    src_linklayer_addr->nd_opt_len = 1;
    //XXX instead of ETHER_ADDR_LEN, actual buffer size should be given
    //to preven buffer overrun.
    agent()->vrrp_mac().ToArray(buf + offset + 2, ETHER_ADDR_LEN);

    // add prefix information
    offset += sizeof(nd_opt_hdr) + ETHER_ADDR_LEN;
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

void Icmpv6Handler::SendRAResponse(uint32_t ifindex, uint32_t vrfindex,
                                   uint8_t *src_ip, uint8_t *dest_ip,
                                   const MacAddress &dest_mac,
                                   const Ip6Address &prefix, uint8_t plen) {
    // fill in the response
    uint16_t len = FillRouterAdvertisement((uint8_t *)icmp_, ifindex, src_ip,
                                           dest_ip, prefix, plen);
    SendIcmpv6Response(ifindex, vrfindex, src_ip, dest_ip, dest_mac, len);
}

void Icmpv6Handler::SendPingResponse() {
    icmp_->icmp6_type = ICMP6_ECHO_REPLY;
    icmp_->icmp6_cksum = 0;
    icmp_->icmp6_cksum =
        Icmpv6Csum(pkt_info_->ip_daddr.to_v6().to_bytes().data(),
                   pkt_info_->ip_saddr.to_v6().to_bytes().data(),
                   icmp_, ntohs(pkt_info_->ip6->ip6_plen));
    uint32_t interface =
        (pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_TOR_CONTROL_PKT) ?
        pkt_info_->agent_hdr.cmd_param : GetInterfaceIndex();
    SendIcmpv6Response(interface, pkt_info_->vrf,
                       pkt_info_->ip_daddr.to_v6().to_bytes().data(),
                       pkt_info_->ip_saddr.to_v6().to_bytes().data(),
                       MacAddress(pkt_info_->eth->ether_shost),
                       ntohs(pkt_info_->ip6->ip6_plen));
}

void Icmpv6Handler::SendIcmpv6Response(uint32_t ifindex, uint32_t vrfindex,
                                       uint8_t *src_ip, uint8_t *dest_ip,
                                       const MacAddress &dest_mac,
                                       uint16_t len) {

    char *buff = (char *)pkt_info_->pkt;
    uint16_t buff_len = pkt_info_->packet_buffer()->data_len();
    char icmpv6_payload[icmp_len_];
    memcpy(icmpv6_payload,icmp_,icmp_len_);
    uint16_t eth_len = EthHdr(buff, buff_len, ifindex, agent()->vrrp_mac(),
                              dest_mac, ETHERTYPE_IPV6);

    pkt_info_->ip6 = (struct ip6_hdr *)(buff + eth_len);
    Ip6Hdr(pkt_info_->ip6, len, IPV6_ICMP_NEXT_HEADER, 255, src_ip, dest_ip);
    memcpy(buff + sizeof(ip6_hdr) + eth_len, icmpv6_payload, icmp_len_);
    pkt_info_->set_len(len + sizeof(ip6_hdr) + eth_len);
    uint16_t command =
        (pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_TOR_CONTROL_PKT) ?
        (uint16_t)AgentHdr::TX_ROUTE : AgentHdr::TX_SWITCH;
    Send(ifindex, vrfindex, command, PktHandler::ICMPV6);
}

uint16_t Icmpv6Handler::FillNeighborSolicit(uint8_t *buf,
                                            const Ip6Address &target,
                                            uint8_t *sip, uint8_t *dip) {
    nd_neighbor_solicit *icmp = (nd_neighbor_solicit *)buf;
    icmp->nd_ns_type = ND_NEIGHBOR_SOLICIT;
    icmp->nd_ns_code = 0;
    icmp->nd_ns_cksum = 0;
    icmp->nd_ns_reserved = 0;
    memcpy(icmp->nd_ns_target.s6_addr, target.to_bytes().data(), 16);
    uint16_t offset = sizeof(nd_neighbor_solicit);

    // add source linklayer address information
    nd_opt_hdr *src_linklayer_addr = (nd_opt_hdr *)(buf + offset);
    src_linklayer_addr->nd_opt_type = ND_OPT_SOURCE_LINKADDR;
    src_linklayer_addr->nd_opt_len = 1;
    //XXX instead of ETHER_ADDR_LEN, actual buffer size should be given
    //to preven buffer overrun.
    agent()->vrrp_mac().ToArray(buf + offset + 2, ETHER_ADDR_LEN);

    offset += sizeof(nd_opt_hdr) + ETHER_ADDR_LEN;

    icmp->nd_ns_cksum = Icmpv6Csum(sip, dip, (icmp6_hdr *)icmp, offset);
    return offset;
}

void Icmpv6Handler::Ipv6Lower24BitsExtract(uint8_t *dst, uint8_t *src) {
    for (int i = 0; i < 16; i++) {
        dst[i] &= src[i];
    }
}

void Icmpv6Handler::Ipv6AddressBitwiseOr(uint8_t *dst, uint8_t *src) {
    for (int i = 0; i < 16; i++) {
        dst[i] |= src[i];
    }
}

void Icmpv6Handler::SolicitedMulticastIpAndMac(const Ip6Address &dip,
                                               uint8_t *ip, MacAddress &mac) {
    /* A solicited-node multicast address is formed by taking the low-order
     * 24 bits of an address (unicast or anycast) and appending those bits to
     * the prefix FF02:0:0:0:0:1:FF00::/104 */

    /* Copy the higher order 104 bits of solicited node multicast IP */
    memcpy(ip, kSolicitedNodeIpPrefix.to_bytes().data(), 16);

    uint8_t ip_bytes[16], suffix_mask_bytes[16];
    memcpy(ip_bytes, dip.to_bytes().data(), 16);
    memcpy(suffix_mask_bytes, kSolicitedNodeIpSuffixMask.to_bytes().data(), 16);
    /* Extract lower order 24 bits of Destination IP */
    Ipv6Lower24BitsExtract(ip_bytes, suffix_mask_bytes);

    /* Build the solicited node multicast address by joining upper order 104
     * bits of FF02:0:0:0:0:1:FF00::/104 with lower 24 bits of destination IP*/
    Ipv6AddressBitwiseOr(ip, ip_bytes);

    /* The ethernet address for IPv6 multicast address is 0x33-33-mm-mm-mm-mm,
     * where mm-mm-mm-mm is a direct mapping of the last 32 bits of the
     * IPv6 multicast address */
    mac[0] = mac[1] = 0x33;
    mac[2] = ip[12];
    mac[3] = ip[13];
    mac[4] = ip[14];
    mac[5] = ip[15];
}

void Icmpv6Handler::SendNeighborSolicit(const Ip6Address &sip,
                                        const Ip6Address &dip,
                                        const VmInterface *vmi,
                                        uint32_t vrf) {
    if (pkt_info_->packet_buffer() == NULL) {
        pkt_info_->AllocPacketBuffer(agent(), PktHandler::ICMPV6, ICMP_PKT_SIZE,
                                     0);
    }

    pkt_info_->eth = (struct ether_header *)(pkt_info_->pkt);
    pkt_info_->ip6 = (ip6_hdr *)(pkt_info_->pkt + sizeof(struct ether_header));
    uint32_t vlan_offset = 0;
    if (vmi->tx_vlan_id() != VmInterface::kInvalidVlanId)
        vlan_offset += 4;
    icmp_ = pkt_info_->transp.icmp6 =
            (icmp6_hdr *)(pkt_info_->pkt + sizeof(struct ether_header) +
                          vlan_offset + sizeof(ip6_hdr));
    uint8_t solicited_mcast_ip[16], source_ip[16];
    MacAddress dmac;
    memcpy(source_ip, sip.to_bytes().data(), sizeof(source_ip));
    SolicitedMulticastIpAndMac(dip, solicited_mcast_ip, dmac);
    uint16_t len = FillNeighborSolicit((uint8_t *)icmp_, dip, source_ip,
                                       solicited_mcast_ip);
    SendIcmpv6Response(vmi->id(), vrf, source_ip,
                       solicited_mcast_ip, dmac, len);
}

bool Icmpv6Handler::IsDefaultGatewayConfigured(uint32_t ifindex,
                                               const Ip6Address &addr) {
    Interface *intf = agent()->interface_table()->FindInterface(ifindex);
    if (!intf || intf->type() != Interface::VM_INTERFACE) {
        return false;
    }
    VmInterface *vmi = static_cast<VmInterface *>(intf);
    if (!vmi->vn()) {
        return false;
    }
    const VnIpam *ipam = vmi->vn()->GetIpam(addr);
    if (!ipam || !ipam->default_gw.is_v6()) {
        return false;
    }
    return !(ipam->default_gw.to_v6().is_unspecified());
}
