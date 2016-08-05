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
#include <pkt/flow_mgmt.h>
#include <pkt/flow_table.h>
#include <oper/mirror_table.h>
#include <vrouter/ksync/ksync_init.h>
#include <pkt/flow_proto.h>
#include <pkt/flow_token.h>

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

    if (response_code_ < 0) {
        return -response_code_;
    }

    return 0;
}

static void LogFlowError(vr_flow_req *r, int err) {
    string op;
    if (r->get_fr_flags() != 0) {
        op = "Add/Update";
    } else {
        op = "Delete";
    }

    int family = (r->get_fr_family() == AF_INET)? Address::INET :
        Address::INET6;
    IpAddress sip, dip;
    VectorToIp(r->get_fr_flow_ip(), family, &sip, &dip);
    LOG(ERROR, "Error Flow entry op = " << op
        << " nh = " << (int) r->get_fr_flow_nh_id()
        << " src = " << sip.to_string() << ":"
        << ntohs(r->get_fr_flow_sport())
        << " dst = " << dip.to_string()
        << ntohs(r->get_fr_flow_dport())
        << " proto = " << (int)r->get_fr_flow_proto()
        << " flow_handle = " << (int) r->get_fr_index());
}

// Handle vr_flow response from VRouter
// We combine responses from both vr_flow and vr_response messages and
// generate single event. Copy the results in vr_flow in KSync entry.
// On receiving vr_response message, event will be generated for both messages
void KSyncSandeshContext::FlowMsgHandler(vr_flow_req *r) {
    assert(r->get_fr_op() == flow_op::FLOW_TABLE_GET || 
           r->get_fr_op() == flow_op::FLOW_SET);

    if (r->get_fr_op() == flow_op::FLOW_TABLE_GET) {
        flow_ksync_->set_major_devid(r->get_fr_ftable_dev());
        flow_ksync_->set_flow_table_size(r->get_fr_ftable_size());
        flow_ksync_->set_flow_table_path(r->get_fr_file_path());
        LOG(DEBUG, "Flow table size : " << r->get_fr_ftable_size());
        return;
    } 

    const KSyncIoContext *ioc = ksync_io_ctx();
    FlowTableKSyncEntry *ksync_entry =
        dynamic_cast<FlowTableKSyncEntry *>(ioc->GetKSyncEntry());
    assert(ksync_entry != NULL);
    ksync_entry->ReleaseToken();
    // Handling a new KSync response. Reset the response-info fields, they will
    // be filled below as necessary
    ksync_entry->ResetKSyncResponseInfo();

    assert(r->get_fr_op() == flow_op::FLOW_SET);
    int err = GetErrno();
    if (err == EBADF) {
        LogFlowError(r, err);
    }

    // Skip delete operation.
    if (ioc->event() == KSyncEntry::DEL_ACK) {
        return;
    }

    ksync_entry->SetKSyncResponseInfo(err, r->get_fr_index(),
                                      r->get_fr_gen_id(),
                                      r->get_fr_flow_bytes(),
                                      r->get_fr_flow_packets(),
                                      r->get_fr_flow_stats_oflow());
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
    agent->set_vrouter_max_flow_entries(r->get_vo_flow_entries());
    agent->set_vrouter_max_oflow_entries(r->get_vo_oflow_entries());
    agent->set_vrouter_build_info(r->get_vo_build_info());
    return;
}
