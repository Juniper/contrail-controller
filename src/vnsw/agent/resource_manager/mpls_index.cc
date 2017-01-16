/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent.h"
#include "cmn/agent_cmn.h"
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "resource_manager/resource_manager.h"
#include "resource_manager/resource_type.h"
#include "resource_manager/resource_cmn.h"
#include "resource_manager/mpls_index.h"
#include "resource_manager/resource_manager_types.h"
#include "resource_manager/resource_backup.h"

MplsIndexResourceKey::MplsIndexResourceKey(ResourceManager *rm,
                                           Type type,
                                           const boost::uuids::uuid &uuid) :
    IndexResourceKey(rm, uuid, Resource::MPLS_INDEX), type_(type) {
}

MplsIndexResourceKey::~MplsIndexResourceKey() {
}

//VM interface
InterfaceIndexResourceKey::InterfaceIndexResourceKey
(ResourceManager *rm, const boost::uuids::uuid &uuid,
 const MacAddress &mac, bool policy, uint32_t id) :
    MplsIndexResourceKey(rm, MplsIndexResourceKey::INTERFACE, uuid), id_(id),
    mac_(mac), policy_(policy) {
}

InterfaceIndexResourceKey::~InterfaceIndexResourceKey() {
}

bool InterfaceIndexResourceKey::IsLess(const ResourceKey &rhs) const {
    const MplsIndexResourceKey *mpls_key = dynamic_cast<const
        MplsIndexResourceKey *>(&rhs);
    if (mpls_key->type_ != type_)
        return mpls_key->type_ < type_;

    const InterfaceIndexResourceKey *vm_key = dynamic_cast<const
        InterfaceIndexResourceKey*>(&rhs);
    if (vm_key->id_ != id_)
        return (vm_key->id_ < id_);

    if (vm_key->mac_ != mac_)
        return vm_key->mac_ < mac_;

    if (vm_key->policy_ != policy_)
        return vm_key->policy_ < policy_;

    return IndexResourceKey::IsLess(rhs);
}

void InterfaceIndexResourceKey::Backup(ResourceData *data, bool del) {
    IndexResourceData *index_data = dynamic_cast<IndexResourceData *>(data);
    InterfaceIndexResource backup_data;
    backup_data.set_type(id_);
    backup_data.set_uuid(UuidToString(uuid_));
    backup_data.set_mac(mac_.ToString());
    backup_data.set_policy(policy_);
    if (del) {
        rm_->backup_mgr()->sandesh_maps().interface_mpls_index_map_.map_.erase
            (index_data->GetIndex());
    } else {
        rm_->backup_mgr()->sandesh_maps().interface_mpls_index_map_.map_.insert
            (std::pair<uint32_t, InterfaceIndexResource>
             (index_data->GetIndex(), backup_data));
    }
    //TODO may be API to insert in map can be added and that will incr sequence
    //number internally.
    rm_->backup_mgr()->sandesh_maps().interface_mpls_index_map_.TriggerBackup();
}

VrfMplsResourceKey::VrfMplsResourceKey(ResourceManager *rm,
                                         const std::string &name) :
    MplsIndexResourceKey(rm, MplsIndexResourceKey::VRF, nil_uuid()), name_(name) {
}

VrfMplsResourceKey::~VrfMplsResourceKey() {
}

bool VrfMplsResourceKey::IsLess(const ResourceKey &rhs) const {
    const MplsIndexResourceKey *mpls_key = dynamic_cast<const
        MplsIndexResourceKey *>(&rhs);
    if (mpls_key->type_ != type_)
        return mpls_key->type_ < type_;

    const VrfMplsResourceKey *vrf_key = dynamic_cast<const
        VrfMplsResourceKey*>(&rhs);
    return (vrf_key->name_ < name_);
}

void VrfMplsResourceKey::Backup(ResourceData *data, bool del) {
    IndexResourceData *index_data = dynamic_cast<IndexResourceData *>(data);
    VrfMplsResource backup_data;
    backup_data.set_name(name_);
    if (del) {
        rm_->backup_mgr()->sandesh_maps().vrf_mpls_index_map_.map_.erase
            (index_data->GetIndex());
    } else {
        rm_->backup_mgr()->sandesh_maps().vrf_mpls_index_map_.map_.insert
            (std::pair<uint32_t, VrfMplsResource>
             (index_data->GetIndex(), backup_data));
    }
    //TODO may be API to insert in map can be added and that will incr sequence
    //number internally.
    rm_->backup_mgr()->sandesh_maps().vrf_mpls_index_map_.TriggerBackup();
}

RouteMplsResourceKey::RouteMplsResourceKey(ResourceManager *rm,
                                             const std::string &vrf_name,
                                             const std::string route_str) :
    MplsIndexResourceKey(rm, MplsIndexResourceKey::ROUTE, nil_uuid()),
    vrf_name_(vrf_name), route_str_(route_str) {
}

RouteMplsResourceKey::~RouteMplsResourceKey() {
}

bool RouteMplsResourceKey::IsLess(const ResourceKey &rhs) const {
    const MplsIndexResourceKey *mpls_key = dynamic_cast<const
        MplsIndexResourceKey *>(&rhs);
    if (mpls_key->type_ != type_)
        return mpls_key->type_ < type_;

    const RouteMplsResourceKey *route_key = dynamic_cast<const
        RouteMplsResourceKey*>(&rhs);
    if (route_key->vrf_name_ != vrf_name_)
        return (route_key->vrf_name_ < vrf_name_);
    return (route_key->route_str_ < route_str_);
}

void RouteMplsResourceKey::Backup(ResourceData *data, bool del) {
    IndexResourceData *index_data = dynamic_cast<IndexResourceData *>(data);
    RouteMplsResource backup_data;
    backup_data.set_vrf_name(vrf_name_);
    backup_data.set_route_prefix(route_str_);
    if (del) {
        rm_->backup_mgr()->sandesh_maps().route_mpls_index_map_.map_.erase
            (index_data->GetIndex());
    } else {
        rm_->backup_mgr()->sandesh_maps().route_mpls_index_map_.map_.insert
            (std::pair<uint32_t, RouteMplsResource>
             (index_data->GetIndex(), backup_data));
    }
    //TODO may be API to insert in map can be added and that will incr sequence
    //number internally.
    rm_->backup_mgr()->sandesh_maps().route_mpls_index_map_.TriggerBackup();
}

TestMplsResourceKey::TestMplsResourceKey(ResourceManager *rm,
                                         const std::string &name) :
    MplsIndexResourceKey(rm, MplsIndexResourceKey::TEST, nil_uuid()), name_(name) {
}

TestMplsResourceKey::~TestMplsResourceKey() {
}

bool TestMplsResourceKey::IsLess(const ResourceKey &rhs) const {
    const MplsIndexResourceKey *mpls_key = dynamic_cast<const
        MplsIndexResourceKey *>(&rhs);
    if (mpls_key->type_ != type_)
        return mpls_key->type_ < type_;

    const TestMplsResourceKey *test_key = dynamic_cast<const
        TestMplsResourceKey*>(&rhs);
    return (test_key->name_ < name_);
}

EdgeMulticastMplsResourceKey::EdgeMulticastMplsResourceKey(ResourceManager *rm,
                                                           uint32_t index) :
    MplsIndexResourceKey(rm, MplsIndexResourceKey::EDGEMCAST, nil_uuid()),
    index_(index) {
}

EdgeMulticastMplsResourceKey::~EdgeMulticastMplsResourceKey() {
}

bool EdgeMulticastMplsResourceKey::IsLess(const ResourceKey &rhs) const {
    const MplsIndexResourceKey *mpls_key = dynamic_cast<const
        MplsIndexResourceKey *>(&rhs);
    if (mpls_key->type_ != type_)
        return mpls_key->type_ < type_;

    const EdgeMulticastMplsResourceKey *key = dynamic_cast<const
        EdgeMulticastMplsResourceKey *>(&rhs);
    return (key->index_ < index_);
}
