/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_type.h>
#include <resource_manager/resource.h>

ResourceManager::ResourceManager(const Agent *agent) :
    agent_(),
    decode_work_queue_(agent->task_scheduler()->
                       GetTaskId("db::DBTable"), 0,
                boost::bind(&ResourceManager::WorkQueueProcess, this, _1)),
    encode_work_queue_(agent->task_scheduler()->
                       GetTaskId("Agent::ResourceBackup"), 0,
                boost::bind(&ResourceManager::WorkQueueProcess, this, _1)) {

    for (uint8_t type = uint8_t(Resource::INVALID + 1);
         type < uint8_t(Resource::MAX);
         type++) {
        resource_type_[type] = Resource::Create(type);
    }
}

~ResourceManager() {
}

bool ResourceManager::BackupResource(ResourceManager::KeyPtr key,
                                     ResourceManager::DataPtr data) {
    ResourceBackupDataEncodeType data(new ResourceBackupDataEncode(key, data));
    encode_work_queue_.Enqueue(boost::static_pointer_cast(data));
}

bool ResourceManager::RestoreResource() {
    //TODO fill arguments
    ResourceBackupDataDecodeType data(new ResourceBackupDataDecode());
    decode_work_queue_.Enqueue(data);
}

void ResourceManager::WorkQueueProcess(ResourceBackupDataType data) {
    ResourceBackupDataEncodeType encode_data =
        boost::dynamic_pointer_cast<ResourceBackupDataEncodeType>(data);
    if (encode_data) {
        //TODO Process encode
        return;
    }
    ResourceBackupDataDecodeType decode_data =
        boost::dynamic_pointer_cast<ResourceBackupDataDecodeType>(data);
    if (decode_data_) {
        ResourceManager::KeyPtr key = data.key_;
        ResourceManager::DataPtr data = data.data_;
        return;
    }
}

ResourceManager::DataPtr ResourceManager::Restore(ResourceManager::KeyPtr key,
                                                  ResourceManager::DataPtr data) {
    ResourceType *resource_type = key.get()->resource_type_; 
    assert(resource_type->FindKey(key) == NULL);
    resource_type->InsertKey(key, data);
}

ResourceManager::DataPtr ResourceManager::Allocate(ResourceManager::KeyPtr key) {
    ResourceType *resource_type = key.get()->resource_type_; 
    ResourceManager::DataPtr data = resource_type->FindKey(key);
    if (data.get() != NULL) return data;

    data = resource_type->AllocateData(key);
    resource_type->InsertKey(key, data);
    return data;
}

void ResourceManager::Release(ResourceManager::KeyPtr key) {
    ResourceType *resource_type = key.get()->resource_type_; 
    ResourceManager::DataPtr data = resource_type->FindKey(key);
    if (data.get() == NULL) return;

    resource_type->DeleteKey(key);
}

ResourceType* ResourceManager::resource_type(uint8_t type) {
    return rm->resource_type_[type];
}

void ResourceManager::WriteFile() {
}

void ResourceManager::ReadFile() {
}
