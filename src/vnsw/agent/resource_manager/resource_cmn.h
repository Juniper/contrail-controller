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
   
    static std::stringstream ResourceBackupFileName(Type type) {
        if (type == MPLS_INDEX)
            return "mpls_index.contrail.res";
        assert(0);
        return "";
    }

    static ResourceType *Create(ResourceType::Type type, ResourceManager *rm) {
        switch (type) {
        case MPLS_INDEX: {
            return new MplsIndexResourceType(rm);                 
        }
        default: {
            assert(0);             
        }                     
        };
        return NULL;
    }

    static Translate();

private:    
    Resource() { }
    virtual ~Resource() { }
};

#endif
