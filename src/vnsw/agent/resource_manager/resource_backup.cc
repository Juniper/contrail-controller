/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <cmn/agent.h>
#include <base/timer.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "resource_manager/resource_backup.h"
#include "resource_manager/resource_table.h"
#include "resource_manager/sandesh_map.h"
#include "resource_manager/resource_manager.h"
#include "resource_manager/resource_manager_types.h"
#include <sys/stat.h>

ResourceBackupManager::ResourceBackupManager(ResourceManager *mgr) :
    resource_manager_(mgr), agent_(mgr->agent()), sandesh_maps_(this),
    backup_work_queue_(agent_->task_scheduler()->
            GetTaskId(kAgentResourceBackUpTask), 0,
            boost::bind(&ResourceBackupManager::WorkQueueBackUpProcess,
            this, _1)) {
}

ResourceBackupManager::~ResourceBackupManager() {
}

void ResourceBackupManager::Init() {
    //Read stored resource file and restore resources
    sandesh_maps_.ReadFromFile();
    sandesh_maps_.RestoreResource();
}

void ResourceBackupManager::BackupResource(ResourceManager::KeyPtr key,
                                           ResourceManager::DataPtr data,
                                           ResourceBackupReq::Op op) {
    ResourceBackupReqPtr backup_data(new ResourceBackupReq (key, data, op));
    backup_work_queue_.Enqueue(backup_data);
}

bool ResourceBackupManager::WorkQueueBackUpProcess(
        ResourceBackupReqPtr backup_data) {
    backup_data->Process();
    return true;
}

uint32_t ResourceBackupManager::ReadResourceDataFromFile
(const std::string &file_name, uint8_t **buf) {
    std::stringstream error_str;
    uint32_t size;
    std::ifstream input(file_name.c_str(), std::ofstream::binary);
    if (!input.good()) {
        error_str << "Resource backup mgr Open failed to read file";
        goto error;
    }
    struct stat st;
    if (stat(file_name.c_str(), &st) == -1) {
        error_str << "Resource backup mgr Get size from file failed";
        goto error;
    }
    size = (uint32_t) st.st_size;
    *buf = new uint8_t [size]();
    input.read((char *)(*buf), size);

    if (!input.good()) {
        error_str << "Resource backup mgr  reading file failed";
        goto error;
    }

    input.close();
    return size;

    error:
    {
        input.close();
        error_str << file_name;
        LOG(ERROR, error_str.str());
        return 0;
    }
}

ResourceSandeshMaps& ResourceBackupManager::sandesh_maps() {
    return sandesh_maps_;
}


ResourceBackupReq::ResourceBackupReq(ResourceManager::KeyPtr key,
                                     ResourceManager::DataPtr data,
                                     Op op) :
    key_(key), data_(data), op_(op) {
}

ResourceBackupReq::~ResourceBackupReq() {
}

void ResourceBackupReq::Process() {
    key_.get()->Backup(data_.get(), op_);
}

ResourceRestoreReq::ResourceRestoreReq(ResourceManager::KeyPtr key,
                             ResourceManager::DataPtr data) :
    key_(key), data_(data) {
}

ResourceRestoreReq::~ResourceRestoreReq() {
}
