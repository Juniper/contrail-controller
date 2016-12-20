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

class IndexResourceKey : public ResourceKey {
public:    
    IndexResourceKey(const ResourceManager *rm,
                 const boost::uuids::uuid &uuid,
                 uint16_t type);
    virtual ~IndexResourceKey();

    virtual ToString();
    virtual bool operator!=(const ResourceKey &rhs) const;
    virtual void Copy(const ResourceKey &rhs);

    const uuid &uuid_;
};

class IndexResourceData : public ResourceData {
public:    
    IndexResourceData(const ResourceManager *rm,
                      uint16_t type);
    virtual ~IndexResourceData();

    uint32_t GetIndex() const;
    void SetIndex(uint32_t index);
private:    
    uint32_t index_;
};

class IndexResourceType : public ResourceType {
public:
    IndexResourceType(const ResourceManager *ra);
    virtual ~IndexResourceType();

    virtual ResourceType::DataPtr AllocateData(ResourceType::KeyPtr key);
    virtual void ReleaseData(ResourceType::KeyPtr key);

    void AllocateIndex(IndexResourceData *data);
    void ReleaseIndex(IndexResourceData *data);

private:
    IndexVectorResource<IndexResourceData> index_vector_;
    DISALLOW_COPY_AND_ASSIGN(IndexResourceType);      
};

#endif
