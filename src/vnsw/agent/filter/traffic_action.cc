/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <vnsw/agent/filter/traffic_action.h>
#include <vnsw/agent/cmn/agent_cmn.h>
#include <oper/vn.h>
#include <oper/nexthop.h>
#include <vnsw/agent/oper/mirror_table.h>

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

MirrorAction::~MirrorAction() {
    //Agent::GetMirrorTable()->DelMirrorEntry(analyzer_name_);
}
