/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <resource_manager/resource_cmn.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/resource_manager.h>

//Include various data types and derivatives
#include <resource_manager/index_resource.h>
#include <resource_manager/mpls_index.h>

ResourceTable *Resource::Create(Type type, ResourceManager *rm) {
    switch (type) {
    case MPLS_INDEX:
    case INTERFACE_INDEX:
    case VRF_INDEX:
    case QOS_INDEX:
    case BGP_AS_SERVICE_INDEX:
    case MIRROR_INDEX:
        return new IndexResourceTable(rm);
    default: {
        assert(0);
    }
    };
    return NULL;
}
