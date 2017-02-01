/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_index_resource_hpp
#define vnsw_agent_index_resource_hpp

#include "resource_manager/index_vector_resource.h"
#include "resource_cmn.h"
class ResourceTable;
class ResourceManager;
class ResourceKey;
class ResourceData;

class IndexResourceKey : public ResourceKey {
public:
    IndexResourceKey(ResourceManager *rm, Resource::Type resource_key_type);
    virtual ~IndexResourceKey();

    virtual const std::string ToString() {return "";}
    virtual bool IsLess(const ResourceKey &rhs) const = 0;
    virtual void  Backup(ResourceData *data, uint16_t op) = 0;

};

class IndexResourceData : public ResourceData {
public:
    //Used by allocation & restore of resource
    IndexResourceData(ResourceManager *rm,
                      uint32_t index);
    virtual ~IndexResourceData();

    uint32_t index() const;
    virtual const std::string ToString() {return "";}
private:
    uint32_t index_;
    DISALLOW_COPY_AND_ASSIGN(IndexResourceData);
};
// This Class is responsible for allocating the unique resource index
// for each key we are adding to the table.
class IndexResourceTable : public ResourceTable {
public:
    IndexResourceTable(ResourceManager *rm);
    virtual ~IndexResourceTable();
    void ReserveIndex(uint32_t index);
    void ReleaseIndex(uint32_t index);
    void Release(uint32_t index);
    virtual ResourceTable::DataPtr AllocateData(ResourceKeyPtr key);
    virtual const std::string ToString() {return "";}

    uint32_t  AllocateIndex(ResourceKeyPtr key);
    void RestoreIndex(uint32_t index, ResourceKeyPtr key);
    void RestoreKey(KeyPtr key, DataPtr data);
    void ReleaseKey(KeyPtr key, DataPtr data);

private:
    IndexVectorResource<ResourceKeyPtr> index_vector_;
    DISALLOW_COPY_AND_ASSIGN(IndexResourceTable);
};

#endif
