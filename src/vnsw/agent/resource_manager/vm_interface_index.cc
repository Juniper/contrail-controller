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
        const boost::uuids::uuid &uuid):IndexResourceKey(rm,
        Resource::INTERFACE_INDEX), uuid_(uuid) {
    
}
    
void VmInterfaceIndexResourceKey::Backup(ResourceData *data, uint16_t op){
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    if (op == ResourceBackupReq::DELETE) {
        rm()->backup_mgr()->
           sandesh_maps().DeleteInterfaceMplsResourceEntry(index_data->index());
    } else {
        VmInterfaceIndexResource backup_data;
        backup_data.set_uuid(UuidToString(uuid_));
        backup_data.set_time_stamp(UTCTimestampUsec());
        rm()->backup_mgr()->
            sandesh_maps().AddVmInterfaceResourceEntry(index_data->index(),
                                                       backup_data);
    }
    //TODO may be API to insert in map can be added and that will incr sequence
    //number internally.
    rm()->backup_mgr()->sandesh_maps().vm_interface_index_table().
    TriggerBackup();

}
bool VmInterfaceIndexResourceKey::IsLess(const ResourceKey &rhs) const {
    const VmInterfaceIndexResourceKey *vm_key = 
        static_cast<const VmInterfaceIndexResourceKey *>(&rhs);
    return uuid_ < vm_key->uuid_; 
}
