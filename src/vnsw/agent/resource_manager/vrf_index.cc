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
#include "vrf_index.h"
#include "resource_manager/resource_manager_types.h"
#include "resource_manager/resource_backup.h"

VrfIndexResourceKey::VrfIndexResourceKey (ResourceManager *rm,
        const string& vrf_name):IndexResourceKey(rm, Resource::VRF_INDEX),
        vrf_name_(vrf_name) {

}

void VrfIndexResourceKey::Backup(ResourceData *data, uint16_t op){
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    if (op == ResourceBackupReq::DEL) {
        rm()->backup_mgr()->
           sandesh_maps().DeleteVrfResourceEntry(index_data->index());
    } else {
        VrfIndexResource backup_data;
        backup_data.set_vrf_name(vrf_name_);
        backup_data.set_time_stamp(UTCTimestampUsec());
        rm()->backup_mgr()->
            sandesh_maps().AddVrfResourceEntry(index_data->index(),
                                                         backup_data);
    }
    rm()->backup_mgr()->sandesh_maps().vrf_index_table().TriggerBackup();

}

bool VrfIndexResourceKey::IsLess(const ResourceKey &rhs) const {
    const VrfIndexResourceKey * key = static_cast<const VrfIndexResourceKey*> (&rhs);
    return vrf_name_ < key->vrf_name_;
}
