/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent.h"
#include "cmn/agent_cmn.h"
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "resource_manager/resource_manager.h"
#include "resource_manager/resource_table.h"
#include "resource_manager/resource_cmn.h"
#include "resource_manager/mpls_index.h"
#include "resource_manager/resource_manager_types.h"
#include "resource_manager/resource_backup.h"
#include "base/time_util.h"

MplsIndexResourceKey::MplsIndexResourceKey(ResourceManager *rm,
                                           Type type) :
    IndexResourceKey(rm, Resource::MPLS_INDEX), type_(type) {
}

MplsIndexResourceKey::~MplsIndexResourceKey() {
}

//VM interface
InterfaceIndexResourceKey::InterfaceIndexResourceKey
(ResourceManager *rm, const boost::uuids::uuid &uuid,
 const MacAddress &mac, bool policy, uint32_t label_type) :
    MplsIndexResourceKey(rm, MplsIndexResourceKey::INTERFACE),
    label_type_(label_type), mac_(mac), policy_(policy), uuid_(uuid) {
}

InterfaceIndexResourceKey::~InterfaceIndexResourceKey() {
}

bool InterfaceIndexResourceKey::IsLess(const ResourceKey &rhs) const {
    const MplsIndexResourceKey *mpls_key = static_cast<const
        MplsIndexResourceKey *>(&rhs);
    if (mpls_key->type_ != type_)
        return mpls_key->type_ < type_;

    const InterfaceIndexResourceKey *vm_key = static_cast<const
        InterfaceIndexResourceKey*>(&rhs);
    if (vm_key->label_type_ != label_type_)
        return (vm_key->label_type_ < label_type_);

    if (vm_key->mac_ != mac_)
        return vm_key->mac_ < mac_;

    if (vm_key->policy_ != policy_)
        return vm_key->policy_ < policy_;
    
    return vm_key->uuid_ < uuid_;
}

void InterfaceIndexResourceKey::Backup(ResourceData *data, uint16_t op) {
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    if (op == ResourceBackupReq::DELETE) {
        rm_->backup_mgr()->
           sandesh_maps().DeleteInterfaceMplsResourceEntry(index_data->Index());
    } else {
        InterfaceIndexResource backup_data;
        backup_data.set_type(label_type_);
        backup_data.set_uuid(UuidToString(uuid_));
        backup_data.set_mac(mac_.ToString());
        backup_data.set_policy(policy_);
        backup_data.set_time_stamp(UTCTimestampUsec());
        rm_->backup_mgr()->
            sandesh_maps().AddInterfaceMplsResourceEntry(index_data->Index(),
                                                         backup_data);
    }
    //TODO may be API to insert in map can be added and that will incr sequence
    //number internally.
    rm_->backup_mgr()->sandesh_maps().InterfaceMplsResourceTable().
        TriggerBackup();
}

VrfMplsResourceKey::VrfMplsResourceKey(ResourceManager *rm,
                                         const std::string &name) :
    MplsIndexResourceKey(rm, MplsIndexResourceKey::VRF), name_(name) {
}

VrfMplsResourceKey::~VrfMplsResourceKey() {
}

bool VrfMplsResourceKey::IsLess(const ResourceKey &rhs) const {
    const MplsIndexResourceKey *mpls_key = static_cast<const
        MplsIndexResourceKey *>(&rhs);
    if (mpls_key->type_ != type_)
        return mpls_key->type_ < type_;

    const VrfMplsResourceKey *vrf_key = static_cast<const
        VrfMplsResourceKey*>(&rhs);
    return (vrf_key->name_ < name_);
}

void VrfMplsResourceKey::Backup(ResourceData *data, uint16_t op) {
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    if (op == ResourceBackupReq::DELETE) {
        rm_->backup_mgr()->sandesh_maps().DeleteVrfMplsResourceEntry(
                index_data->Index());
    } else {
        VrfMplsResource backup_data;
        backup_data.set_name(name_);
        backup_data.set_time_stamp(UTCTimestampUsec());
        rm_->backup_mgr()->sandesh_maps().AddVrfMplsResourceEntry(
                index_data->Index(), backup_data);
    }
    //TODO may be API to insert in map can be added and that will incr sequence
    //number internally.
    rm_->backup_mgr()->sandesh_maps().VrfMplsResourceTable().
        TriggerBackup();
}

RouteMplsResourceKey::RouteMplsResourceKey(ResourceManager *rm,
                                             const std::string &vrf_name,
                                             const std::string route_key) :
    MplsIndexResourceKey(rm, MplsIndexResourceKey::ROUTE),
    vrf_name_(vrf_name), route_key_(route_key) {
}

RouteMplsResourceKey::~RouteMplsResourceKey() {
}

bool RouteMplsResourceKey::IsLess(const ResourceKey &rhs) const {
    const MplsIndexResourceKey *mpls_key = static_cast<const
        MplsIndexResourceKey *>(&rhs);
    if (mpls_key->type_ != type_)
        return mpls_key->type_ < type_;

    const RouteMplsResourceKey *route_key = static_cast<const
        RouteMplsResourceKey*>(&rhs);
    if (route_key->vrf_name_ != vrf_name_)
        return (route_key->vrf_name_ < vrf_name_);
    return (route_key->route_key_ < route_key_);
}

void RouteMplsResourceKey::Backup(ResourceData *data, uint16_t op) {
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    if (op == ResourceBackupReq::DELETE) {
        rm_->backup_mgr()->sandesh_maps().DeleteRouteMplsResourceEntry(
                index_data->Index());
    } else {
        RouteMplsResource backup_data;
        backup_data.set_vrf_name(vrf_name_);
        backup_data.set_route_prefix(route_key_);
        backup_data.set_time_stamp(UTCTimestampUsec());
        rm_->backup_mgr()->sandesh_maps().AddRouteMplsResourceEntry(
                index_data->Index(), backup_data);
    }
    //TODO may be API to insert in map can be added and that will incr sequence
    //number internally.
    rm_->backup_mgr()->sandesh_maps().RouteMplsResourceTable().
        TriggerBackup();
}

TestMplsResourceKey::TestMplsResourceKey(ResourceManager *rm,
                                         const std::string &name) :
    MplsIndexResourceKey(rm, MplsIndexResourceKey::TEST), name_(name) {
}

TestMplsResourceKey::~TestMplsResourceKey() {
}

bool TestMplsResourceKey::IsLess(const ResourceKey &rhs) const {
    const MplsIndexResourceKey *mpls_key = static_cast<const
        MplsIndexResourceKey *>(&rhs);
    if (mpls_key->type_ != type_)
        return mpls_key->type_ < type_;

    const TestMplsResourceKey *test_key = static_cast<const
        TestMplsResourceKey*>(&rhs);
    return (test_key->name_ < name_);
}

EdgeMulticastMplsResourceKey::EdgeMulticastMplsResourceKey(ResourceManager *rm,
                                                           uint32_t index) :
    MplsIndexResourceKey(rm, MplsIndexResourceKey::EDGEMCAST),
    index_(index) {
}

EdgeMulticastMplsResourceKey::~EdgeMulticastMplsResourceKey() {
}

bool EdgeMulticastMplsResourceKey::IsLess(const ResourceKey &rhs) const {
    const MplsIndexResourceKey *mpls_key = static_cast<const
        MplsIndexResourceKey *>(&rhs);
    if (mpls_key->type_ != type_)
        return mpls_key->type_ < type_;

    const EdgeMulticastMplsResourceKey *key = static_cast<const
        EdgeMulticastMplsResourceKey *>(&rhs);
    return (key->index_ < index_);
}
