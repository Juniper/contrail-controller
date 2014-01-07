/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "pkt/proto.h"
#include "pkt/proto_handler.h"
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
    PktFlowInfo info(pkt_info_);

    MatchPolicy m_policy;

    SecurityGroupList empty_sg_id_l;
    info.source_sg_id_l = &empty_sg_id_l;
    info.dest_sg_id_l = &empty_sg_id_l;

    if (info.Process(pkt_info_.get(), &in, &out) == false) {
        info.short_flow = true;
    }

    if (in.rt_) {
        const AgentPath *path = in.rt_->GetActivePath();
        info.source_sg_id_l = &(path->GetSecurityGroupList());
        info.source_plen = in.rt_->GetPlen();
    }

    if (out.rt_) {
        const AgentPath *path = out.rt_->GetActivePath();
        info.dest_sg_id_l = &(path->GetSecurityGroupList());
        info.dest_plen = out.rt_->GetPlen();
    }

    if (info.source_vn == NULL)
        info.source_vn = FlowHandler::UnknownVn();

    if (info.dest_vn == NULL)
        info.dest_vn = FlowHandler::UnknownVn();

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
