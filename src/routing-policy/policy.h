/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_policy_h
#define ctrlplane_policy_h

#include <boost/scoped_ptr.hpp>

#include "routing-policy/policy_graph.h"

class PolicyGraphBuilder {
    bool AddTerm();

    bool AddDefaultAction();

    bool FlushGraph();

private:
    boost::scoped_ptr<PolicyGraph> graph;
};
#endif //ctrlplane_policy_h
