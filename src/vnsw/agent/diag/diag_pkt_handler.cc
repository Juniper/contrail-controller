/*
 *  * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 *   */

#include <map>
#include "vr_defs.h"
#include "base/timer.h"
#include "cmn/agent_cmn.h"
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "diag/diag.h"
#include "diag/diag_proto.h"
#include "diag/ping.h"
#include "oper/mirror_table.h"


void DiagPktHandler::SetReply() {
    AgentDiagPktData *ad = (AgentDiagPktData *)pkt_info_->data;
    ad->op_ = htonl(AgentDiagPktData::DIAG_REPLY);
}

void DiagPktHandler::SetDiagChkSum() {
#if defined(__linux__)
    pkt_info_->ip->check = 0xffff;
#elif defined(__FreeBSD__)
    pkt_info_->ip->ip_sum = 0xffff;
#else
#error "Unsupported platform"
#endif
}

void DiagPktHandler::Reply() {
    SetReply();
    Swap();
    SetDiagChkSum();
    Send(GetLen() - (2 * IPC_HDR_LEN), GetIntf(), GetVrf(),
         AGENT_CMD_ROUTE, PktHandler::DIAG);
}
bool DiagPktHandler::Run() {
    AgentDiagPktData *ad = (AgentDiagPktData *)pkt_info_->data;

    if (!ad) {
        //Ignore if packet doesnt have proper L4 header
               return true;
    }
    if (ntohl(ad->op_) == AgentDiagPktData::DIAG_REQUEST) {
        //Request received swap the packet
        //and dump the packet back
                Reply();
                return false;
    }

    if (ntohl(ad->op_) != AgentDiagPktData::DIAG_REPLY) {
        return true;
    }
    //Reply for a query we sent
    DiagEntry::DiagKey key = ntohl(ad->key_);
    DiagEntry *entry = diag_table_->Find(key);
    if (!entry) {
        return true;
    }

    entry->HandleReply(this);

    if (entry->GetSeqNo() == entry->GetCount()) {
        DiagEntryOp *op;
        op = new DiagEntryOp(DiagEntryOp::DELETE, entry);
        entry->diag_table()->Enqueue(op);
    } else {
        entry->Retry();
    }

    return true;
}
