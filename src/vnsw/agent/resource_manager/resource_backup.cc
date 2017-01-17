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
#include <boost/filesystem.hpp>
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

static bool RenameFile(const std::string &file_tmp_name,
                     const std::string &file_name) {
    boost::system::error_code ec;
    boost::filesystem::path tmp_file_path(file_tmp_name.c_str());
    boost::filesystem::path backup_file_path(file_name.c_str());
    boost::filesystem::rename(tmp_file_path, backup_file_path, ec);
    if (!ec) {
        LOG(ERROR, "Resource backup mgr Rename file failed" << ec);
        return false;
    }
    return true;
}

bool ResourceBackupManager::SaveResourceDataToFile(const std::string &file_name,
                                                   const uint8_t *buffer,
                                                   uint32_t size) {
    std::ofstream output;
    std::stringstream tmp_name_str;
    std::stringstream error_str;
    tmp_name_str << file_name << ".tmp";
    std::string file_tmp_name = tmp_name_str.str();

    output.open(file_tmp_name.c_str(),
                std::ofstream::binary | std::ofstream::trunc);
    if (!output.good()) {
        error_str << "Resource backup mgr File open failed for write";
        goto error;
    }

    output.write((char *)buffer, size);
    if (!output.good()) {
        error_str << "Resource backup mgr write to file failed";
        goto error;
    }
    output.close();
    // Rename the tmp File to backup
    if (RenameFile(file_tmp_name, file_name))
        return true;

    error:
    {
        output.close();
        error_str << file_name;
        LOG(ERROR, error_str.str());
        return false;
    }
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
    *buf = new uint8_t [size];
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
