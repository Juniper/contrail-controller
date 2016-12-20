/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_index_resource_hpp
#define vnsw_agent_index_resource_hpp

#include "resource_manager/index_resource.h"

/*
 * MPLS index allocator using index_vector
 * All the key types below carve out mpls label from same index vector.
 *
 * Uses Resource::MPLS_INDEX
 */

class MplsIndexResourceKey : public IndexResourceKey {
public:    
    MplsIndexResourceKey(const ResourceManager *rm,
                         const boost::uuids::uuid &uuid) :
        IndexResourceKey(rm, uuid, Resource::MPLS_INDEX) { }
    virtual ~MplsIndexResourceKey() { }

private:    
    DISALLOW_COPY_AND_ASSIGN(MplsIndexResourceKey);
};

//Interface mpls label
class InterfaceIndexResourceKey : public MplsIndexResourceKey {
public:    
    InterfaceIndexResourceKey(const ResourceManager *rm,
                 const boost::uuids::uuid &uuid, uint32_t id);
    virtual ~InterfaceIndexResourceKey();

    virtual ToString();
    virtual bool operator!=(const ResourceKey &rhs) const;
    virtual void Copy(const ResourceKey &rhs);

    //Type of label - l2, l3 ....
    uint32_t id_;

private:    
    DISALLOW_COPY_AND_ASSIGN(InterfaceIndexResourceKey);
};

class VrfMplsResourceKey : public MplsIndexResourceKey {
public:    
    VrfMplsResourceKey(const ResourceManager *rm,
                           const std::string &name);
    virtual ~VrfMplsResourceKey();

    virtual ToString();
    virtual bool operator!=(const ResourceKey &rhs) const;
    virtual void Copy(const ResourceKey &rhs);

    const std::string &name_;
private:    
    DISALLOW_COPY_AND_ASSIGN(VrfMplsResourceKey);
};

class RouteMplsResourceKey : public MplsIndexResourceKey {
public:    
    RouteMplsResourceKey(const ResourceManager *rm,
                          const std::string &vrf_name,
                          const std::string &route_str);
    virtual ~RouteMplsResourceKey();

    virtual ToString();
    virtual bool operator!=(const ResourceKey &rhs) const;
    virtual void Copy(const ResourceKey &rhs);

    const std::string &name_;
private:    
    DISALLOW_COPY_AND_ASSIGN(RouteMplsResourceKey);
};

class TestMplsResourceKey : public MplsIndexResourceKey {
public:    
    TestMplsResourceKey(const ResourceManager *rm,
                         const std::string &name);
    virtual ~TestMplsResourceKey();

    virtual ToString();
    virtual bool operator!=(const ResourceKey &rhs) const;
    virtual void Copy(const ResourceKey &rhs);

    const std::string &name_;
private:    
    DISALLOW_COPY_AND_ASSIGN(TestMplsResourceKey);
};

#endif
