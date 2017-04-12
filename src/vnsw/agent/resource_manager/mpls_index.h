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
extern SandeshTraceBufferPtr InterfaceMplsDataTraceBuf;
extern SandeshTraceBufferPtr VrfMplsDataTraceBuf;
extern SandeshTraceBufferPtr VlanMplsDataTraceBuf;
extern SandeshTraceBufferPtr RouteMplsDataTraceBuf;

class ResourceManager;
class ResourceKey;
class NextHopKey;

class MplsIndexResourceKey : public IndexResourceKey {
public:
    enum Type {
        NEXTHOP,
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

//Nexthop mpls label
class NexthopIndexResourceKey : public MplsIndexResourceKey {
public:
    NexthopIndexResourceKey(ResourceManager *rm, NextHopKey *nh_key);
    virtual ~NexthopIndexResourceKey();

    virtual const std::string ToString() { return "";}
    virtual bool IsLess(const ResourceKey &rhs) const;
    virtual void Backup(ResourceData *data, uint16_t op);
    void BackupInterfaceResource(ResourceData *data, uint16_t op);
    void BackupVrfResource(ResourceData *data, uint16_t op);
    void BackupVlanResource(ResourceData *data, uint16_t op);
    const NextHopKey *GetNhKey() const { return nh_key_.get(); }
private:
    std::auto_ptr<NextHopKey> nh_key_;
    DISALLOW_COPY_AND_ASSIGN(NexthopIndexResourceKey);
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
