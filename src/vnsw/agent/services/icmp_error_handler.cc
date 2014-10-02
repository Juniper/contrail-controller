/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <vr_defs.h>
#include <cmn/agent_cmn.h>
#include <oper/interface_common.h>
#include <oper/vn.h>

#include <pkt/pkt_init.h>
#include <pkt/flow_table.h>
#include <services/icmp_error_proto.h>
#include <services/icmp_error_handler.h>

IcmpErrorHandler::IcmpErrorHandler(Agent *agent, IcmpErrorProto *proto,
                                   boost::shared_ptr<PktInfo> info,
                                   boost::asio::io_service *io) :
    ProtoHandler(agent, info, *io), proto_(proto) {
}

IcmpErrorHandler::~IcmpErrorHandler() {
}

bool IcmpErrorHandler::ValidatePacket() {
    if (pkt_info_->len < (sizeof(struct ether_header) + sizeof(struct ip)))
        return false;
    return true;
}

bool IcmpErrorHandler::Run() {
    if (ValidatePacket() == false) {
        proto_->increment_drops();
        return true;
    }

    VmInterface *vm_itf = dynamic_cast<VmInterface *>
        (agent()->interface_table()->FindInterface(GetInterfaceIndex()));
    if (vm_itf == NULL || vm_itf->layer3_forwarding() == false ||
        vm_itf->vn() == NULL) {
        proto_->increment_interface_errors();
        return true;
    }

    return SendIcmpError(vm_itf);
}

// Generate ICMP error
bool IcmpErrorHandler::SendIcmpError(VmInterface *intf) {
    char data[ICMP_PAYLOAD_LEN];
    uint16_t data_len = ntohs(pkt_info_->ip->ip_len);
    if (data_len > ICMP_PAYLOAD_LEN)
        data_len = ICMP_PAYLOAD_LEN;
    memcpy(data, pkt_info_->ip, data_len);

    uint32_t src_ip = 0;
    FlowKey key;
    if (pkt_info_->agent_hdr.flow_index == (uint32_t)-1) {
        // flow index is -1 for 255.255.255.255
        if (pkt_info_->ip_daddr.to_v4().to_ulong() != 0xFFFFFFFF) {
            proto_->increment_invalid_flow_index();
            return true;
        }
        src_ip = pkt_info_->ip_saddr.to_v4().to_ulong();
    } else {
        if (proto_->FlowIndexToKey(pkt_info_->agent_hdr.flow_index, &key)
            == false) {
            proto_->increment_invalid_flow_index();
            return true;
        }
        src_ip = key.src_addr.to_v4().to_ulong();
    }

    // Get IPAM to find default gateway
    const VnIpam *ipam = intf->vn()->GetIpam(Ip4Address(src_ip));
    if (ipam == NULL) {
        proto_->increment_interface_errors();
        return true;
    }

    // Retain the agent-header before ethernet header
    uint16_t len = (char *)pkt_info_->eth - (char *)pkt_info_->pkt;
    uint16_t buf_len = pkt_info_->max_pkt_len;

    // Form ICMP Packet with following
    // EthHdr - IP Header - ICMP Header - User IP Packet(max 128 bytes)
    char *ptr = (char *)pkt_info_->pkt;
    len += EthHdr(ptr + len, buf_len - len,
                  agent()->vhost_interface()->mac(),
                  MacAddress(pkt_info_->eth->ether_shost),
                  ETHERTYPE_IP, intf->vlan_id());

    uint16_t ip_len = sizeof(struct ip) + sizeof(struct icmp) + data_len;
    len += IpHdr(ptr + len, buf_len - len, ip_len,
            ipam->default_gw.to_v4().to_ulong(),
            htonl(src_ip), IPPROTO_ICMP);

    char *icmp = ptr + len;
    len += IcmpHdr(ptr + len, buf_len - len, ICMP_DEST_UNREACH,
                   ICMP_FRAG_NEEDED, 0, pkt_info_->agent_hdr.mtu);

    if (pkt_info_->agent_hdr.flow_index != (uint32_t)-1) {
        // Its possible that user payload has gone thru NAT processing already.
        // Restore the original fields from flow_key
        struct ip *ip = (struct ip *)data;
        uint16_t ip_hlen = ip->ip_hl * 4;

        ip->ip_src.s_addr = htonl(key.src_addr.to_v4().to_ulong());
        ip->ip_dst.s_addr = htonl(key.dst_addr.to_v4().to_ulong());
        ip->ip_sum = Csum((uint16_t *)data, ip_hlen, 0);
        if (ip->ip_p == IPPROTO_UDP) {
            udphdr *udp = (udphdr *)(data + ip_hlen);
            udp->source = ntohs(key.src_port);
            udp->dest = ntohs(key.dst_port);
        } else if (ip->ip_p == IPPROTO_TCP) {
            tcphdr *tcp = (tcphdr *)(data + ip_hlen);
            tcp->source = ntohs(key.src_port);
            tcp->dest = ntohs(key.dst_port);
        }
    }
    memcpy(ptr + len, data, data_len);
    len += data_len;
    IcmpChecksum(icmp, sizeof(struct icmp) + data_len);
    pkt_info_->set_len(len);

    Send(GetInterfaceIndex(), pkt_info_->vrf, AgentHdr::TX_SWITCH,
         PktHandler::ICMP);
    return true;
}
