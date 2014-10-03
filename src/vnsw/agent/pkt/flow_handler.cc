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
