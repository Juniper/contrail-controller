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
    if (pkt_info_->len < (IPC_HDR_LEN + sizeof(ethhdr) + sizeof(iphdr)))
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
    if (vm_itf == NULL || vm_itf->ipv4_forwarding() == false || vm_itf->vn() == NULL) { 
        proto_->increment_interface_errors();
        return true;
    }

    return SendIcmpError(vm_itf);
}

// Generate ICMP error
bool IcmpErrorHandler::SendIcmpError(VmInterface *intf) {
    char data[ICMP_PAYLOAD_LEN];
    uint16_t data_len = ntohs(pkt_info_->ip->tot_len);
    if (data_len > ICMP_PAYLOAD_LEN)
        data_len = ICMP_PAYLOAD_LEN;
    memcpy(data, pkt_info_->ip, data_len);

    FlowKey key;
    if (proto_->FlowIndexToKey(pkt_info_->agent_hdr.flow_index, &key)
        == false) {
        proto_->increment_invalid_flow_index();
        return true;
    }

    // Get IPAM to find default gateway
    const VnIpam *ipam = intf->vn()->GetIpam(Ip4Address(key.src.ipv4));
    if (ipam == NULL) {
        proto_->increment_interface_errors();
        return true;
    }

    uint16_t len = (char *)pkt_info_->eth - (char *)pkt_info_->pkt;
    uint16_t buf_len = pkt_info_->max_pkt_len;

    // Form ICMP Packet with following
    // EthHdr - IP Header - ICMP Header - User IP Packet(max 128 bytes)
    char *ptr = (char *)pkt_info_->pkt;
    len += EthHdr(ptr + len, buf_len - len,
                  agent()->vhost_interface()->mac().ether_addr_octet,
                  pkt_info_->eth->h_source, IP_PROTOCOL, intf->vlan_id());

    uint16_t ip_len = sizeof(iphdr) + sizeof(icmphdr) + data_len;
    len += IpHdr(ptr + len, buf_len - len, ip_len, ipam->default_gw.to_ulong(),
            htonl(key.src.ipv4), IPPROTO_ICMP);

    char *icmp = ptr + len;
    len += IcmpHdr(ptr + len, buf_len - len, ICMP_DEST_UNREACH,
                   ICMP_FRAG_NEEDED, 0, pkt_info_->agent_hdr.mtu);

    // Its possible that user payload has gone thru NAT processing already.
    // Restore the original fields from flow_key
    iphdr *ip = (iphdr *)data;
    uint16_t ip_hlen = ip->ihl * 4;

    ip->saddr = htonl(key.src.ipv4);
    ip->daddr = htonl(key.dst.ipv4);
    ip->check = Csum((uint16_t *)data, ip_hlen, 0);
    if (ip->protocol == IPPROTO_UDP) {
        udphdr *udp = (udphdr *)(data + ip_hlen);
        udp->source = ntohs(key.src_port);
        udp->dest = ntohs(key.dst_port);
    } else if (ip->protocol == IPPROTO_TCP) {
        tcphdr *tcp = (tcphdr *)(data + ip_hlen);
        tcp->source = ntohs(key.src_port);
        tcp->dest = ntohs(key.dst_port);
    }
    memcpy(ptr + len, data, data_len);
    len += data_len;
    IcmpChecksum(icmp, sizeof(icmphdr) + data_len);

    Send(len, GetInterfaceIndex(), pkt_info_->vrf, AGENT_CMD_SWITCH,
         PktHandler::ICMP);
    return true;
}
