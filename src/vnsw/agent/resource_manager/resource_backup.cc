/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include "resource_manager/resource_backup.h"
#include "resource_manager/resource_manager.h"

ResourceBackupData::ResourceBackupData() {
}

ResourceBackupData::~ResourceBackupData() {
}

ResourceBackupDataEncode::ResourceBackupDataEncode(ResourceManager::KeyPtr key,
                                                   ResourceManager::DataPtr data) :
    ResourceBackupData(), key_(key), data_(data) {
        //TODO Encode
        //resource_type - file name(distinguisher) 
        //also it decides which sandesh data type to pickup.
        //All index vector users can use same sandesh data as they are seperated
        //by files.
        //Resource::ResourceBackupFileName(type) - file-name
}

ResourceBackupDataEncode::~ResourceBackupDataEncode() {
}

ResourceBackupDataDecode::ResourceBackupDataDecode() :
    ResourceBackupData() {
    // TODO
    // Using file name derive the type and prepare resource key and data.
    // Enqueue the same for processing
}

ResourceBackupDataDecode::~ResourceBackupDataDecode() {
}
