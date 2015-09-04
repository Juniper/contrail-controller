/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <net/address_util.h>
#include <vrouter/ksync/sandesh_ksync.h>
#include <vrouter/ksync/flowtable_ksync.h>
#include <vrouter/ksync/interface_ksync.h>
#include <pkt/flow_table.h>
#include <oper/mirror_table.h>
#include <vrouter/ksync/ksync_init.h>

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

void vr_mem_stats_req::Process(SandeshContext *context) {
}

int KSyncSandeshContext::VrResponseMsgHandler(vr_response *r) {
    response_code_ = r->get_resp_code();

    if (-response_code_ == EEXIST) {
        return 0;
    }

    if (response_code_ < 0) {
        LOG(ERROR, "VrResponseMsg Error: " << strerror(-response_code_));
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
        int family = (r->get_fr_family() == AF_INET)? Address::INET :
            Address::INET6;
        IpAddress sip, dip;
        VectorToIp(r->get_fr_flow_ip(), family, &sip, &dip);
        FlowKey key(r->get_fr_flow_nh_id(), sip, dip,
                    r->get_fr_flow_proto(),
                    ntohs(r->get_fr_flow_sport()),
                    ntohs(r->get_fr_flow_dport()));
        FlowEntry *entry = flow_ksync_->ksync()->agent()->pkt()->flow_table()->
                           Find(key);

        if (GetErrno() == EBADF) {
            string op;
            if (r->get_fr_flags() != 0) {
                op = "Add/Update";
            } else {
                op = "Delete";
            }

            LOG(ERROR, "Error Flow entry op = " << op
                       << " nh = " << (int) key.nh
                       << " src = " << sip.to_string() << ":" << key.src_port
                       << " dst = " << dip.to_string() << ":" << key.dst_port
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
            flow_ksync_->ksync()->agent()->pkt()->flow_table()->
                AddIndexFlowInfo(entry, r->get_fr_index());

            FlowEntry *rev_flow = entry->reverse_flow_entry();
            //Tie forward flow and reverse flow
            if (rev_flow && update_rev_flow) {
                rev_flow->UpdateKSync(
                        flow_ksync_->ksync()->agent()->pkt()->flow_table());
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
