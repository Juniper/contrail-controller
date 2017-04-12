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
#include <oper/nexthop.h>

SandeshTraceBufferPtr InterfaceMplsDataTraceBuf(SandeshTraceBufferCreate
                                               ("InterfaceMplsData", 5000));
SandeshTraceBufferPtr VrfMplsDataTraceBuf(SandeshTraceBufferCreate
                                          ("VrfMplsData", 5000));
SandeshTraceBufferPtr VlanMplsDataTraceBuf(SandeshTraceBufferCreate
                                           ("VlanMplsData", 5000));
SandeshTraceBufferPtr RouteMplsDataTraceBuf(SandeshTraceBufferCreate
                                            ("RouteMplsData", 5000));

MplsIndexResourceKey::MplsIndexResourceKey(ResourceManager *rm,
                                           Type type) :
    IndexResourceKey(rm, Resource::MPLS_INDEX), type_(type) {
}

MplsIndexResourceKey::~MplsIndexResourceKey() {
}

// NextHop resource key
NexthopIndexResourceKey::NexthopIndexResourceKey(ResourceManager *rm,
                                                 NextHopKey *nh_key) :
    MplsIndexResourceKey(rm, MplsIndexResourceKey::NEXTHOP) {
    nh_key_.reset(nh_key);
}

NexthopIndexResourceKey::~NexthopIndexResourceKey() {
}

bool NexthopIndexResourceKey::IsLess(const ResourceKey &rhs) const {
    const MplsIndexResourceKey *mpls_key = static_cast<const
        MplsIndexResourceKey *>(&rhs);
    // Return if index resource key types are different
    if (mpls_key->type() != type())
        return mpls_key->type() < type();

    const NexthopIndexResourceKey *nh_rkey = static_cast<const
        NexthopIndexResourceKey *>(&rhs);
    const NextHopKey *nh_key1 = GetNhKey();
    const NextHopKey *nh_key2 = nh_rkey->GetNhKey();
    return nh_key1->IsLess(*nh_key2);
}

//Backup the Nexthop Resource Data
void NexthopIndexResourceKey::Backup(ResourceData *data, uint16_t op) {
    const NextHopKey *nh_key = GetNhKey();
    switch(nh_key->GetType()) {
    case NextHop::INTERFACE:
        BackupInterfaceResource(data, op);
        break;
    case NextHop::VLAN:
        BackupVlanResource(data, op);
        break;
    case NextHop::VRF:
        BackupVrfResource(data, op);
        break;
    default:
        assert(0);
    }
}

//Backup Interface resource data as sandesh encoded format
void NexthopIndexResourceKey::BackupInterfaceResource(ResourceData *data,
                                                      uint16_t op) {
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    const InterfaceNHKey *itfnh_key = static_cast<const InterfaceNHKey *>(
                                      GetNhKey());
    string operation;
    if (op == ResourceBackupReq::DELETE) {
        rm()->backup_mgr()->
           sandesh_maps().DeleteInterfaceMplsResourceEntry(index_data->index());
        operation = "DELETE";
    } else {
        InterfaceIndexResource backup_data;
        backup_data.set_type(
            (itfnh_key->intf_type() == Interface::VM_INTERFACE)?"vmi":"inet");
        backup_data.set_uuid(UuidToString(itfnh_key->GetUuid()));
        backup_data.set_name(itfnh_key->name());
        backup_data.set_policy(itfnh_key->GetPolicy());
        backup_data.set_flags(itfnh_key->flags());
        backup_data.set_mac(itfnh_key->dmac().ToString());
        backup_data.set_time_stamp(UTCTimestampUsec());
        rm()->backup_mgr()->
            sandesh_maps().AddInterfaceMplsResourceEntry(index_data->index(),
                                                         backup_data);
        operation = "ADD";
    }

    INTERFACE_MPLS_DATA_TRACE(InterfaceMplsDataTraceBuf,
                              (itfnh_key->intf_type() == Interface::VM_INTERFACE)?"vmi":"inet",
                              UuidToString(itfnh_key->GetUuid()),
                              itfnh_key->name(), itfnh_key->GetPolicy(),
                              itfnh_key->dmac().ToString(), index_data->index(),
                              operation);
    //TODO may be API to insert in map can be added and that will incr sequence
    //number internally.
    rm()->backup_mgr()->sandesh_maps().interface_mpls_index_table().
        TriggerBackup();
}

//Backup Vrf resource data as sandesh encoded format
void NexthopIndexResourceKey::BackupVrfResource(ResourceData *data,
                                                uint16_t op) {
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    const VrfNHKey *vrfnh_key = static_cast<const VrfNHKey *>(GetNhKey());
    string operation;
    if (op == ResourceBackupReq::DELETE) {
        rm()->backup_mgr()->sandesh_maps().DeleteVrfMplsResourceEntry(
                index_data->index());
        operation = "DELETE";
    } else {
        VrfMplsResource backup_data;
        backup_data.set_name(vrfnh_key->GetVrfName());
        backup_data.set_vxlan_nh(vrfnh_key->GetVxlanNh());
        backup_data.set_time_stamp(UTCTimestampUsec());
        rm()->backup_mgr()->sandesh_maps().AddVrfMplsResourceEntry(
                index_data->index(), backup_data);
        operation = "ADD";
    }
    //TODO may be API to insert in map can be added and that will incr sequence
    //number internally.
    VRF_MPLS_DATA_TRACE(VrfMplsDataTraceBuf, vrfnh_key->GetVrfName(),
                        vrfnh_key->GetVxlanNh(), index_data->index(), operation);
    rm()->backup_mgr()->sandesh_maps().vrf_mpls_index_table().
        TriggerBackup();
}

//Backup Vlan resource data as sandesh encoded format
void NexthopIndexResourceKey::BackupVlanResource(ResourceData *data,
                                                 uint16_t op) {
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    const VlanNHKey *vlan_nh_key = static_cast<const VlanNHKey *>(GetNhKey());
    string operation;
    if (op == ResourceBackupReq::DELETE) {
        rm()->backup_mgr()->sandesh_maps().DeleteVlanMplsResourceEntry(
                index_data->index());
        operation = "DELETE";
    } else {
        operation = "ADD";
        VlanMplsResource backup_data;
        backup_data.set_uuid(UuidToString(vlan_nh_key->GetUuid()));
        backup_data.set_tag(vlan_nh_key->vlan_tag());
        backup_data.set_time_stamp(UTCTimestampUsec());
        rm()->backup_mgr()->sandesh_maps().AddVlanMplsResourceEntry(
                index_data->index(), backup_data);
    }
    //TODO may be API to insert in map can be added and that will incr sequence
    //number internally.
    VLAN_MPLS_DATA_TRACE(VlanMplsDataTraceBuf,
                        UuidToString(vlan_nh_key->GetUuid()),
                        vlan_nh_key->vlan_tag(), index_data->index(), operation);
    rm()->backup_mgr()->sandesh_maps().vlan_mpls_index_table().
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
    if (mpls_key->type() != type())
        return mpls_key->type() < type();

    const RouteMplsResourceKey *route_key = static_cast<const
        RouteMplsResourceKey*>(&rhs);
    if (route_key->vrf_name_ != vrf_name_)
        return (route_key->vrf_name_ < vrf_name_);
    return (route_key->route_key_ < route_key_);
}

//Backup the Route Mpls Resource Data as Sandesh encoded format.
void RouteMplsResourceKey::Backup(ResourceData *data, uint16_t op) {
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    string operation;
    if (op == ResourceBackupReq::DELETE) {
        rm()->backup_mgr()->sandesh_maps().DeleteRouteMplsResourceEntry(
                index_data->index());
        operation = "DELETE";
    } else {
        RouteMplsResource backup_data;
        backup_data.set_vrf_name(vrf_name_);
        backup_data.set_route_prefix(route_key_);
        backup_data.set_time_stamp(UTCTimestampUsec());
        rm()->backup_mgr()->sandesh_maps().AddRouteMplsResourceEntry(
                index_data->index(), backup_data);
        operation = "ADD";
    }
    //TODO may be API to insert in map can be added and that will incr sequence
    //number internally.
    ROUTE_MPLS_DATA_TRACE(RouteMplsDataTraceBuf, vrf_name_, route_key_,
                          index_data->index(), operation);
    rm()->backup_mgr()->sandesh_maps().route_mpls_index_table().
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
    if (mpls_key->type() != type())
        return mpls_key->type() < type();

    const TestMplsResourceKey *test_key = static_cast<const
        TestMplsResourceKey*>(&rhs);
    return (test_key->name_ < name_);
}
