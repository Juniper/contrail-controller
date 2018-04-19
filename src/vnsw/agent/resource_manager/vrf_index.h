/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrf_index_resource_hpp
#define vnsw_agent_vrf_index_resource_hpp

/*
 * vrf index allocator using index_vector
 */
#include <oper/interface.h>
#include <resource_manager/index_resource.h>
#include <resource_manager/resource_backup.h>

class ResourceManager;
class ResourceKey;

class VrfIndexResourceKey : public IndexResourceKey {
public:
    VrfIndexResourceKey(ResourceManager *rm,
                        const string &vrf_name);
    virtual ~VrfIndexResourceKey() {};
    virtual void Backup(ResourceData *data, uint16_t op);
    virtual bool IsLess(const ResourceKey &rhs) const;
private:
    std::string vrf_name_;
};
#endif
