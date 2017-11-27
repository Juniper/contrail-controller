/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
#include <vr_defs.h>
#include <cmn/agent_cmn.h>
#include <pkt/pkt_init.h>
#include <pkt/control_interface.h>
#include <oper/interface_common.h>
#include <services/igmp_proto.h>

#include <base/logging.h>

IgmpHandler::IgmpHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                         boost::asio::io_service &io)
    : ProtoHandler(agent, info, io), igmp_(pkt_info_->transp.igmp) {
    igmp_len_ = ntohs(pkt_info_->ip->ip_len) - (pkt_info_->ip->ip_hl * 4);
}

IgmpHandler::~IgmpHandler() {
}

bool IgmpHandler::Run() {

    Interface *itf =
        agent()->interface_table()->FindInterface(GetInterfaceIndex());
    if (itf == NULL) {
        return true;
    }
    if (!CheckPacket()) {
        /* TODO - increment statistics */
        /* Call IGMP - GMP API */
        return true;
    }

    /* TODO : Translate incoming info and send to GMP */
    /* TODO : Logging info for now */
    LOG(DEBUG, "Received IGMP Packet from pkt0.\n");

    return true;
}

bool IgmpHandler::CheckPacket() {
    if (pkt_info_->len < (sizeof(struct ether_header) + ntohs(pkt_info_->ip->ip_len)))
        return false;

    uint16_t checksum = igmp_->igmp_cksum;
    igmp_->igmp_cksum = 0;
    if (checksum == Csum((uint16_t *)igmp_, igmp_len_, 0))
        return true;

    return false;
}

void IgmpHandler::SendMessage(VmInterface *vm_intf) {

    uint32_t interface =
        ((pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_TOR_CONTROL_PKT) ?
         pkt_info_->agent_hdr.cmd_param : GetInterfaceIndex());
    uint16_t command =
        ((pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_TOR_CONTROL_PKT) ?
         AgentHdr::TX_ROUTE : AgentHdr::TX_SWITCH);
    Send(interface, pkt_info_->vrf, command, PktHandler::IGMP);
}
