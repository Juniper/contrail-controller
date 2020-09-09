/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <init/agent_init.h>
#include <pkt/pkt_init.h>
#include <pkt/pkt_handler.h>
#include <services/bfd_proto.h>
#include "mac_learning/mac_learning_proto.h"

BfdHandler::BfdHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                       boost::asio::io_service &io)
    : ProtoHandler(agent, info, io) {
}

BfdHandler::~BfdHandler() {
}

bool BfdHandler::Run() {
    Interface *itf =
        agent()->interface_table()->FindInterface(GetInterfaceIndex());
    if (itf == NULL) {
        return true;
    }

    BfdProto *bfd_proto = agent()->GetBfdProto();
    uint8_t len = ntohs(pkt_info_->transp.udp->len) - 8;
    uint8_t *data = new uint8_t[len];
    memcpy(data, pkt_info_->data, len);
    boost::asio::const_buffer buffer(boost::asio::buffer(data, len));

    boost::system::error_code ec;
    boost::asio::ip::udp::endpoint local_endpoint(pkt_info_->ip_daddr,
                                                  pkt_info_->dport);
    boost::asio::ip::udp::endpoint remote_endpoint(pkt_info_->ip_saddr,
                                                   pkt_info_->sport);
    bfd_proto->bfd_communicator().HandleReceive(
                                  buffer, local_endpoint, remote_endpoint,
                                  BFD::SessionIndex(GetInterfaceIndex()),
                                  len, ec);
    bfd_proto->IncrementReceived();

    return true;
}

void BfdHandler::SendPacket(
         const boost::asio::ip::udp::endpoint &local_endpoint,
         const boost::asio::ip::udp::endpoint &remote_endpoint,
         uint32_t interface_id, const boost::asio::mutable_buffer &packet,
         int packet_length) {

    Interface *intrface =
        agent()->interface_table()->FindInterface(interface_id);
    if (!intrface || intrface->type() != Interface::VM_INTERFACE)
        return;

    if (pkt_info_->packet_buffer() == NULL) {
        pkt_info_->AllocPacketBuffer(agent(), PktHandler::BFD,
                                     BFD_TX_BUFF_LEN, 0);
    }

    uint16_t buf_len = pkt_info_->packet_buffer()->data_len();
    char *ptr = (char *)pkt_info_->packet_buffer()->data();
    memset(ptr, 0, buf_len);
    pkt_info_->eth = (struct ether_header *)ptr;

    VmInterface *vm_interface = static_cast<VmInterface *>(intrface);
    bool is_v4 = local_endpoint.address().is_v4();
    uint16_t len = 0;

    uint8_t *data = boost::asio::buffer_cast<uint8_t *>(packet);
    uint16_t eth_proto = is_v4 ? ETHERTYPE_IP : ETHERTYPE_IPV6;
    MacAddress mac = agent()->mac_learning_proto()->
                        GetMacIpLearningTable()->GetPairedMacAddress(
                                vm_interface->vrf()->vrf_id(),
                                remote_endpoint.address());

    if (mac == MacAddress()) {
        mac = vm_interface->vm_mac();
    } 
    
    len += EthHdr(ptr + len, buf_len - len,
                  agent()->vrrp_mac(),
                  mac,
                  eth_proto, vm_interface->tx_vlan_id());

    if (is_v4) {
        uint16_t ip_len = sizeof(struct ip) + sizeof(udphdr) + packet_length;
        len += IpHdr(ptr + len, buf_len - len, ip_len,
                     htonl(local_endpoint.address().to_v4().to_ulong()),
                     htonl(remote_endpoint.address().to_v4().to_ulong()),
                     IPPROTO_UDP, 0, 255);
        memcpy(ptr + len + sizeof(udphdr), data, packet_length);
        len += UdpHdr((udphdr *)(ptr + len), buf_len - len,
                      sizeof(udphdr) + packet_length,
                      htonl(local_endpoint.address().to_v4().to_ulong()),
                      local_endpoint.port(),
                      htonl(remote_endpoint.address().to_v4().to_ulong()),
                      remote_endpoint.port());
    } else {
        Ip6Hdr((ip6_hdr *)(ptr + len),
               sizeof(struct ip6_hdr) + sizeof(udphdr) + packet_length,
               IPPROTO_UDP, 64,
               local_endpoint.address().to_v6().to_bytes().data(),
               remote_endpoint.address().to_v6().to_bytes().data());
        len += sizeof(ip6_hdr);
        memcpy(ptr + len + sizeof(udphdr), data, packet_length);
        UdpHdr((udphdr *)(ptr + len), sizeof(udphdr) + packet_length,
               local_endpoint.address().to_v6().to_bytes().data(),
               local_endpoint.port(),
               remote_endpoint.address().to_v6().to_bytes().data(),
               remote_endpoint.port(), IPPROTO_UDP);
        len += sizeof(udphdr);
    }

    len += packet_length;

    pkt_info_->set_len(len);
    Send(interface_id, vm_interface->vrf_id(),
         AgentHdr::TX_SWITCH, PktHandler::BFD);
    pkt_info_->reset_packet_buffer();
    const uint8_t *p = boost::asio::buffer_cast<const uint8_t *>(packet);
    delete[] p;
}
