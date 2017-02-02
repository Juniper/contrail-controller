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
#include <resource_manager/index_resource.h>
#include <resource_manager/resource_backup.h>
class ResourceManager;
class ResourceKey;

class MplsIndexResourceKey : public IndexResourceKey {
public:
    enum Type {
        INTERFACE,
        VRF,
        ROUTE,
        TEST,
        EDGEMCAST
    };
    MplsIndexResourceKey(ResourceManager *rm, Type type);
    virtual ~MplsIndexResourceKey();
    virtual void Backup(ResourceData *data, uint16_t op) {assert(0);}
    Type type() const {return type_;}
private:
    Type type_;
};

//Interface mpls label
class InterfaceIndexResourceKey : public MplsIndexResourceKey {
public:
    InterfaceIndexResourceKey(ResourceManager *rm,
                              const boost::uuids::uuid &uuid,
                              const MacAddress &mac, bool policy,
                              uint32_t label_type, uint16_t vlan_tag);
    virtual ~InterfaceIndexResourceKey();

    virtual const std::string ToString() { return "";}
    virtual bool IsLess(const ResourceKey &rhs) const;
    virtual void Backup(ResourceData *data, uint16_t op);
private:
    const boost::uuids::uuid uuid_;
    //Type of label - l2, l3 ....
    uint32_t label_type_;
    MacAddress mac_;
    bool policy_;
    uint16_t vlan_tag_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceIndexResourceKey);
};

//Vrf mpls label
class VrfMplsResourceKey : public MplsIndexResourceKey {
public:
    VrfMplsResourceKey(ResourceManager *rm,
                           const std::string &name);
    virtual ~VrfMplsResourceKey();

    virtual const std::string ToString() { return "";}
    virtual bool IsLess(const ResourceKey &rhs) const;
    virtual void Backup(ResourceData *data, uint16_t op);
private:
    const std::string name_;
    DISALLOW_COPY_AND_ASSIGN(VrfMplsResourceKey);
};

//Route mpls label
class RouteMplsResourceKey : public MplsIndexResourceKey {
public:
    RouteMplsResourceKey(ResourceManager *rm,
                          const std::string &vrf_name,
                          const std::string route_str);
    virtual ~RouteMplsResourceKey();

    virtual const std::string ToString() { return "";}
    virtual bool IsLess(const ResourceKey &rhs) const;
    virtual void Backup(ResourceData *data, uint16_t op);
private:
    const std::string vrf_name_;
    const std::string route_key_;
    DISALLOW_COPY_AND_ASSIGN(RouteMplsResourceKey);
};

class TestMplsResourceKey : public MplsIndexResourceKey {
public:
    TestMplsResourceKey(ResourceManager *rm,
                         const std::string &name);
    virtual ~TestMplsResourceKey();

    virtual const std::string ToString() { return "";}
    virtual bool IsLess(const ResourceKey &rhs) const;
    virtual void Backup(ResourceData *data, uint16_t op) { }

    const std::string name_;
    DISALLOW_COPY_AND_ASSIGN(TestMplsResourceKey);
};

#endif
