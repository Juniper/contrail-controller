/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_cmn_hpp
#define vnsw_agent_resource_cmn_hpp

class ResourceTable;
class ResourceManager;
class IndexResourceTable;
// This Class is to Create the Resource Table
// based on type of the Index requested.
class Resource {
public:
    enum Type {
        INVALID = 0,
        MPLS_INDEX,
        INTERFACE_INDEX,
        VRF_INDEX,
        QOS_INDEX,
        BGP_AS_SERVICE_INDEX,
        MIRROR_INDEX,
        MAX
    };

    static ResourceTable *Create(Type type, ResourceManager *rm);

private:
    Resource() { }
    virtual ~Resource() { }
};

#endif
