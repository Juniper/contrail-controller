/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_policy_match_h
#define ctrlplane_policy_match_h

#include "routing-policy/policy_vertex.h"

class PolicyMatchVertex : public PolicyGraphVertex {
    virtual PolicyGraphVertex::VertexType Type() {
        return PolicyMatchVertex::POLICY_MATCH;
    }

    bool operator ==(const PolicyMatchVertex &rhs) const {
        return IsSame(rhs);
    }

    virtual bool IsSame(const PolicyMatchVertex &rhs) const = 0;
};
#endif // ctrlplane_policy_match_h
