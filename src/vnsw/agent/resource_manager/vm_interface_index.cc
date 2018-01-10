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
#include "vm_interface_index.h"
#include "resource_manager/resource_manager_types.h"
#include "resource_manager/resource_backup.h"

VmInterfaceIndexResourceKey::VmInterfaceIndexResourceKey(ResourceManager *rm,
        const boost::uuids::uuid &uuid, const std::string &interface_name):
        IndexResourceKey(rm, Resource::INTERFACE_INDEX), uuid_(uuid),
        interface_name_(interface_name) {

}

void VmInterfaceIndexResourceKey::Backup(ResourceData *data, uint16_t op){
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    if (op == ResourceBackupReq::DEL) {
        rm()->backup_mgr()->
           sandesh_maps().DeleteVmInterfaceResourceEntry(index_data->index());
    } else {
        VmInterfaceIndexResource backup_data;
        backup_data.set_uuid(UuidToString(uuid_));
        backup_data.set_interface_name(interface_name_);
        backup_data.set_time_stamp(UTCTimestampUsec());
        rm()->backup_mgr()->
            sandesh_maps().AddVmInterfaceResourceEntry(index_data->index(),
                                                       backup_data);
    }
    rm()->backup_mgr()->sandesh_maps().vm_interface_index_table().
    TriggerBackup();

}
bool VmInterfaceIndexResourceKey::IsLess(const ResourceKey &rhs) const {
    const VmInterfaceIndexResourceKey *vm_key =
        static_cast<const VmInterfaceIndexResourceKey *>(&rhs);
    if (uuid_ != vm_key->uuid_)
        return uuid_ < vm_key->uuid_;
    return interface_name_ < vm_key->interface_name_;
}
