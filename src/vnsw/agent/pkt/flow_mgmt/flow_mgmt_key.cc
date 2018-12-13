/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <pkt/flow_mgmt/flow_mgmt_key.h>
#include <oper/health_check.h>

FlowEvent::Event FlowMgmtKey::FreeDBEntryEvent() const {
    FlowEvent::Event event = FlowEvent::INVALID;

    switch (type_) {
    case INTERFACE:
    case ACL:
    case VN:
    case INET4:
    case INET6:
    case BRIDGE:
    case NH:
    case VRF:
        event = FlowEvent::FREE_DBENTRY;
        break;

    case ACE_ID:
    case VM:
        event = FlowEvent::INVALID;
        break;
    case BGPASASERVICE:
        event = FlowEvent::INVALID;

    default:
        assert(0);
    }

    return event;
}

void BgpAsAServiceFlowMgmtKey::StartHealthCheck(
        Agent *agent, FlowEntry *flow, const boost::uuids::uuid &hc_uuid) {
    if (bgp_health_check_instance_ != NULL) {
        bgp_health_check_instance_->UpdateInstanceTask();
        return;
    }

    bgp_health_check_service_ = agent->health_check_table()->Find(hc_uuid);
    if (bgp_health_check_service_ == NULL) {
        LOG(DEBUG, "Unable to start BFD health check " << hc_uuid << " on flow "
                   " destination = " << flow->key().dst_addr <<
                   " source = " << flow->key().src_addr);
        return;
    }

    const VmInterface *vm_interface =
        static_cast<const VmInterface *>(flow->intf_entry());
    bgp_health_check_instance_ =
        bgp_health_check_service_->StartHealthCheckService(
            const_cast<VmInterface *>(vm_interface),
            flow->key().dst_addr, flow->key().src_addr);
    bgp_health_check_instance_->SetService(bgp_health_check_service_);
}

void BgpAsAServiceFlowMgmtKey::StopHealthCheck(FlowEntry *flow) {
    if (bgp_health_check_instance_) {
        bgp_health_check_instance_->StopTask(bgp_health_check_service_);
        bgp_health_check_instance_ = NULL;
        bgp_health_check_service_ = NULL;
    }
}

bool InetRouteFlowMgmtKey::NeedsReCompute(const FlowEntry *flow) {
    if (Match(flow->key().src_addr)) {
        return true;
    }

    if (Match(flow->key().dst_addr)) {
        return true;
    }

    const FlowEntry *rflow = flow->reverse_flow_entry();
    if (rflow == NULL) {
        return true;
    }

    if (Match(rflow->key().src_addr)) {
        return true;
    }

    if (Match(rflow->key().dst_addr)) {
        return true;
    }

    return false;
}
