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
#include "diag/overlay_ping.h"
#include "oper/mirror_table.h"
#include <oper/bridge_route.h>
#include <oper/vxlan.h>
#include <netinet/icmp6.h>
#include "services/icmpv6_handler.h"

using namespace boost::posix_time; 
void DiagPktHandler::SetReply() {
    AgentDiagPktData *ad = (AgentDiagPktData *)pkt_info_->data;
    ad->op_ = htonl(AgentDiagPktData::DIAG_REPLY);
}

void DiagPktHandler::SetDiagChkSum() {
    uint8_t proto;
    if (pkt_info_->ip) {
        proto = pkt_info_->ip->ip_p;
    } else {
        proto = pkt_info_->ip6->ip6_nxt;
    }
    switch(proto) {
        case IPPROTO_TCP:
            pkt_info_->transp.tcp->th_sum = 0xffff;
            break;

        case IPPROTO_UDP:
            pkt_info_->transp.udp->uh_sum = 0xffff;
            break;

        case IPPROTO_ICMP:
            pkt_info_->transp.icmp->icmp_cksum = 0xffff;
            break;
        case IPPROTO_ICMPV6:
            pkt_info_->transp.icmp6->icmp6_cksum = 0xffff;
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

bool DiagPktHandler::IsOverlayPingPacket() {
    if (pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_ROUTER_ALERT)
        return true;
    return false;
}

bool DiagPktHandler::HandleTraceRoutePacket() {

    uint32_t rabit = 0;
    uint8_t ttl;
    if (pkt_info_->ether_type == ETHERTYPE_IP) {
        ttl = pkt_info_->ip->ip_ttl;
    } else if (pkt_info_->ether_type == ETHERTYPE_IPV6) {
        ttl = pkt_info_->ip6->ip6_ctlun.ip6_un1.ip6_un1_hlim;
    } else {
        return true;
    }

    if (ttl == 0 ) {
        if (pkt_info_->dport == VXLAN_UDP_DEST_PORT) {
            VxlanHdr *vxlan = (VxlanHdr *)(pkt_info_->transp.udp + 1);
            rabit = ntohl(vxlan->reserved) & OverlayPing::kVxlanRABit;
        }

        if (rabit) {
           SendOverlayResponse(); 
        } else {
            if (pkt_info_->ether_type == ETHERTYPE_IP) {
                SendTimeExceededPacket();
            } else {
                SendTimeExceededV6Packet();
            }
        }
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
    if (!ParseIcmpData(icmp_payload, icmp_len, (uint16_t *)&key, true))
        return;

    char *ptr = (char *)pkt_info_->pkt;
    uint16_t buf_len = pkt_info_->max_pkt_len;

    // Retain the agent-header before ethernet header
    uint16_t len = (char *)pkt_info_->eth - (char *)pkt_info_->pkt;

    Ip4Address src_ip(0);
    VmInterface *vm_itf = dynamic_cast<VmInterface *>
        (agent()->interface_table()->FindInterface(GetInterfaceIndex()));
    if (vm_itf == NULL) {
        src_ip = agent()->router_id();
    } else {
        if (vm_itf->vn() == NULL) {
            return;
        }
        const VnIpam *ipam = vm_itf->vn()->GetIpam(pkt_info_->ip_saddr.to_v4());
        if (ipam == NULL) {
            return;
        }
        src_ip = ipam->default_gw.to_v4();
    }

    // Form ICMP Packet with EthHdr - IP Header - ICMP Header
    len += EthHdr(ptr + len, buf_len - len, agent()->vhost_interface()->mac(),
                  MacAddress(pkt_info_->eth->ether_shost), ETHERTYPE_IP,
                  VmInterface::kInvalidVlanId);

    uint16_t ip_len = sizeof(iphdr) + 8 + icmp_len;
    len += IpHdr(ptr + len, buf_len - len, ip_len,
                 htonl(src_ip.to_ulong()),
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


void DiagPktHandler::SendTimeExceededV6Packet() {
    uint8_t icmp_payload[icmp_payload_len];
    uint16_t  icmp_len = ntohs(pkt_info_->ip6->ip6_plen);

    if (icmp_len > icmp_payload_len)
        icmp_len = icmp_payload_len;
    memcpy(icmp_payload, pkt_info_->ip6, icmp_len);

    DiagEntry::DiagKey key = -1;
    if (!ParseIcmpData(icmp_payload, icmp_len, (uint16_t *)&key, false))
        return;

    char *buff = (char *)pkt_info_->pkt;
    uint16_t buff_len = pkt_info_->max_pkt_len;
    // Retain the agent-header before ethernet header

    uint16_t eth_len = EthHdr(buff, buff_len, GetInterfaceIndex(),
                              agent()->vhost_interface()->mac(),
                              MacAddress(pkt_info_->eth->ether_shost),
                              ETHERTYPE_IPV6);

    VmInterface *vm_itf = dynamic_cast<VmInterface *>
        (agent()->interface_table()->FindInterface(GetInterfaceIndex()));
    if (vm_itf == NULL || vm_itf->layer3_forwarding() == false ||
        vm_itf->vn() == NULL) {
        return;
    }

    const VnIpam *ipam = vm_itf->vn()->GetIpam(pkt_info_->ip_saddr.to_v6());
    if (ipam == NULL)
        return ;

    pkt_info_->ip6 = (struct ip6_hdr *)(buff + eth_len);
    Ip6Hdr(pkt_info_->ip6, icmp_len+ICMP_UNREACH_HDR_LEN, IPV6_ICMP_NEXT_HEADER,
           DEFAULT_IP_TTL, ipam->default_gw.to_v6().to_bytes().data(),
           pkt_info_->ip_saddr.to_v6().to_bytes().data());

    icmp6_hdr *icmp = pkt_info_->transp.icmp6 =
        (icmp6_hdr *)(pkt_info_->pkt + eth_len
                      + sizeof(ip6_hdr));
    icmp->icmp6_type = ICMP_TIME_EXCEEDED;
    icmp->icmp6_code = ICMP_EXC_TTL;
    icmp->icmp6_mtu = 0;
    icmp->icmp6_cksum = 0;
    icmp->icmp6_cksum =
        Icmpv6Csum(ipam->default_gw.to_v6().to_bytes().data(),
                   pkt_info_->ip_saddr.to_v6().to_bytes().data(),
                   icmp, icmp_len);
    memcpy(buff + sizeof(ip6_hdr) + eth_len+ICMP_UNREACH_HDR_LEN,
           icmp_payload, icmp_len);
    pkt_info_->set_len(icmp_len + sizeof(ip6_hdr) + eth_len+ICMP_UNREACH_HDR_LEN);

    uint16_t command =
        (pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_TOR_CONTROL_PKT) ?
        (uint16_t)AgentHdr::TX_ROUTE : AgentHdr::TX_SWITCH;

    Send(GetInterfaceIndex(), pkt_info_->vrf, command,
         PktHandler::ICMPV6);

}

bool DiagPktHandler::HandleTraceRouteResponse() {
    uint8_t *data = (uint8_t *)(pkt_info_->transp.icmp) + 8;
    uint16_t len = pkt_info_->len - (data - pkt_info_->pkt);
    bool is_v4 = pkt_info_->ether_type == ETHERTYPE_IP;
    DiagEntry::DiagKey key = -1;
    // if it is Overlay packet get the key from Oam data
    if (IsOverlayPingPacket()) {
        OverlayOamPktData *oamdata = (OverlayOamPktData *) pkt_info_->data;
        key = ntohs(oamdata->org_handle_); 
    } else {
        if (!ParseIcmpData(data, len, (uint16_t *)&key, is_v4))
            return true;
    }

    DiagEntry *entry = diag_table_->Find(key);
    if (!entry) return true;

    if (is_v4)
        address_ = pkt_info_->ip_saddr.to_v4().to_string();
    else {
        address_ =  pkt_info_->ip_saddr.to_v6().to_string();
    }
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
                                   uint16_t *key, bool is_v4) {
    if (data_len < sizeof(struct ip))
        return false;
    struct ip *ip_hdr = (struct ip *)(data);
    *key = ntohs(ip_hdr->ip_id);
    uint16_t len = sizeof(struct ip);
    uint8_t protocol; 

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
        if (is_v4) {
            if (data_len < len + sizeof(struct ip))
                return true;
            ip_hdr = (struct ip *)(data + len);
            len += sizeof(struct ip);
            protocol = ip_hdr->ip_p;
        } else {
            if (data_len < len + sizeof(struct ip6_hdr))
                return true;
            struct ip6_hdr *ip6_hdr = (struct ip6_hdr *)(data + len);
            len += sizeof(struct ip6_hdr);
            protocol = ip6_hdr->ip6_nxt;
        }
        switch (protocol) {
            case IPPROTO_TCP:
                len += sizeof(tcphdr);
                break;

            case IPPROTO_UDP:
                len += sizeof(udphdr);
                break;

            case IPPROTO_ICMP:
                len += 8;
                break;
            case IPPROTO_ICMPV6:
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
    bool isoverlay_packet = IsOverlayPingPacket();
    AgentDiagPktData *ad = NULL;
    AgentDiagPktData tempdata;
    
    if (IsTraceRoutePacket()) {
        return HandleTraceRoutePacket();
    }

    if (isoverlay_packet) {
        // Process Overlay packet
        // papulate diag data AgentDiagPktData here
        memset(&tempdata, 0, sizeof(AgentDiagPktData));
        ad = &tempdata;
        OverlayOamPktData *oamdata = (OverlayOamPktData *) pkt_info_->data;
        if (oamdata->msg_type_ == OverlayOamPktData::OVERLAY_ECHO_REQUEST) {
            ad->op_ = htonl(AgentDiagPktData::DIAG_REQUEST);
        } else if (oamdata->msg_type_ == OverlayOamPktData::OVERLAY_ECHO_REPLY) {
            ad->op_ = htonl(AgentDiagPktData::DIAG_REPLY);
        }
        
        ad->key_ = oamdata->org_handle_;
    } else {
        ad = (AgentDiagPktData *)pkt_info_->data;
    }

    if (!ad) {
        //Ignore if packet doesnt have proper L4 header
        return true;
    }

    if (ntohl(ad->op_) == AgentDiagPktData::DIAG_REQUEST) {
        //Request received swap the packet
        //and dump the packet back
        if (isoverlay_packet) {
            // Reply packet with after setting the TLV content.
            SendOverlayResponse();
            return true;
        } 
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

void DiagPktHandler::TcpHdr(uint16_t len, const uint8_t *src, uint16_t sport,
                            const uint8_t *dest, uint16_t dport, bool is_syn,
                            uint32_t seq_no, uint8_t next_hdr){
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
    tcp->th_sum = Ipv6Csum(src, dest, len, next_hdr, (uint16_t *)tcp);
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
    }
     else if(pkt_info_->ip_proto == IPPROTO_UDP) {
        udphdr *udp = pkt_info_->transp.udp;
        UdpHdr(ntohs(udp->uh_ulen), pkt_info_->ip_daddr.to_v4().to_ulong(),
               ntohs(udp->uh_dport), pkt_info_->ip_saddr.to_v4().to_ulong(),
               ntohs(udp->uh_sport));
    }
}

void DiagPktHandler::Swapv6L4() {
    if (pkt_info_->ip_proto == IPPROTO_TCP) {
        tcphdr *tcp = pkt_info_->transp.tcp;
        TcpHdr(ntohs(pkt_info_->ip6->ip6_ctlun.ip6_un1.ip6_un1_plen) - sizeof(struct ip6_hdr),
               pkt_info_->ip_daddr.to_v6().to_bytes().data(), ntohs(tcp->th_dport),
               pkt_info_->ip_saddr.to_v6().to_bytes().data(), ntohs(tcp->th_sport),
               false, ntohs(tcp->th_ack), IPPROTO_TCP);
    } else if (pkt_info_->ip_proto == IPPROTO_UDP) {
        udphdr *udp = pkt_info_->transp.udp;
        UdpHdr(ntohs(udp->uh_ulen), pkt_info_->ip_daddr.to_v6().to_bytes().data(),
               ntohs(udp->uh_dport), pkt_info_->ip_saddr.to_v6().to_bytes().data(),
               ntohs(udp->uh_sport), IPPROTO_UDP);
    }
}

void DiagPktHandler::SwapIpHdr() {
    //IpHdr expects IP address to be in network format
    struct ip *ip = pkt_info_->ip;
    IpHdr(ntohs(ip->ip_len), ip->ip_dst.s_addr,
          ip->ip_src.s_addr, ip->ip_p, DEFAULT_IP_ID, DEFAULT_IP_TTL);
}

void DiagPktHandler::SwapIp6Hdr() {
    //Ip6Hdr expects IPv6 address to be in network format
    struct ip6_hdr  *ip6 = pkt_info_->ip6;
    Ip6Hdr(ip6, ntohl(ip6->ip6_ctlun.ip6_un1.ip6_un1_plen),
           ip6->ip6_ctlun.ip6_un1.ip6_un1_nxt, DEFAULT_IP_TTL,
           pkt_info_->ip_daddr.to_v6().to_bytes().data(),
           pkt_info_->ip_saddr.to_v6().to_bytes().data());
}

void DiagPktHandler::SwapEthHdr() {
    struct ether_header *eth = pkt_info_->eth;
    EthHdr(MacAddress(eth->ether_dhost), MacAddress(eth->ether_shost), ntohs(eth->ether_type));
}

void DiagPktHandler::Swap() {
    if (pkt_info_->ether_type == ETHERTYPE_IPV6) {
        Swapv6L4();
        SwapIp6Hdr();
    } else {
        SwapL4();
        SwapIpHdr();
    }
    SwapEthHdr();
}

void DiagPktHandler::SetReturnCode(OverlayOamPktData *oamdata) {
    oamdata->return_code_ = OverlayOamPktData::OVERLAY_SEGMENT_NOT_PRESENT;

    uint16_t type  = ntohs(oamdata->oamtlv_.type_);
    if (type != OamTlv::VXLAN_PING_IPv4 && type != OamTlv::VXLAN_PING_IPv6) {
        return;
    }

    const int tlv_length = ntohs(oamdata->oamtlv_.length_);
    // VXLAN VNI and IPv4 / IPv6 sender address
    int parsed_tlv = (type == OamTlv::VXLAN_PING_IPv4) ? 8 : 20;

    VxLanId *vxlan =
        Agent::GetInstance()->vxlan_table()->Find(pkt_info_->tunnel.vxlan_id);
    if (!vxlan) {
        return;
    }

    const VrfNH *vrf_nh = dynamic_cast<const VrfNH *> (vxlan->nexthop());
    if (!vrf_nh) {
        return;
    }

    const VrfEntry *vrf = vrf_nh->GetVrf();
    if (!vrf || vrf->IsDeleted()) {
        return;
    }

    oamdata->return_code_ = OverlayOamPktData::RETURN_CODE_OK;
    while (tlv_length - parsed_tlv >= (int) sizeof(SubTlv)) {
        SubTlv *subtlv = (SubTlv *) (oamdata->oamtlv_.data_ + parsed_tlv);
        parsed_tlv += sizeof(SubTlv);

        const int subtlv_length = ntohs(subtlv->length_);
        if (ntohs(subtlv->type_) != SubTlv::END_SYSTEM_MAC) {
            parsed_tlv += subtlv_length;
            continue;
        }

        int parsed_subtlv = 0;
        // check that we have bytes for MAC (6) and return code (2)
        while (subtlv_length - parsed_subtlv >=
               (int) sizeof(SubTlv::EndSystemMac)) {
            SubTlv::EndSystemMac *end_system_mac =
                (SubTlv::EndSystemMac *)
                (oamdata->oamtlv_.data_ + parsed_tlv + parsed_subtlv);
            MacAddress mac(end_system_mac->mac);
            BridgeAgentRouteTable *table =
                static_cast<BridgeAgentRouteTable *> (vrf->GetBridgeRouteTable());
            if (table->FindRoute(mac, Peer::EVPN_PEER) != NULL ||
                table->FindRoute(mac, Peer::LOCAL_VM_PORT_PEER) != NULL ||
                table->FindRoute(mac, Peer::LOCAL_VM_PEER) != NULL) {
                end_system_mac->return_code = htons(SubTlv::END_SYSTEM_PRESENT);
            } else {
                end_system_mac->return_code =
                    htons(SubTlv::END_SYSTEM_NOT_PRESENT);
            }
            parsed_subtlv += sizeof(SubTlv::EndSystemMac);
        }
        parsed_tlv += subtlv_length;
    }
}

void DiagPktHandler::TunnelHdrSwap() {
    struct ether_header *eth = pkt_info_->tunnel.eth;
    EthHdr((char *)eth, sizeof(struct ether_header), MacAddress(eth->ether_dhost), 
           MacAddress(eth->ether_shost), ntohs(eth->ether_type), 
           VmInterface::kInvalidVlanId);
    struct ip *ip = pkt_info_->tunnel.ip;
    IpHdr((char *)ip, sizeof(struct ip), ntohs(ip->ip_len),ip->ip_dst.s_addr, 
            ip->ip_src.s_addr, ip->ip_p, DEFAULT_IP_ID, DEFAULT_IP_TTL);
}

void DiagPktHandler::SendOverlayResponse() {
    Agent *agent = Agent::GetInstance();
    TunnelHdrSwap();

    OverlayOamPktData *oamdata = (OverlayOamPktData *) pkt_info_->data;
    if (oamdata->reply_mode_ == OverlayOamPktData::DONT_REPLY) {
        return ;
    }

    SetReturnCode(oamdata);

    oamdata->msg_type_ = OverlayOamPktData::OVERLAY_ECHO_REPLY;
    boost::posix_time::ptime
        epoch(boost::gregorian::date(1970, boost::gregorian::Jan, 1));
    boost::posix_time::ptime time = microsec_clock::universal_time();
    boost::posix_time::time_duration td = time - epoch;
    oamdata->timerecv_sec_ = htonl(td.total_seconds());
    oamdata->timerecv_misec_ = htonl(td.total_microseconds());

    PhysicalInterfaceKey key1(agent->fabric_interface_name());
    Interface *intf = static_cast<Interface *>
                (agent->interface_table()->Find(&key1, true));
    Send(intf->id(), agent->fabric_vrf()->vrf_id(),
         AgentHdr::TX_SWITCH, CMD_PARAM_PACKET_CTRL,
         CMD_PARAM_1_DIAG, PktHandler::DIAG);
}
