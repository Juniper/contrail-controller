/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <init/agent_param.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/resource_backup.h>
#include <resource_manager/resource_cmn.h>
#include <resource_manager/index_resource.h>

ResourceManager::ResourceManager(Agent *agent) :
    agent_(agent),
    restore_work_queue_(agent->task_scheduler()->
                       GetTaskId("Agent::ResoureRestore"), 0,
                boost::bind(&ResourceManager::WorkQueueRestoreProcess, this, _1)),
    backup_work_queue_(agent->task_scheduler()->
                       GetTaskId("Agent::ResourceBackup"), 0,
                boost::bind(&ResourceManager::WorkQueueBackUpProcess, this, _1)) {

    for (uint8_t type = uint8_t(Resource::INVALID + 1);
         type < uint8_t(Resource::MAX);
         type++) {
        resource_table_[type].reset(Resource::Create((Resource::Type)type, this));
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

void ResourceManager::BackupResource(KeyPtr key, DataPtr data, bool del) {
    if (backup_mgr_ == NULL) 
        return;
    ResourceBackupReqType backup_data(new ResourceBackupReq
                                             (key, data, del));
    backup_work_queue_.Enqueue(backup_data);
}

bool ResourceManager::WorkQueueRestoreProcess(
        ResourceRestoreReqType restore_data) {
    RestoreResource(restore_data.get()->key(), restore_data.get()->data());
    return true;
}

bool ResourceManager::WorkQueueBackUpProcess(
        ResourceBackupReqType backup_data) {
        backup_data->Process();
        return true;
}

void ResourceManager::EnqueueRestore(KeyPtr key, DataPtr data) {
    ResourceRestoreReqType restore_data(new ResourceRestoreReq(key, data));
    restore_work_queue_.Enqueue(restore_data);
}

void ResourceManager::RestoreResource(KeyPtr key, DataPtr data) {
    if (dynamic_cast<ResourceBackupEndKey *>(key.get())) {
        agent_->SetResourceManagerReady();
        return;
    }

    ResourceTable *resource_table = key.get()->resource_table_; 
    assert(resource_table->FindKey(key) == NULL);
    //If its not used then candidate for deletion so mark dirty
    key.get()->set_dirty();
    resource_table->InsertKey(key, data);
}

void ResourceManager::ReserveIndex(Resource::Type type, uint32_t index) {
    IndexResourceTable *table = dynamic_cast<IndexResourceTable *>
        (resource_table(type));
    table->ReserveIndex(index);
}

void ResourceManager::ReleaseIndex(Resource::Type type, uint32_t index) {
    IndexResourceTable *table = dynamic_cast<IndexResourceTable *>
        (resource_table(type));
    table->ReleaseIndex(index);
}

ResourceManager::DataPtr ResourceManager::Allocate(KeyPtr key) {
    assert(agent_->ResourceManagerReady());
    ResourceManager::DataPtr data = key.get()->resource_table_->Allocate(key);
    BackupResource(key, data, false);
    return data;
}

void ResourceManager::Release(KeyPtr key) {
    ResourceTable *resource_table = key.get()->resource_table_; 
    ResourceManager::DataPtr data = resource_table->FindKeyPtr(key);
    if (data.get() == NULL) return;
    BackupResource(key, data, true);
    resource_table->DeleteKey(key);
}

void ResourceManager::Release(Resource::Type type, uint32_t index) {
    IndexResourceTable *table = dynamic_cast<IndexResourceTable *>
        (resource_table(type));
    table->Release(index);
}

ResourceTable* ResourceManager::resource_table(uint8_t type) {
    return resource_table_[type].get();
}

void ResourceManager::Audit() {
    for (uint8_t type = uint8_t(Resource::INVALID + 1);
         type < uint8_t(Resource::MAX);
         type++) {
        resource_table_[type].get()->FlushStale();
    }
}
