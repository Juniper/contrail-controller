/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_index_resource_hpp
#define vnsw_agent_index_resource_hpp

#include "resource_manager/index_vector_resource.h"

using namespace boost::uuids;
using namespace std;

class ResourceType;
class ResourceManager;
class ResourceKey;
class ResourceData;

class IndexResourceKey : public ResourceKey {
public:    
    IndexResourceKey(ResourceManager *rm,
                     const boost::uuids::uuid &uuid,
                     uint16_t type);
    virtual ~IndexResourceKey();

    virtual const std::string ToString() {return "";}
    virtual bool IsLess(const ResourceKey &rhs) const;
    virtual void Backup(ResourceData *data, bool del) {assert(0);}

    const uuid uuid_;
};

class IndexResourceData : public ResourceData {
public:    
    //Used by allocation of resource
    IndexResourceData(ResourceManager *rm,
                      IndexResourceType *type);
    //Used by restore resource 
    IndexResourceData(ResourceManager *rm,
                      IndexResourceType *type,
                      uint32_t index);
    virtual ~IndexResourceData();

    uint32_t GetIndex() const;
    void SetIndex(uint32_t index);
    virtual const std::string ToString() {return "";}
private:    
    uint32_t index_;
    IndexResourceType *resource_type_;
};

class IndexResourceType : public ResourceType {
public:
    IndexResourceType(ResourceManager *rm);
    virtual ~IndexResourceType();

    void ReserveIndex(uint32_t index);
    void ReleaseIndex(uint32_t index);
    void Release(uint32_t index);
    virtual ResourceType::DataPtr AllocateData(ResourceType::KeyPtr key);
    virtual void ReleaseData(ResourceType::KeyPtr key);
    virtual const std::string ToString() {return "";}

    void AllocateIndex(IndexResourceData *data);
    void RestoreIndex(IndexResourceData *data);
    void ReleaseIndex(IndexResourceData *data);

private:
    IndexVectorResource<IndexResourceData> index_vector_;
    DISALLOW_COPY_AND_ASSIGN(IndexResourceType);      
};

#endif
