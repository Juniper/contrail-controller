/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
#include <vr_defs.h>
#include <base/logging.h>
#include <cmn/agent_cmn.h>
#include <pkt/pkt_init.h>
#include <pkt/control_interface.h>
#include <oper/interface_common.h>
#include <services/igmp_proto.h>

IgmpHandler::IgmpHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                         boost::asio::io_service &io)
    : ProtoHandler(agent, info, io), igmp_(pkt_info_->transp.igmp) {
    igmp_len_ = ntohs(pkt_info_->ip->ip_len) - (pkt_info_->ip->ip_hl * 4);
}

IgmpHandler::~IgmpHandler() {
}

bool IgmpHandler::Run() {

    HandleVmIgmpPacket();

    return true;
}

bool IgmpHandler::HandleVmIgmpPacket() {

    IgmpProto *igmp_proto = agent()->GetIgmpProto();

    int iphlen;
    u_int32_t igmplen;

    Interface *itf =
        agent()->interface_table()->FindInterface(GetInterfaceIndex());
    if (itf == NULL) {
        igmp_proto->IncrStatsBadInterface();
        return true;
    }

    VmInterface *vm_itf = static_cast<VmInterface *>(itf);

    IgmpInfo::McastInterfaceState *mif_state = NULL;

    mif_state = static_cast<IgmpInfo::McastInterfaceState *>(vm_itf->GetState(
                                vm_itf->get_table_partition()->parent(),
                                igmp_proto->ItfListenerId()));

    iphlen = (pkt_info_->ip->ip_hl << 2);
    igmplen = ntohs(pkt_info_->ip->ip_len) - iphlen;

    if (igmplen < IGMP_MIN_PACKET_LENGTH) {
        igmp_proto->IncrStatsBadLength();
        return true;
    }

    if (!CheckPacket()) {
        igmp_proto->IncrStatsBadCksum();
        return true;
    }

    if (pkt_info_->transp.igmp->igmp_type > IGMP_MAX_TYPE) {
        if (mif_state) {
            mif_state->IncrRxUnknown();
        } else {
            igmp_proto->IncrStatsRxUnknown();
        }
        return true;
    }

    bool parse_ok = false;

    if (mif_state) {
        uint32_t src_addr = pkt_info_->ip_saddr.to_v4().to_ulong();
        uint32_t dst_addr = pkt_info_->ip_daddr.to_v4().to_ulong();

        parse_ok = igmp_proto->GetGmpSm()->GmpProcessPkt(mif_state->gmp_intf_,
                        (void *)pkt_info_->transp.igmp, igmplen,
                        (const u_int8_t *)&src_addr,
                        (const u_int8_t *)&dst_addr);
    } else {
        /* No MIF.  If we didn't process the packet, whine. */
        igmp_proto->IncrStatsBadInterface();
        return true;
    }

    /* Complain if it didn't parse. */
    if (!parse_ok) {
        if (mif_state) {
            mif_state->IncrRxBadPkt(pkt_info_->transp.igmp->igmp_type);
        }
    }

    if (mif_state) {
        mif_state->IncrRxOkPkt(pkt_info_->transp.igmp->igmp_type);
    }

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
