/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <base/address_util.h>
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

void vr_pkt_drop_log_req::Process(SandeshContext *context)
{}

void vr_drop_stats_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->DropStatsMsgHandler(this);
}

void vr_vrf_stats_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->VrfStatsMsgHandler(this);
}

void vr_hugepage_config::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->VrouterHugePageHandler(this);
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

void KSyncSandeshContext::BridgeTableInfoHandler(vr_bridge_table_data *r) {
    assert(r->get_btable_op() == sandesh_op::GET);
    ksync_->ksync_bridge_memory()->set_major_devid(r->get_btable_dev());
    ksync_->ksync_bridge_memory()->set_table_size(r->get_btable_size());
    if (r->get_btable_file_path() != Agent::NullString()) {
        ksync_->ksync_bridge_memory()->
            set_table_path(r->get_btable_file_path());
    }
    LOG(DEBUG, "Flow table size : " << r->get_btable_size());
    return;
}

void KSyncSandeshContext::FlowTableInfoHandler(vr_flow_table_data *r) {
    assert(r->get_ftable_op() == flow_op::FLOW_TABLE_GET);
    ksync_->ksync_flow_memory()->set_major_devid(r->get_ftable_dev());
    ksync_->ksync_flow_memory()->set_table_size(r->get_ftable_size());
    if (r->get_ftable_file_path() != Agent::NullString()) {
        ksync_->ksync_flow_memory()->set_table_path(r->get_ftable_file_path());
    }
    LOG(DEBUG, "Flow table size : " << r->get_ftable_size());
}

static void LogFlowError(vr_flow_response *r, int err) {
    string op;
    if (r->get_fresp_flags() != 0) {
        op = "Add/Update";
    } else {
        op = "Delete";
    }

    LOG(ERROR, "Error Flow entry op = " << op
        << " flow_handle = " << (int) r->get_fresp_index()
        << " gen-id = " << (int) r->get_fresp_gen_id());
}

// Handle vr_flow response from VRouter
// We combine responses from both vr_flow and vr_response messages and
// generate single event. Copy the results in vr_flow in KSync entry.
// On receiving vr_response message, event will be generated for both messages
void KSyncSandeshContext::FlowResponseHandler(vr_flow_response *r) {
    assert(r->get_fresp_op() == flow_op::FLOW_SET);

    const KSyncIoContext *ioc = ksync_io_ctx();
    FlowTableKSyncEntry *ksync_entry =
        dynamic_cast<FlowTableKSyncEntry *>(ioc->GetKSyncEntry());
    assert(ksync_entry != NULL);
    ksync_entry->ReleaseToken();
    // Handling a new KSync response. Reset the response-info fields, they will
    // be filled below as necessary
    ksync_entry->ResetKSyncResponseInfo();

    assert(r->get_fresp_op() == flow_op::FLOW_SET);
    int err = GetErrno();
    if (err == EBADF) {
        LogFlowError(r, err);
    }

    // Skip delete operation.
    if (ioc->event() == KSyncEntry::DEL_ACK) {
        return;
    }

    ksync_entry->SetKSyncResponseInfo(err, r->get_fresp_index(),
                                      r->get_fresp_gen_id(),
                                      r->get_fresp_bytes(),
                                      r->get_fresp_packets(),
                                      r->get_fresp_stats_oflow());
    return;
}

void KSyncSandeshContext::FlowMsgHandler(vr_flow_req *r) {
    assert(0);
}

void KSyncSandeshContext::IfMsgHandler(vr_interface_req *r) {
    context_marker_ = r->get_vifr_idx();
}

void KSyncSandeshContext::VrouterHugePageHandler(vr_hugepage_config *r) {
    std::string message;

    switch(r->get_vhp_resp()) {
        case VR_HPAGE_CFG_RESP_HPAGE_SUCCESS:
            message = "Huge pages set successfully";
            break;
        case VR_HPAGE_CFG_RESP_MEM_FAILURE:
            message = "ERROR !! Failed to set huge pages and vrouter couldnt allocate memory !!";
            break;
        case VR_HPAGE_CFG_RESP_INVALID_ARG_MEM_INITED:
            message = "Invalid huge pages argument, vrouter using allocated memory (not huge pages)";
            break;
        case VR_HPAGE_CFG_RESP_HPAGE_FAILURE_MEM_INITED:
            message = "Failed to set huge pages, vrouter using allocated memory (not huge pages)";
            break;
        case VR_HPAGE_CFG_RESP_MEM_ALREADY_INITED:
            message = "Vrouter using already allocated memory";
            break;
        case VR_HPAGE_CFG_RESP_HPAGE_PARTIAL_SUCCESS:
            message = "Not all huge pages are successfully set, vrouter using allocated memory (not huge pages)";
            break;
        default:
            message = "Failed to set huge pages, invalid vrouter response";
            break;
    }

    LOG(INFO, message);
}

void KSyncSandeshContext::VrouterOpsMsgHandler(vrouter_ops *r) {
    Agent *agent = ksync_->agent();
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
    agent->set_vrouter_priority_tagging(r->get_vo_priority_tagging());
    return;
}
