/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <net/address.h>
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "pkt/pkt_init.h"
#include "pkt/pkt_handler.h"
#include "pkt/flow_table.h"
#include "pkt/flow_proto.h"
#include "pkt/flow_handler.h"

SandeshTraceBufferPtr PktFlowTraceBuf(SandeshTraceBufferCreate("FlowHandler", 5000));

static const VmEntry *InterfaceToVm(const Interface *intf) {
    if (intf->type() != Interface::VM_INTERFACE)
        return NULL;

    const VmInterface *vm_port = static_cast<const VmInterface *>(intf);
    return vm_port->vm();
}

bool FlowHandler::Run() {
    PktControlInfo in;
    PktControlInfo out;
    PktFlowInfo info(pkt_info_, agent_->pkt()->flow_table());
    std::auto_ptr<FlowTaskMsg> ipc;

    if (pkt_info_->type == PktType::MESSAGE) {
        ipc = std::auto_ptr<FlowTaskMsg>(static_cast<FlowTaskMsg *>(pkt_info_->ipc));
        pkt_info_->ipc = NULL;
        FlowEntry *fe = ipc->fe_ptr.get();
        assert(fe->set_pending_recompute(false));
        if (fe->deleted() || fe->is_flags_set(FlowEntry::ShortFlow)) {
            return true;
        }
        info.flow_entry = fe;
        pkt_info_->agent_hdr.cmd = AGENT_TRAP_FLOW_MISS;
        pkt_info_->agent_hdr.cmd_param = fe->flow_handle();
        pkt_info_->agent_hdr.ifindex = fe->data().if_index_info;
        pkt_info_->tunnel = fe->data().tunnel_info;
        pkt_info_->agent_hdr.nh = fe->key().nh;
        pkt_info_->agent_hdr.vrf = fe->data().vrf;
        pkt_info_->family =
            fe->key().src_addr.is_v4() ? Address::INET : Address::INET6;
        pkt_info_->ip_saddr = fe->key().src_addr;
        pkt_info_->ip_daddr = fe->key().dst_addr;
        pkt_info_->ip_proto = fe->key().protocol;
        pkt_info_->sport = fe->key().src_port;
        pkt_info_->dport = fe->key().dst_port;
        pkt_info_->tcp_ack = fe->is_flags_set(FlowEntry::TcpAckFlow);
    }

    if (info.Process(pkt_info_.get(), &in, &out) == false) {
        info.short_flow = true;
    }

    if (in.intf_ && ((in.intf_->type() != Interface::VM_INTERFACE) &&
                     (in.intf_->type() != Interface::INET))) {
        in.intf_ = NULL;
    }

    if (in.intf_ && out.intf_) {
        info.local_flow = true;
    }

    if (in.intf_) {
        in.vm_ = InterfaceToVm(in.intf_);
    }

    if (out.intf_) {
        out.vm_ = InterfaceToVm(out.intf_);
    }

    info.Add(pkt_info_.get(), &in, &out);
    return true;
}
