/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_PKT_FLOW_ENTRY_INFO_H__
#define __AGENT_PKT_FLOW_ENTRY_INFO_H__

#include <pkt/flow_mgmt/flow_mgmt_tree.h>

////////////////////////////////////////////////////////////////////////////
// Per flow information stored in flow-mgmt module. Holds a reference to
// flow so that flow active till flow-mgmt processing is done
////////////////////////////////////////////////////////////////////////////
class FlowEntryInfo {
public:
    FlowEntryInfo(FlowEntry *flow) :
        flow_(flow), tree_(), count_(0), ingress_(false), local_flow_(false) {
    }
    virtual ~FlowEntryInfo() { assert(tree_.size() == 0); }
    const FlowMgmtKeyTree &tree() const { return tree_; }

private:
    friend class FlowMgmtManager;
    FlowEntryPtr flow_;
    FlowMgmtKeyTree tree_;
    uint32_t count_; // Number of times tree modified
    bool ingress_;
    bool local_flow_;
    DISALLOW_COPY_AND_ASSIGN(FlowEntryInfo);
};

#endif // __AGENT_PKT_FLOW_ENTRY_INFO_H__
