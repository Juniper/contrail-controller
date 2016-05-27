/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>

#include <cmn/agent_cmn.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>

#include <filter/traffic_action.h>
#include <filter/acl_entry_match.h>
#include <filter/acl_entry.h>
#include <filter/acl_entry_spec.h>
#include <filter/packet_header.h>

#include <oper/vn.h>
#include <oper/nexthop.h>
#include <oper/mirror_table.h>

const std::string TrafficAction::kActionLogStr = "log";
const std::string TrafficAction::kActionAlertStr = "alert";

bool TrafficAction::IsDrop() const {
    if (((1 << action_) & TrafficAction::DROP_FLAGS) ||
        ((1 << action_) & TrafficAction::IMPLICIT_DENY_FLAGS)) {
        return true;
    }
    return false;
}

std::string TrafficAction::ActionToString(enum Action at)
{
    switch(at) {
        case ALERT: 
            return kActionAlertStr;
        case DENY:
            return("deny");
        case LOG:
            return kActionLogStr;
        case PASS:
            return("pass");
        case MIRROR:
            return("mirror");
        case TRAP:
            return("trap");
        case IMPLICIT_DENY:
            return("implicit deny");
        case VRF_TRANSLATE:
            return("VRF assign");
        case APPLY_QOS:
            return ("Apply QOS marking");
        default:
            return("unknown");
    }
}

void TrafficAction::SetActionSandeshData(std::vector<ActionStr> &actions) {
    ActionStr astr;
    astr.action = ActionToString(action());
    actions.push_back(astr);
    return;
}

void MirrorAction::SetActionSandeshData(std::vector<ActionStr> &actions) {
    ActionStr astr;
    astr.action = ActionToString(action());
    actions.push_back(astr);
    astr.action = m_ip_.to_string();
    actions.push_back(astr);
    std::stringstream ss;
    ss << port_;
    astr.action = ss.str();
    actions.push_back(astr);
    return;
}

bool MirrorAction::Compare(const TrafficAction &rhs) const {
    const MirrorAction &rhs_mirror_action =
        static_cast<const MirrorAction &>(rhs);
    if (analyzer_name_ != rhs_mirror_action.analyzer_name_) {
        return false;
    }

    if (vrf_name_ != rhs_mirror_action.vrf_name_) {
        return false;
    }

    if (m_ip_ != rhs_mirror_action.m_ip_) {
        return false;
    }

    if (port_ != rhs_mirror_action.port_) {
        return false;
    }

    if (encap_ != rhs_mirror_action.encap_) {
        return false;
    }
    return true;
}

MirrorAction::~MirrorAction() {
    //Agent::mirror_table()->DelMirrorEntry(analyzer_name_);
}

void VrfTranslateAction::SetActionSandeshData(std::vector<ActionStr> &actions) {
    ActionStr astr;
    astr.action = ActionToString(action());
    actions.push_back(astr);
    std::stringstream ss;
    ss << vrf_name_;
    if (ignore_acl_) {
        ss << " ignore acl";
    }
    astr.action = ss.str();
    actions.push_back(astr);
    return;
}

bool VrfTranslateAction::Compare(const TrafficAction &rhs) const {
    const VrfTranslateAction &rhs_vrf_action =
        static_cast<const VrfTranslateAction &>(rhs);
    if (vrf_name_ != rhs_vrf_action.vrf_name_) {
        return false;
    }
    if (ignore_acl_ != rhs_vrf_action.ignore_acl_) {
        return false;
    }
    return true;
}

void QosConfigAction::SetActionSandeshData(std::vector<ActionStr> &actions) {
    ActionStr astr;
    astr.action = ActionToString(action());
    actions.push_back(astr);
    std::stringstream ss;
    ss << name_;
    astr.action = ss.str();
    actions.push_back(astr);
    return;
}

bool QosConfigAction::Compare(const TrafficAction &r) const {
    const QosConfigAction &rhs = static_cast<const QosConfigAction &>(r);
    if (name_ != rhs.name_) {
        return false;
    }
    if (qos_config_ref_.get() != rhs.qos_config_ref_.get()) {
        return false;
    }
    return true;
}
