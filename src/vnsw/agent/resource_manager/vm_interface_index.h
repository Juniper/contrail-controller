/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_interface_index_resource_hpp
#define vnsw_agent_interface_index_resource_hpp

/*
 * interface index allocator using index_vector
 */
#include <oper/interface.h>
#include <resource_manager/index_resource.h>
#include <resource_manager/resource_backup.h>

class ResourceManager;
class ResourceKey;
// Vm interface backup.
class VmInterfaceIndexResourceKey : public IndexResourceKey {
public:
    VmInterfaceIndexResourceKey(ResourceManager *rm,
                                const boost::uuids::uuid &uuid,
                                const std::string &interface_name);
    virtual ~VmInterfaceIndexResourceKey() {};
    virtual void Backup(ResourceData *data, uint16_t op);
    virtual bool IsLess(const ResourceKey &rhs) const;
private:
    boost::uuids::uuid uuid_;
    std::string interface_name_;
};
#endif
