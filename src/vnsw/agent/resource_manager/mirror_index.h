/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mirror_index_resource_hpp
#define vnsw_agent_mirror_index_resource_hpp

/*
 * mirror index allocator using index_vector
 */
#include <resource_manager/index_resource.h>
#include <resource_manager/resource_backup.h>

class ResourceManager;
class ResourceKey;

class MirrorIndexResourceKey : public IndexResourceKey {
public:
    MirrorIndexResourceKey(ResourceManager *rm,
                        const string &analyzer_name);
    virtual ~MirrorIndexResourceKey() {};
    virtual void Backup(ResourceData *data, uint16_t op);
    virtual bool IsLess(const ResourceKey &rhs) const;
private:
    std::string analyzer_name_;
};
#endif
