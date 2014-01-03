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
#include <pkt/flow_table.h>
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
        FlowTableKSyncObject::GetKSyncObject()->major_devid_ = r->get_fr_ftable_dev();
        FlowTableKSyncObject::GetKSyncObject()->flow_table_size_ = r->get_fr_ftable_size();
        LOG(DEBUG, "Flow table size : " << r->get_fr_ftable_size());
    } else if (r->get_fr_op() == flow_op::FLOW_SET) {
        FlowKey key;
        key.vrf = r->get_fr_flow_vrf();
        key.src.ipv4 = ntohl(r->get_fr_flow_sip());
        key.dst.ipv4 = ntohl(r->get_fr_flow_dip());
        key.src_port = ntohs(r->get_fr_flow_sport());
        key.dst_port = ntohs(r->get_fr_flow_dport());
        key.protocol = r->get_fr_flow_proto();
        FlowEntry *entry = Agent::GetInstance()->pkt()->flow_table()->Find(key);
        in_addr src;
        in_addr dst;
        src.s_addr = r->get_fr_flow_sip();
        dst.s_addr = r->get_fr_flow_dip();
        string src_str = inet_ntoa(src);
        string dst_str = inet_ntoa(dst);

        if (GetErrno() == EBADF) {
            string op;
            if (r->get_fr_flags() != 0) {
                op = "Add/Update";
            } else {
                op = "Delete";
            }

            LOG(ERROR, "Error Flow entry op = " << op
                       << " vrf = " << (int) key.vrf
                       << " src = " << src_str << ":" << key.src_port
                       << " dst = " << dst_str << ":" << key.dst_port
                       << " proto = " << (int)key.protocol);
            if (entry && (int)entry->flow_handle == r->get_fr_index()) {
                entry->flow_handle = FlowEntry::kInvalidFlowHandle;
            }
            return;
        }

        if (entry) {
            if (entry->flow_handle != FlowEntry::kInvalidFlowHandle) {
                if ((int)entry->flow_handle != r->get_fr_index()) {
                    LOG(DEBUG, "Flow index changed from <" << entry->flow_handle
                        << "> to <" << r->get_fr_rindex() << ">");
                }
            }
            entry->flow_handle = r->get_fr_index();
            //Tie forward flow and reverse flow
            if (entry->nat || entry->data.ecmp) {
                 FlowEntry *rev_flow = entry->data.reverse_flow.get();
                 if (rev_flow) {
                     FlowTableKSyncEntry *rev_ksync_entry =
                         FlowTableKSyncObject::GetKSyncObject()->Find(rev_flow);
                     rev_flow->UpdateKSync(rev_ksync_entry, false);
                 }
            }
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
