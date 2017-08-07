/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_bgp_as_service_index_resource_hpp
#define vnsw_agent_bgp_as_service_index_resource_hpp

/*
 * bgp index allocator using index_vector
 */
#include <oper/interface.h>
#include <resource_manager/index_resource.h>
#include <resource_manager/resource_backup.h>

class ResourceManager;
class ResourceKey;

class BgpAsServiceIndexResourceKey : public IndexResourceKey {
public:
    BgpAsServiceIndexResourceKey(ResourceManager *rm,
                        const boost::uuids::uuid &uuid_);
    virtual ~BgpAsServiceIndexResourceKey() {};
    virtual void Backup(ResourceData *data, uint16_t op);
    virtual bool IsLess(const ResourceKey &rhs) const;
private:
    boost::uuids::uuid uuid_;
};
#endif
