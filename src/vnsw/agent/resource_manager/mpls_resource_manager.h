/*
 *  * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#ifndef agent_mpls_resource_manager_h
#define agent_mpls_resource_manager_h
#include <map>
#include <string>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "resource_manager.h"
#include "resource_manager/mpls_resource_data_types.h"

/*This Structure Needs to be modified based on actual Data*/
struct  MplsData : public ResourceData {
    uint32_t mpls_label; 
    std::string data;
};

struct MplsKey : public ResourceKey {
    uint32_t mpls_resource_key;
};

class MplsResourceManager : public ResourceManager {
public:
    static const std::string mpls_file_;
    static const std::string mpls_tmp_file_;
    MplsResourceManager(const std::string &file, const std::string &tmp_file,
            Agent *agent);
    ~MplsResourceManager() { }
    virtual void AddResourceData(const ResourceReq& );
    virtual void ReadResourceData(ResourceMap &rmap);
    virtual void DeleteResourceData(const ResourceReq& );
    virtual void ModifyResourceData(const ResourceReq&);
    virtual void EnCode();
    virtual void DeCode();
private:
    // Mplsresourcekey sandesh Genrated structure
    // MplsResourceData sandesh Genrated Structure
    // Resource Key can be defined based on requirement.
    std::map<uint32_t, MplsResourceData> mpls_data_map_;
    DISALLOW_COPY_AND_ASSIGN(MplsResourceManager);    
};
#endif
