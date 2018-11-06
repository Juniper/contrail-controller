/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_PKT_FLOW_MGMT_KEY_NODE_H__
#define __AGENT_PKT_FLOW_MGMT_KEY_NODE_H__

#include <cstdlib>
#include <boost/intrusive/list_hook.hpp>

class FlowEntry;

class FlowMgmtKeyNode {
public:
    FlowMgmtKeyNode() : flow_(NULL) { }
    FlowMgmtKeyNode(FlowEntry *fe) : flow_(fe) { }
    virtual ~FlowMgmtKeyNode() { }
    FlowEntry *flow_entry() const { return flow_; }

private:
    friend class FlowMgmtEntry;
    FlowEntry *flow_;
    boost::intrusive::list_member_hook<> hook_;
};

#endif // __AGENT_PKT_FLOW_MGMT_KEY_NODE_H__
