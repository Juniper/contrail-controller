/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_policy_action_h
#define ctrlplane_policy_action_h

#include "routing-policy/policy_vertex.h"

class PolicyActionVertex : public PolicyGraphVertex {
    virtual PolicyGraphVertex::VertexType Type() {
        return PolicyMatchVertex::POLICY_ACTION;
    }

    virtual bool IsTerminal() {
        return false;
    }

    virtual bool IsAccept() {
        return false;
    }
};

class PolicyActionAccept : public PolicyActionVertex {
    virtual bool IsTerminal() {
        return true;
    }

    virtual bool IsAccept() {
        return true;
    }
};

class PolicyActionReject : public PolicyActionVertex {
    virtual bool IsAccept() {
        return false;
    }

    virtual bool IsTerminal() {
        return true;
    }
};

#endif // ctrlplane_policy_action_h
