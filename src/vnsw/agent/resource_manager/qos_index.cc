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
#include "qos_index.h"
#include "resource_manager/resource_manager_types.h"
#include "resource_manager/resource_backup.h"

QosIndexResourceKey::QosIndexResourceKey(ResourceManager *rm,
        const boost::uuids::uuid &uuid):IndexResourceKey(rm,
        Resource::QOS_INDEX), uuid_(uuid) {

}

void QosIndexResourceKey::Backup(ResourceData *data, uint16_t op){
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    if (op == ResourceBackupReq::DEL) {
        rm()->backup_mgr()->
           sandesh_maps().DeleteQosResourceEntry(index_data->index());
    } else {
        QosIndexResource backup_data;
        backup_data.set_uuid(UuidToString(uuid_));
        backup_data.set_time_stamp(UTCTimestampUsec());
        rm()->backup_mgr()->
            sandesh_maps().AddQosResourceEntry(index_data->index(),
                                                       backup_data);
    }
    rm()->backup_mgr()->sandesh_maps().qos_index_table().
    TriggerBackup();

}
bool QosIndexResourceKey::IsLess(const ResourceKey &rhs) const {
    const QosIndexResourceKey *vm_key =
        static_cast<const QosIndexResourceKey *>(&rhs);
    return uuid_ < vm_key->uuid_;
}
