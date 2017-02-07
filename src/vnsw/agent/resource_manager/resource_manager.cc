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
            GetTaskId(kAgentResourceRestoreTask), 0,
            boost::bind(&ResourceManager::WorkQueueRestoreProcess,
            this, _1)) {

    for (uint8_t type = uint8_t(Resource::INVALID + 1);
         type < uint8_t(Resource::MAX);
         type++) {
        resource_table_[type].reset(Resource::Create((Resource::Type)type,
                                                      this));
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
// Read the Data from Resore Queue and process it.
bool ResourceManager::WorkQueueRestoreProcess(
        ResourceRestoreReqPtr restore_data) {
    RestoreResource(restore_data.get()->key(), restore_data.get()->data());
    return true;
}


void ResourceManager::EnqueueRestore(KeyPtr key, DataPtr data) {
    ResourceRestoreReqPtr restore_data(new ResourceRestoreReq(key, data));
    restore_work_queue_.Enqueue(restore_data);
}

// Check for ResouceBackupEndkey if it is set mark it resource manager ready.
// Mark the resource key diry until we process the audit for validity of key.
void ResourceManager::RestoreResource(KeyPtr key, DataPtr data) {
    if (dynamic_cast<ResourceBackupEndKey *>(key.get())) {
        agent_->SetResourceManagerReady();
        return;
    }

    ResourceTable *resource_table = key->resource_table();
    assert(resource_table->FindKey(key) == NULL);
    //If its not used then candidate for deletion so mark dirty
    key->set_dirty();
    resource_table->RestoreKey(key, data);
}

// Reserve the index for certain resource types.
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

// Allocates the the index by resource table.
// if back up is enabled it will store the data to file.
ResourceManager::DataPtr ResourceManager::Allocate(KeyPtr key) {
    assert(agent_->ResourceManagerReady());
    ResourceManager::DataPtr data = key.get()->resource_table()->Allocate(key);
    if (backup_mgr_.get()) {
        backup_mgr_->BackupResource(key, data, ResourceBackupReq::ADD);
    }
    return data;
}

// Releases the resource table index and  key.
// Delete the data from file.
void ResourceManager::Release(KeyPtr key) {
    ResourceTable *resource_table = key.get()->resource_table(); 
    ResourceManager::DataPtr data = resource_table->FindKeyPtr(key);
    if (data.get() == NULL) {
        return;
    }

    if (backup_mgr_.get()) {
        backup_mgr_->BackupResource(key, data, ResourceBackupReq::DELETE);
    }
    resource_table->ReleaseKey(key, data);
}

// Releases the reserved index.
void ResourceManager::Release(Resource::Type type, uint32_t index) {
    IndexResourceTable *table = dynamic_cast<IndexResourceTable *>
        (resource_table(type));
    table->Release(index);
}

ResourceTable* ResourceManager::resource_table(uint8_t type) {
    return resource_table_[type].get();
}

// Audit the records per resource table.
bool ResourceManager::Audit() {
    for (uint8_t type = uint8_t(Resource::INVALID + 1);
         type < uint8_t(Resource::MAX);
         type++) {
        resource_table_[type].get()->FlushStale();
    }
    backup_mgr_->AuditDone();
    return true;
}
