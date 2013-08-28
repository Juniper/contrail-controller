/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_POLICY_CONFIG_SPEC_H__
#define __AGENT_POLICY_CONFIG_SPEC_H__

#include <boost/uuid/uuid.hpp>
#include <vector>

typedef boost::uuids::uuid uuid;

struct PolicyConfigSpec {
    uuid vpc_id;
    uuid policy_id;
    std::string name;
    bool inbound;
    uuid acl_id;
};

#endif
