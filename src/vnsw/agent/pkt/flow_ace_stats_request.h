/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_ACE_STATS_REQUEST_H__
#define __AGENT_FLOW_ACE_STATS_REQUEST_H__

#include "pkt/flow_table.h"
#include "pkt/flow_event.h"

////////////////////////////////////////////////////////////////////////////
// Request to the Uve module for Ace Stats
////////////////////////////////////////////////////////////////////////////
class FlowAceStatsRequest {
public:
    enum Event {
        INVALID,
        ADD_FLOW,
        DELETE_FLOW
    };

    FlowAceStatsRequest(Event event, const boost::uuids::uuid &u,
                        const std::string &intf, const std::string &sg_rule,
                        const std::string &vn, const std::string &nw_ace) :
        event_(event), uuid_(u), interface_(intf), sg_rule_uuid_(sg_rule),
        vn_(vn), nw_ace_uuid_(nw_ace) {
    }

    FlowAceStatsRequest(Event event, const boost::uuids::uuid &u) :
        event_(event), uuid_(u) {
    }

    FlowAceStatsRequest(const FlowAceStatsRequest &r) :
        event_(r.event()), uuid_(r.uuid()), interface_(r.interface()),
        sg_rule_uuid_(r.sg_rule_uuid()), vn_(r.vn()),
        nw_ace_uuid_(r.nw_ace_uuid()) {
    }
        

    virtual ~FlowAceStatsRequest() { }

    Event event() const { return event_; }
    const boost::uuids::uuid &uuid() const { return uuid_; }
    const std::string &interface() const { return interface_; }
    const std::string &sg_rule_uuid() const { return sg_rule_uuid_; }
    const std::string &vn() const { return vn_; }
    const std::string &nw_ace_uuid() const { return nw_ace_uuid_; }

private:
    Event event_;
    boost::uuids::uuid uuid_;
    std::string interface_;
    std::string sg_rule_uuid_;
    std::string vn_;
    std::string nw_ace_uuid_;
};
#endif //  __AGENT_FLOW_ACE_STATS_REQUEST_H__
