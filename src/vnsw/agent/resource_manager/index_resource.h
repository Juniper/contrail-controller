/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_index_resource_hpp
#define vnsw_agent_index_resource_hpp

#include "resource_manager/index_vector_resource.h"

using namespace boost::uuids;
using namespace std;

class ResourceTable;
class ResourceManager;
class ResourceKey;
class ResourceData;

class IndexResourceKey : public ResourceKey {
public:    
    IndexResourceKey(ResourceManager *rm, uint16_t type);
    virtual ~IndexResourceKey();

    virtual const std::string ToString() {return "";}
    virtual bool IsLess(const ResourceKey &rhs) const = 0;
    virtual void  Backup(ResourceData *data, bool del) = 0;

};

class IndexResourceData : public ResourceData {
public:    
    //Used by allocation of resource
    IndexResourceData(ResourceManager *rm,
                      IndexResourceTable *table,
                      uint32_t index);
    //Used by restore resource 
    IndexResourceData(ResourceManager *rm,
                      uint32_t index,
                      ResourceManager::KeyPtr key);
    virtual ~IndexResourceData();

    uint32_t GetIndex() const;
    void SetIndex(uint32_t index);
    virtual const std::string ToString() {return "";}
private:
    uint32_t index_;
    IndexResourceTable *resource_table_;
};

class IndexResourceTable : public ResourceTable {
public:
    IndexResourceTable(ResourceManager *rm);
    virtual ~IndexResourceTable();
    typedef ResourceTable::KeyPtr ResourceKeyPtr;
    void ReserveIndex(uint32_t index);
    void ReleaseIndex(uint32_t index);
    void Release(uint32_t index);
    virtual ResourceTable::DataPtr AllocateData(ResourceKeyPtr key);
    virtual const std::string ToString() {return "";}

    uint32_t  AllocateIndex(ResourceKeyPtr key);
    void RestoreIndex(uint32_t index, ResourceKeyPtr key);

private:
    IndexVectorResource<ResourceKeyPtr> index_vector_;
    DISALLOW_COPY_AND_ASSIGN(IndexResourceTable);
};

#endif
