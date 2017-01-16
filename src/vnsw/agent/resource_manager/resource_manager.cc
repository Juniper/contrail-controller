/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <init/agent_param.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_type.h>
#include <resource_manager/resource_backup.h>
#include <resource_manager/resource_cmn.h>
#include <resource_manager/index_resource.h>

ResourceManager::ResourceManager(Agent *agent) :
    agent_(agent),
    decode_work_queue_(agent->task_scheduler()->
                       GetTaskId("db::DBTable"), 0,
                boost::bind(&ResourceManager::WorkQueueProcess, this, _1)),
    encode_work_queue_(agent->task_scheduler()->
                       GetTaskId("Agent::ResourceBackup"), 0,
                boost::bind(&ResourceManager::WorkQueueProcess, this, _1)) {

    for (uint8_t type = uint8_t(Resource::INVALID + 1);
         type < uint8_t(Resource::MAX);
         type++) {
        resource_type_[type].reset(Resource::Create((Resource::Type)type, this));
    }

    //Backup resource if configured
    if (agent_->params()->restart_backup_enable()) {
        backup_mgr_.reset(new ResourceBackupManager(this));
    } else {
        agent_->SetResourceManagerReady();
    }
}

ResourceManager::~ResourceManager() {
}

void ResourceManager::Init() {
    if (backup_mgr_.get())
        backup_mgr_->Init();
}

void ResourceManager::BackupResource(ResourceManager::KeyPtr key,
                                     ResourceManager::DataPtr data,
                                     bool del) {
    if (backup_mgr_ == NULL) return;
    ResourceBackupDataEncodeType encode_data(new ResourceBackupDataEncode
                                             (key, data, del));
    encode_work_queue_.Enqueue(boost::static_pointer_cast<ResourceBackupData>
                               (encode_data));
}

bool ResourceManager::WorkQueueProcess(ResourceBackupDataType data) {
    ResourceBackupDataEncodeType encode_data =
        boost::dynamic_pointer_cast<ResourceBackupDataEncode>(data);
    if (encode_data) {
        encode_data->Process();
        return true;
    }
    ResourceBackupDataDecodeType decode_data =
        boost::dynamic_pointer_cast<ResourceBackupDataDecode>(data);
    if (decode_data) {
        RestoreResource(decode_data.get()->key(), decode_data.get()->data());
        return true;
    }
    return true;
}

void ResourceManager::EnqueueRestore(ResourceManager::KeyPtr key,
                                     ResourceManager::DataPtr data) {
    ResourceBackupDataDecodeType decode_data(new ResourceBackupDataDecode
                                           (key, data));
    decode_work_queue_.Enqueue(boost::static_pointer_cast<ResourceBackupData>
                               (decode_data));
}

void ResourceManager::RestoreResource(ResourceManager::KeyPtr key,
                                      ResourceManager::DataPtr data) {
    if (dynamic_cast<ResourceBackupEndKey *>(key.get())) {
        agent_->SetResourceManagerReady();
        return;
    }

    ResourceType *resource_type = key.get()->resource_type_; 
    assert(resource_type->FindKey(key) == NULL);
    //If its not used then candidate for deletion so mark dirty
    key.get()->set_dirty();
    resource_type->InsertKey(key, data);
}

void ResourceManager::ReserveIndex(Resource::Type type, uint32_t index) {
    IndexResourceType *table = dynamic_cast<IndexResourceType *>
        (resource_type(type));
    table->ReserveIndex(index);
}

void ResourceManager::ReleaseIndex(Resource::Type type, uint32_t index) {
    IndexResourceType *table = dynamic_cast<IndexResourceType *>
        (resource_type(type));
    table->ReleaseIndex(index);
}

ResourceManager::DataPtr ResourceManager::Allocate(ResourceManager::KeyPtr key) {
    assert(agent_->ResourceManagerReady());
    ResourceType *resource_type = key.get()->resource_type_; 
    ResourceManager::DataPtr data = resource_type->FindKeyPtr(key);
    if (data.get() == NULL) {
        data = resource_type->AllocateData(key);
        resource_type->InsertKey(key, data);
    }

    //Reset dirty bit as key has a user.
    key.get()->reset_dirty();
    BackupResource(key, data, false);
    return data;
}

void ResourceManager::Release(ResourceManager::KeyPtr key) {
    ResourceType *resource_type = key.get()->resource_type_; 
    ResourceManager::DataPtr data = resource_type->FindKeyPtr(key);
    if (data.get() == NULL) return;
    BackupResource(key, data, true);
    resource_type->DeleteKey(key);
}

void ResourceManager::Release(Resource::Type type, uint32_t index) {
    IndexResourceType *table = dynamic_cast<IndexResourceType *>
        (resource_type(type));
    table->Release(index);
}

ResourceType* ResourceManager::resource_type(uint8_t type) {
    return resource_type_[type].get();
}

void ResourceManager::FlushStale() {
    for (uint8_t type = uint8_t(Resource::INVALID + 1);
         type < uint8_t(Resource::MAX);
         type++) {
        resource_type_[type].get()->FlushStale();
    }
}
