/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/sandesh_ksync.h>
#include <ksync/flowtable_ksync.h>
#include <ksync/interface_ksync.h>
#include <pkt/flowtable.h>
#include <oper/mirror_table.h>

void vr_drop_stats_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->DropStatsMsgHandler(this);
}

void vr_vrf_stats_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->VrfStatsMsgHandler(this);
}

int KSyncSandeshContext::VrResponseMsgHandler(vr_response *r) {
    resp_code_ = r->get_resp_code();

    if (-resp_code_ == EEXIST) {
        return 0;
    }

    if (resp_code_ < 0) {
        LOG(ERROR, "VrResponseMsg Error: " << strerror(-resp_code_));
        return -resp_code_;
    }

    return 0;
}

void KSyncSandeshContext::FlowMsgHandler(vr_flow_req *r) {
    assert(r->get_fr_op() == flow_op::FLOW_TABLE_GET || 
           r->get_fr_op() == flow_op::FLOW_SET);

    if (r->get_fr_op() == flow_op::FLOW_TABLE_GET) {
        FlowTableKSyncObject::flow_info_ = *r;
        LOG(DEBUG, "Flow table size : " << r->get_fr_ftable_size());
    } else if (r->get_fr_op() == flow_op::FLOW_SET) {
        FlowKey key;
        key.vrf = r->get_fr_rflow_vrf();
        key.src.ipv4 = ntohl(r->get_fr_rflow_sip());
        key.dst.ipv4 = ntohl(r->get_fr_rflow_dip());
        key.src_port = ntohs(r->get_fr_rflow_sport());
        key.dst_port = ntohs(r->get_fr_rflow_dport());
        key.protocol = r->get_fr_rflow_proto();
        FlowEntry *entry = FlowTable::GetFlowTableObject()->Find(key);

        if (entry) {
            if (entry->flow_handle != FlowEntry::kInvalidFlowHandle) {
                if ((int)entry->flow_handle != r->get_fr_rindex()) {
                    LOG(DEBUG, "Flow index changed from <" << entry->flow_handle
                        << "> to <" << r->get_fr_rindex() << ">");
                }
            }
            entry->flow_handle = r->get_fr_rindex();
        }
    } else {
        assert(!("Invalid Flow operation"));
    }
    return;
}

void KSyncSandeshContext::IfMsgHandler(vr_interface_req *r) {
    InterfaceKSnap::GetInstance()->KernelInterfaceData(r);
    context_marker_ = r->get_vifr_idx();
}
