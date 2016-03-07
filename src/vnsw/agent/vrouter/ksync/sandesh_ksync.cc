/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <vrouter/ksync/sandesh_ksync.h>
#include <vrouter/ksync/flowtable_ksync.h>
#include <vrouter/ksync/interface_ksync.h>
#include <pkt/flow_table.h>
#include <oper/mirror_table.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/flow_stats/flow_stats_collector.h>

void vr_interface_req::Process(SandeshContext *context) {
     AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
     ioc->IfMsgHandler(this);
}

void vr_drop_stats_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->DropStatsMsgHandler(this);
}

void vr_vrf_stats_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->VrfStatsMsgHandler(this);
}

void vrouter_ops::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->VrouterOpsMsgHandler(this);
}

int KSyncSandeshContext::VrResponseMsgHandler(vr_response *r) {
    response_code_ = r->get_resp_code();

    if (-response_code_ == EEXIST) {
        return 0;
    }

    if (response_code_ < 0) {
        LOG(ERROR, "VrResponseMsg Error: " <<
            KSyncEntry::VrouterErrorToString(-response_code_));
        return -response_code_;
    }

    return 0;
}

void KSyncSandeshContext::FlowMsgHandler(vr_flow_req *r) {
    assert(r->get_fr_op() == flow_op::FLOW_TABLE_GET || 
           r->get_fr_op() == flow_op::FLOW_SET);

    if (r->get_fr_op() == flow_op::FLOW_TABLE_GET) {
        flow_ksync_->major_devid_ = r->get_fr_ftable_dev();
        flow_ksync_->flow_table_size_ = r->get_fr_ftable_size();
        flow_ksync_->flow_table_path_ = r->get_fr_file_path();
        LOG(DEBUG, "Flow table size : " << r->get_fr_ftable_size());
    } else if (r->get_fr_op() == flow_op::FLOW_SET) {
        const KSyncIoContext *ioc = ksync_io_ctx();
        // TODO : IPv6
        FlowKey key(r->get_fr_flow_nh_id(),
                    Ip4Address(ntohl(r->get_fr_flow_sip())),
                    Ip4Address(ntohl(r->get_fr_flow_dip())),
                    r->get_fr_flow_proto(),
                    ntohs(r->get_fr_flow_sport()),
                    ntohs(r->get_fr_flow_dport()));
        FlowEntry *entry = flow_ksync_->ksync()->agent()->pkt()->flow_table()->
                           Find(key);
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
                       << " nh = " << (int) key.nh
                       << " src = " << src_str << ":" << key.src_port
                       << " dst = " << dst_str << ":" << key.dst_port
                       << " proto = " << (int)key.protocol
                       << " flow_handle = " << (int) r->get_fr_index());
            if (entry && (int)entry->flow_handle() == r->get_fr_index()) {
                entry->MakeShortFlow(FlowEntry::SHORT_FAILED_VROUTER_INSTALL);
            }
            return;
        }

        if (GetErrno() == ENOSPC) {
            if (entry) {
                entry->MakeShortFlow(FlowEntry::SHORT_FAILED_VROUTER_INSTALL);
            }
            return;
        }

        if (entry) {
            if (ioc->event() == KSyncEntry::DEL_ACK) {
                // Skip delete operation.
                return;
            }

            if (entry->flow_handle() != FlowEntry::kInvalidFlowHandle) {
                if ((int)entry->flow_handle() != r->get_fr_index()) {
                    LOG(DEBUG, "Flow index changed from <" << 
                        entry->flow_handle() << "> to <" << 
                        r->get_fr_index() << ">");
                }
            }

            bool update_rev_flow = false;
            if ((int)entry->flow_handle() != r->get_fr_index()) {
                update_rev_flow = true;
            }

            FlowTable *flow_table =
                flow_ksync_->ksync()->agent()->pkt()->flow_table();

            FlowStatsManager *sm = flow_table->agent()->flow_stats_manager();
            FlowEntry *flow = flow_table->FindByIndex(r->get_fr_index());
            if (flow && flow->deleted() == false) {
                sm->UpdateStatsEvent(flow, r->get_fr_flow_bytes(),
                        r->get_fr_flow_packets(), r->get_fr_flow_stats_oflow(),
                        flow_table);
            }

            entry->set_flow_handle(r->get_fr_index(), flow_table);
            FlowEntry *rev_flow = entry->reverse_flow_entry();
            //Tie forward flow and reverse flow
            if (rev_flow && update_rev_flow) {
                rev_flow->UpdateKSync(
                        flow_ksync_->ksync()->agent()->pkt()->flow_table(),
                        true);
            }
        }
    } else {
        assert(!("Invalid Flow operation"));
    }
    return;
}

void KSyncSandeshContext::IfMsgHandler(vr_interface_req *r) {
    flow_ksync_->ksync()->interface_scanner()->KernelInterfaceData(r); 
    context_marker_ = r->get_vifr_idx();
}

void KSyncSandeshContext::VrouterOpsMsgHandler(vrouter_ops *r) {
    Agent *agent = flow_ksync_->ksync()->agent();
    agent->set_vrouter_max_labels(r->get_vo_mpls_labels());
    agent->set_vrouter_max_nexthops(r->get_vo_nexthops());
    agent->set_vrouter_max_bridge_entries(r->get_vo_bridge_entries());
    agent->set_vrouter_max_oflow_bridge_entries(
            r->get_vo_oflow_bridge_entries());
    agent->set_vrouter_max_interfaces(r->get_vo_interfaces());
    agent->set_vrouter_max_mirror_entries(r->get_vo_mirror_entries());
    agent->set_vrouter_max_vrfs(r->get_vo_vrfs());
    agent->set_vrouter_build_info(r->get_vo_build_info());
    return;
}
