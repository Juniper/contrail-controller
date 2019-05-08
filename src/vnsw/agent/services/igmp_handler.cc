/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
#include <vr_defs.h>
#include <base/logging.h>
#include <cmn/agent_cmn.h>
#include <pkt/pkt_init.h>
#include <oper/vn.h>
#include <pkt/control_interface.h>
#include <oper/interface_common.h>
#include <services/igmp_proto.h>

IgmpHandler::IgmpHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                         boost::asio::io_service &io)
    : ProtoHandler(agent, info, io), igmp_(pkt_info_->transp.igmp) {
    if (pkt_info_->ip) {
        igmp_len_ = ntohs(pkt_info_->ip->ip_len) - (pkt_info_->ip->ip_hl * 4);
    } else {
        igmp_len_ = 0;
    }
}

IgmpHandler::~IgmpHandler() {
}

bool IgmpHandler::Run() {

    HandleVmIgmpPacket();

    return true;
}

// Received IGMP packet handler for processing before sending to core IGMP
// message decoder and handler.
// Processes the IP header of the received IGMP packet
bool IgmpHandler::HandleVmIgmpPacket() {

    IgmpProto *igmp_proto = agent()->GetIgmpProto();

    int iphlen;
    u_int32_t igmplen;

    const Interface *itf =
        agent()->interface_table()->FindInterface(GetInterfaceIndex());
    if (!itf || !itf->IsActive()) {
        igmp_proto->IncrStatsBadInterface();
        return true;
    }

    const VmInterface *vm_itf = static_cast<const VmInterface *>(itf);
    if (!vm_itf || vm_itf->vmi_type() == VmInterface::VHOST) {
        igmp_proto->IncrStatsBadInterface();
        return true;
    }

    if (!vm_itf->igmp_enabled()) {
        igmp_proto->IncrStatsRejectedPkt();
        return true;
    }

    if (pkt_info_->len <
                (sizeof(struct ether_header) + ntohs(pkt_info_->ip->ip_len))) {
        return true;
    }

    iphlen = (pkt_info_->ip->ip_hl << 2);
    if (ntohs(pkt_info_->ip->ip_len) < iphlen) {
        igmp_proto->IncrStatsBadLength();
        return true;
    }

    igmplen = ntohs(pkt_info_->ip->ip_len) - iphlen;
    if (igmplen < IGMP_MIN_PACKET_LENGTH) {
        igmp_proto->IncrStatsBadLength();
        return true;
    }

    if (!CheckPacket()) {
        igmp_proto->IncrStatsBadCksum();
        return true;
    }

    const VnEntry *vn = vm_itf->vn();
    IgmpInfo::VnIgmpDBState *state = NULL;
    state = static_cast<IgmpInfo::VnIgmpDBState *>(vn->GetState(
                                vn->get_table_partition()->parent(),
                                igmp_proto->vn_listener_id()));
    if (!state) {
        igmp_proto->IncrStatsBadInterface();
        return true;
    }

    const VnIpam *ipam = vn->GetIpam(pkt_info_->ip_saddr);
    if (!ipam) {
        igmp_proto->IncrStatsBadInterface();
        return true;
    }

    IgmpInfo::VnIgmpDBState::IgmpSubnetStateMap::const_iterator it =
                            state->igmp_state_map_.find(ipam->default_gw);
    if (it == state->igmp_state_map_.end()) {
        igmp_proto->IncrStatsBadInterface();
        return true;
    }

    IgmpInfo::IgmpSubnetState *igmp_intf = NULL;
    igmp_intf = it->second;

    if (pkt_info_->transp.igmp->igmp_type > IGMP_MAX_TYPE) {
        if (igmp_intf) {
            igmp_intf->IncrRxUnknown();
        }
        igmp_proto->IncrStatsRxUnknown();
        return true;
    }

    bool parse_ok = false;

    if (!igmp_intf) {
        /* No MIF.  If we didn't process the packet, whine. */
        igmp_proto->IncrStatsBadInterface();
        return true;
    }

    parse_ok = igmp_proto->GetGmpProto()->GmpProcessPkt(
                        vm_itf, (void *)pkt_info_->transp.igmp, igmplen,
                        pkt_info_->ip_saddr, pkt_info_->ip_daddr);
    /* Complain if it didn't parse. */
    if (!parse_ok) {
        igmp_intf->IncrRxBadPkt(pkt_info_->transp.igmp->igmp_type);
    } else {
        igmp_intf->IncrRxOkPkt(pkt_info_->transp.igmp->igmp_type);
    }

    return true;
}

// Verify the checksum of the IGMP data present in the received packet
bool IgmpHandler::CheckPacket() const {

    uint16_t checksum = igmp_->igmp_cksum;
    igmp_->igmp_cksum = 0;
    if (checksum == Csum((uint16_t *)igmp_, igmp_len_, 0))
        return true;

    return false;
}

// Send packet to the VMs.
void IgmpHandler::SendPacket(const VmInterface *vm_itf, const VrfEntry *vrf,
                                    const IpAddress& gmp_addr,
                                    GmpPacket *packet) {

    if (pkt_info_->packet_buffer() == NULL) {
        pkt_info_->AllocPacketBuffer(agent(), PktHandler::IGMP, 1024, 0);
    }

    char *buf = (char *)pkt_info_->packet_buffer()->data();
    uint16_t buf_len = pkt_info_->packet_buffer()->data_len();
    memset(buf, 0, buf_len);

    MacAddress smac = vm_itf->GetVifMac(agent());
    MacAddress dmac = vm_itf->vm_mac();

    pkt_info_->vrf = vrf->vrf_id();
    pkt_info_->eth = (struct ether_header *)buf;
    uint16_t eth_len = 0;
    eth_len += EthHdr(buf, buf_len, vm_itf->id(), smac, dmac, ETHERTYPE_IP);

    uint16_t data_len = packet->pkt_len_;

    in_addr_t src_ip = htonl(gmp_addr.to_v4().to_ulong());
    in_addr_t dst_ip = htonl(packet->dst_addr_.to_v4().to_ulong());

    pkt_info_->ip = (struct ip *)(buf + eth_len);
    pkt_info_->transp.igmp = (struct igmp *)
                    ((uint8_t *)pkt_info_->ip + sizeof(struct ip));

    data_len += sizeof(struct ip);
    IpHdr(data_len, src_ip, dst_ip, IPPROTO_IGMP, DEFAULT_IP_ID, 1);

    memcpy(((char *)pkt_info_->transp.igmp), packet->pkt_, packet->pkt_len_);
    pkt_info_->set_len(data_len + eth_len);

    Send(vm_itf->id(), pkt_info_->vrf, AgentHdr::TX_SWITCH, PktHandler::IGMP);

    IgmpProto *igmp_proto = agent()->GetIgmpProto();
    igmp_proto->IncrSendStats(vm_itf, true);

    return;
}
