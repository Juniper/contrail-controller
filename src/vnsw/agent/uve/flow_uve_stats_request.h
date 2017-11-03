/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_UVE_STATS_REQUEST_H__
#define __AGENT_FLOW_UVE_STATS_REQUEST_H__

#include "pkt/flow_table.h"
#include "pkt/flow_event.h"

struct FlowUveFwPolicyInfo {
    TagList local_tagset_;
    std::string fw_policy_;
    TagList remote_tagset_;
    std::string remote_prefix_;
    std::string remote_vn_;
    std::string local_vn_;
    bool initiator_;
    bool added_;
    std::string action_;
    bool short_flow_;
    bool is_valid_;
    FlowUveFwPolicyInfo() : is_valid_(false) {}
};

struct FlowUveVnAcePolicyInfo {
    std::string vn_;
    std::string nw_ace_uuid_;
    bool is_valid_;
    FlowUveVnAcePolicyInfo() : is_valid_(false) {}
};

////////////////////////////////////////////////////////////////////////////
// Request to the Uve module for Flow based statistics update
////////////////////////////////////////////////////////////////////////////
class FlowUveStatsRequest {
public:
    enum Event {
        INVALID,
        ADD_FLOW,
        DELETE_FLOW
    };

    FlowUveStatsRequest(Event event, const boost::uuids::uuid &u,
                        const std::string &intf, const std::string &sg_rule,
                        const FlowUveVnAcePolicyInfo &vn_ace_info,
                        const FlowUveFwPolicyInfo &fw_policy_info) :
        event_(event), uuid_(u), interface_(intf), sg_rule_uuid_(sg_rule),
        sg_info_valid_(false), vn_ace_info_(vn_ace_info),
        fw_policy_info_(fw_policy_info) {
        if (!intf.empty() && !sg_rule.empty()) {
            sg_info_valid_ = true;
        }
    }

    FlowUveStatsRequest(Event event, const boost::uuids::uuid &u,
                        const std::string &intf,
                        const FlowUveFwPolicyInfo &fw_policy_info) :
        event_(event), uuid_(u), interface_(intf), sg_info_valid_(false),
        fw_policy_info_(fw_policy_info) {
    }

    ~FlowUveStatsRequest() { }

    Event event() const { return event_; }
    const boost::uuids::uuid &uuid() const { return uuid_; }
    const std::string &interface() const { return interface_; }
    const std::string &sg_rule_uuid() const { return sg_rule_uuid_; }
    bool sg_info_valid() const { return sg_info_valid_; }
    const FlowUveVnAcePolicyInfo &vn_ace_info() const { return vn_ace_info_; }
    bool vn_ace_valid() const { return vn_ace_info_.is_valid_; }
    const FlowUveFwPolicyInfo &fw_policy_info() const { return fw_policy_info_;}
    bool fw_policy_valid() const { return fw_policy_info_.is_valid_; }

private:
    Event event_;
    boost::uuids::uuid uuid_;
    std::string interface_;
    std::string sg_rule_uuid_;
    bool sg_info_valid_;
    FlowUveVnAcePolicyInfo vn_ace_info_;
    FlowUveFwPolicyInfo fw_policy_info_;

    DISALLOW_COPY_AND_ASSIGN(FlowUveStatsRequest);
};
#endif //  __AGENT_FLOW_UVE_STATS_REQUEST_H__
