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
#include "bgp_as_service_index.h"
#include "resource_manager/resource_manager_types.h"
#include "resource_manager/resource_backup.h"

BgpAsServiceIndexResourceKey::BgpAsServiceIndexResourceKey(ResourceManager *rm,
        const boost::uuids::uuid &uuid):IndexResourceKey(rm,
        Resource::BGP_AS_SERVICE_INDEX), uuid_(uuid) {

}

void BgpAsServiceIndexResourceKey::Backup(ResourceData *data, uint16_t op){
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    if (op == ResourceBackupReq::DEL) {
        rm()->backup_mgr()->
           sandesh_maps().DeleteBgpAsServiceResourceEntry(index_data->index());
    } else {
        BgpAsServiceIndexResource backup_data;
        backup_data.set_uuid(UuidToString(uuid_));
        backup_data.set_time_stamp(UTCTimestampUsec());
        rm()->backup_mgr()->
            sandesh_maps().AddBgpAsServiceResourceEntry(index_data->index(),
                                                       backup_data);
    }
    rm()->backup_mgr()->sandesh_maps().bgp_as_service_index_table().
    TriggerBackup();

}
bool BgpAsServiceIndexResourceKey::IsLess(const ResourceKey &rhs) const {
    const BgpAsServiceIndexResourceKey *vm_key =
        static_cast<const BgpAsServiceIndexResourceKey *>(&rhs);
    return uuid_ < vm_key->uuid_;
}
