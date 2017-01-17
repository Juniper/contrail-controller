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
    virtual void Backup(ResourceData *data, bool del) {assert(0);}

};

class IndexResourceData : public ResourceData {
public:    
    //Used by allocation of resource
    IndexResourceData(ResourceManager *rm,
                      IndexResourceTable *type);
    //Used by restore resource 
    IndexResourceData(ResourceManager *rm,
                      IndexResourceTable *type,
                      uint32_t index);
    virtual ~IndexResourceData();

    uint32_t GetIndex() const;
    void SetIndex(uint32_t index);
    virtual const std::string ToString() {return "";}
private:
    uint32_t index_;
    IndexResourceTable *resource_type_;
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

    void AllocateIndex(IndexResourceData *data);
    void RestoreIndex(IndexResourceData *data);
    void ReleaseIndex(IndexResourceData *data);

private:
    IndexVectorResource<IndexResourceData> index_vector_;
    DISALLOW_COPY_AND_ASSIGN(IndexResourceTable);
};

#endif
