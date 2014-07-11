/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <vnsw/agent/filter/traffic_action.h>
#include <vnsw/agent/cmn/agent_cmn.h>
#include <oper/vn.h>
#include <oper/nexthop.h>
#include <vnsw/agent/oper/mirror_table.h>

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
            return("alert");
        case DROP:
            return("drop");
        case DENY:
            return("deny");
        case LOG:
            return("log");
        case PASS:
            return("pass");
        case REJECT:
            return("reject");
        case MIRROR:
	    return("mirror");
        case TRAP:
            return("trap");
        case IMPLICIT_DENY:
            return("implicit deny");
        case VRF_TRANSLATE:
            return("VRF assign");
        default:
            return("unknown");
    }
}

void TrafficAction::SetActionSandeshData(std::vector<ActionStr> &actions) {
    ActionStr astr;
    astr.action = ActionToString(action_);
    actions.push_back(astr);
    return;
}

void MirrorAction::SetActionSandeshData(std::vector<ActionStr> &actions) {
    ActionStr astr;
    astr.action = ActionToString(action_);
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


void VrfTranslateAction::SetActionSandeshData(std::vector<ActionStr> &actions) {
    ActionStr astr;
    astr.action = ActionToString(action_);
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

MirrorAction::~MirrorAction() {
    //Agent::mirror_table()->DelMirrorEntry(analyzer_name_);
}
