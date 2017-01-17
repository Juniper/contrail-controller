/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mpls_index_resource_hpp
#define vnsw_agent_mpls_index_resource_hpp

/*
 * MPLS index allocator using index_vector
 * All the key types below carve out mpls label from same index vector.
 *
 * Uses Resource::MPLS_INDEX
 */

using namespace boost::uuids;
using namespace std;

class ResourceManager;
class ResourceKey;
#include <resource_manager/index_resource.h>

class MplsIndexResourceKey : public IndexResourceKey {
public:
    enum Type {
        INTERFACE,
        VRF,
        ROUTE,
        TEST,
        EDGEMCAST
    };
    MplsIndexResourceKey(ResourceManager *rm,
                         Type type);
    virtual ~MplsIndexResourceKey();
    virtual void Backup(ResourceData *data, bool del) {assert(0);}

    Type type_;
};

//Interface mpls label
class InterfaceIndexResourceKey : public MplsIndexResourceKey {
public:
    InterfaceIndexResourceKey(ResourceManager *rm,
                              const boost::uuids::uuid &uuid,
                              const MacAddress &mac, bool policy, uint32_t id);
    virtual ~InterfaceIndexResourceKey();

    virtual const std::string ToString() { return "";}
    virtual bool IsLess(const ResourceKey &rhs) const;
    virtual void Backup(ResourceData *data, bool del);

    //Type of label - l2, l3 ....
    uint32_t id_;
    MacAddress mac_;
    bool policy_;
    const uuid uuid_;
};

class VrfMplsResourceKey : public MplsIndexResourceKey {
public:
    VrfMplsResourceKey(ResourceManager *rm,
                           const std::string &name);
    virtual ~VrfMplsResourceKey();

    virtual const std::string ToString() { return "";}
    virtual bool IsLess(const ResourceKey &rhs) const;
    virtual void Backup(ResourceData *data, bool del);

    const std::string name_;
};

class RouteMplsResourceKey : public MplsIndexResourceKey {
public:
    RouteMplsResourceKey(ResourceManager *rm,
                          const std::string &vrf_name,
                          const std::string route_str);
    virtual ~RouteMplsResourceKey();

    virtual const std::string ToString() { return "";}
    virtual bool IsLess(const ResourceKey &rhs) const;
    virtual void Backup(ResourceData *data, bool del);

    //TODO make it routekey
    const std::string vrf_name_;
    const std::string route_str_;
};

class TestMplsResourceKey : public MplsIndexResourceKey {
public:
    TestMplsResourceKey(ResourceManager *rm,
                         const std::string &name);
    virtual ~TestMplsResourceKey();

    virtual const std::string ToString() { return "";}
    virtual bool IsLess(const ResourceKey &rhs) const;
    virtual void Backup(ResourceData *data, bool del) { }

    const std::string name_;
};

class EdgeMulticastMplsResourceKey : public MplsIndexResourceKey {
public:
    EdgeMulticastMplsResourceKey(ResourceManager *rm,
                                 uint32_t index);
    virtual ~EdgeMulticastMplsResourceKey();

    virtual const std::string ToString() { return "";}
    virtual bool IsLess(const ResourceKey &rhs) const;
    virtual void Backup(ResourceData *data, bool del) { }

    uint32_t index_;
};

#endif
