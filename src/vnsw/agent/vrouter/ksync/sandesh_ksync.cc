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
        LOG(ERROR, "VrResponseMsg Error: " <<
            KSyncEntry::VrouterErrorToString(-response_code_));
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

    assert(r->get_fr_op() == flow_op::FLOW_SET);
    int err = GetErrno();
    if (err == EBADF) {
        LogFlowError(r, err);
    }

    const KSyncIoContext *ioc = ksync_io_ctx();
    // Skip delete operation.
    if (ioc->event() == KSyncEntry::DEL_ACK) {
        return;
    }

    FlowTableKSyncEntry *ksync_entry =
        dynamic_cast<FlowTableKSyncEntry *>(ioc->GetKSyncEntry());
    assert(ksync_entry != NULL);

    FlowEntry *flow = ksync_entry->flow_entry().get();
    if (flow == NULL)
        return;

    tbb::mutex::scoped_lock lock(flow->mutex());
    FlowProto *proto = flow->flow_table()->agent()->pkt()->get_flow_proto();
    if (err == EBADF || err == ENOSPC) {
        if ((err = EBADF) && (((int)flow->flow_handle() != r->get_fr_index())))
            return;

        proto->KSyncFlowErrorRequest(ksync_entry);
        return;
    }

    if (flow->flow_handle() != FlowEntry::kInvalidFlowHandle) {
        if ((int)flow->flow_handle() != r->get_fr_index()) {
            LOG(DEBUG, "Flow index changed from <" <<
                flow->flow_handle() << "> to <" <<
                r->get_fr_index() << ">");
        }
    }

    KSyncFlowIndexManager *imgr = flow_ksync_->ksync()->
        ksync_flow_index_manager();
    FlowMgmtManager *mgr = flow_ksync_->ksync()->agent()->pkt()->
        flow_mgmt_manager();
    FlowEntryPtr evicted_flow = imgr->FindByIndex(r->get_fr_index());
    if (evicted_flow.get() && evicted_flow->deleted() == false) {
        mgr->FlowStatsUpdateEvent(evicted_flow.get(), r->get_fr_flow_bytes(),
                                  r->get_fr_flow_packets(),
                                  r->get_fr_flow_stats_oflow());
    }
    proto->KSyncFlowHandleRequest(ksync_entry, r->get_fr_index());
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
