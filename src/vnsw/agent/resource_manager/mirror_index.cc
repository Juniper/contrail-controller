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
#include "mirror_index.h"
#include "resource_manager/resource_manager_types.h"
#include "resource_manager/resource_backup.h"

MirrorIndexResourceKey::MirrorIndexResourceKey (ResourceManager *rm,
    const string& analyzer_name):IndexResourceKey(rm, Resource::MIRROR_INDEX),
    analyzer_name_(analyzer_name) {

}

void MirrorIndexResourceKey::Backup(ResourceData *data, uint16_t op){
    IndexResourceData *index_data = static_cast<IndexResourceData *>(data);
    if (op == ResourceBackupReq::DEL) {
        rm()->backup_mgr()->
           sandesh_maps().DeleteMirrorResourceEntry(index_data->index());
    } else {
        MirrorIndexResource backup_data;
        backup_data.set_analyzer_name(analyzer_name_);
        backup_data.set_time_stamp(UTCTimestampUsec());
        rm()->backup_mgr()->
            sandesh_maps().AddMirrorResourceEntry(index_data->index(),
                                                         backup_data);
    }
    rm()->backup_mgr()->sandesh_maps().mirror_index_table().TriggerBackup();

}

bool MirrorIndexResourceKey::IsLess(const ResourceKey &rhs) const {
    const MirrorIndexResourceKey * key =
        static_cast<const MirrorIndexResourceKey*> (&rhs);
    return analyzer_name_ < key->analyzer_name_;
}
