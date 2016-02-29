/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <net/address.h>
#include <net/address_util.h>
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

// Compute L2/L3 forwarding mode for pacekt.
// Forwarding mode is L3 if,
// - Packet uses L3 label
// - Packet uses L2 Label and DMAC hits a route with L2-Receive NH
// Else forwarding mode is L2
bool FlowHandler::IsL3ModeFlow() const {
    if (pkt_info_->l3_label) {
        return true;
    }

    VrfTable *table = static_cast<VrfTable *>(agent_->vrf_table());
    VrfEntry *vrf = table->FindVrfFromId(pkt_info_->agent_hdr.vrf);
    if (vrf == NULL) {
        return false;
    }
    BridgeAgentRouteTable *l2_table = static_cast<BridgeAgentRouteTable *>
        (vrf->GetBridgeRouteTable());
    AgentRoute *rt = static_cast<AgentRoute *>
        (l2_table->FindRouteNoLock(pkt_info_->dmac));
    if (rt == NULL) {
        return false;
    }

    const NextHop *nh = rt->GetActiveNextHop();
    if (nh == NULL) {
        return false;
    }

    if (nh->GetType() == NextHop::L2_RECEIVE) {
        return true;
    }
    return false;
}

bool FlowHandler::Run() {
    PktControlInfo in;
    PktControlInfo out;
    PktFlowInfo info(agent_, pkt_info_,
                     flow_proto_->GetTable(flow_table_index_));
    std::auto_ptr<FlowTaskMsg> ipc;

    if (pkt_info_->type == PktType::INVALID) {
        // packet parsing is not done, invoke the same here
        uint8_t *pkt = pkt_info_->packet_buffer()->data();
        PktHandler::PktModuleName mod = agent_->pkt()->pkt_handler()->
                                        ParseFlowPacket(pkt_info_, pkt);
        // if packet wasnt for flow module, it would've got enqueued to the
        // correct module in the above call. Nothing else to do.
        if (mod != PktHandler::FLOW) {
            return true;
        }
        flow_proto_->FreeBuffer(pkt_info_.get());
        info.SetPktInfo(pkt_info_);
        info.l3_flow = pkt_info_->l3_forwarding = IsL3ModeFlow();
    } else if (pkt_info_->type == PktType::MESSAGE) {
        ipc = std::auto_ptr<FlowTaskMsg>(static_cast<FlowTaskMsg *>(pkt_info_->ipc));
        pkt_info_->ipc = NULL;
        FlowEntry *fe = ipc->fe_ptr.get();
        // take lock on flow entry before accessing it, since we need to read
        // forward flow only take lock only on forward flow
        tbb::mutex::scoped_lock lock1(fe->mutex());
        assert(flow_table_index_ == fe->flow_table()->table_index());
        if (fe->deleted() || fe->is_flags_set(FlowEntry::ShortFlow)) {
            return true;
        }

        if (fe->is_flags_set(FlowEntry::ShortFlow)) {
            return true;
        }

        // We dont support revaluation of linklocal flows
        if (fe->is_flags_set(FlowEntry::LinkLocalFlow)) {
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
        pkt_info_->smac = fe->data().smac;
        pkt_info_->dmac = fe->data().dmac;
        pkt_info_->ip_saddr = fe->key().src_addr;
        pkt_info_->ip_daddr = fe->key().dst_addr;
        pkt_info_->ip_proto = fe->key().protocol;
        pkt_info_->sport = fe->key().src_port;
        pkt_info_->dport = fe->key().dst_port;
        pkt_info_->tcp_ack = fe->is_flags_set(FlowEntry::TcpAckFlow);
        pkt_info_->vrf = fe->data().vrf;
        pkt_info_->l3_forwarding = fe->l3_flow();
        info.l3_flow = fe->l3_flow();
        info.out_component_nh_idx = fe->data().component_nh_idx;
    } 

    if (info.Process(pkt_info_.get(), &in, &out) == false) {
        info.short_flow = true;
    }

    //Identify port nat and enqueue port nat to specific flow table
    if (((pkt_info_->sport != info.nat_sport) ||
        (pkt_info_->dport != info.nat_dport)) &&
        (info.nat_sport != 0) && (info.nat_dport != 0) &&
        (flow_table_index_ != FlowTable::kPortNatFlowTableInstance)) {
        //Enqueue flow evaluation to
        //FlowTable::kPortNatFlowTableInstance instance.
        flow_proto_->EnqueueReentrant(pkt_info_,
                                      FlowTable::kPortNatFlowTableInstance);
        return true;
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
