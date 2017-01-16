/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_cmn_hpp
#define vnsw_agent_resource_cmn_hpp

class ResourceType;
class ResourceManager;
class IndexResourceType;

class Resource {
public:
    enum Type {
        INVALID = 0,
        MPLS_INDEX,
        MAX,
    };
   
    static ResourceType *Create(Type type, ResourceManager *rm);

private:    
    Resource() { }
    virtual ~Resource() { }
};

#endif
