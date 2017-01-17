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
    resource_manager_(mgr), agent_(mgr->agent()), sandesh_maps_(this) {
    //Read stored resource file and restore resources
}
void ResourceBackupManager::Init() {
    //Read stored resource file and restore resources
    ReadFromFile();
    sandesh_maps_.RestoreResource();
}
ResourceBackupManager::~ResourceBackupManager() {
}

void ResourceBackupManager::ReadFromFile() {
    sandesh_maps_.ReadFromFile();
}

void ResourceBackupManager::SaveResourceDataToFile(const std::string &file_name,
                                                   const uint8_t *buffer,
                                                   uint32_t size) {
    std::ofstream output;
    std::stringstream tmp_name_str;
    tmp_name_str << file_name << ".tmp";
    std::string file_tmp_name = tmp_name_str.str();

    output.open(file_tmp_name.c_str(),
                std::ofstream::binary | std::ofstream::trunc);
    if (!output.good()) {
        output.close();
        LOG(ERROR, "Resource backup mgr File open failed for write" <<
            file_tmp_name);
        return; 
    }

    output.write((char *)buffer, size);
    if (!output.good()) {
        output.close();
        LOG(ERROR, "Resource backup mgr write to file failed" << file_tmp_name);
        return; 
    }
    output.flush();
    output.close();
    // Copy the tmp File to backup
    boost::system::error_code ec;
    boost::filesystem::path tmp_file_path(file_tmp_name.c_str());
    boost::filesystem::path backup_file_path(file_name.c_str());
    boost::filesystem::rename(tmp_file_path, backup_file_path, ec);
    if (!ec) {
        LOG(ERROR, "Resource backup mgr copy file failed" << ec);
    }
}

uint32_t ResourceBackupManager::ReadResourceDataFromFile
(const std::string &file_name, uint8_t **buf) {
    std::ifstream input(file_name.c_str(), std::ofstream::binary);
    if (!input.good()) {
        input.close();
        LOG(ERROR, "Resource backup mgr Open failed to read file" << file_name);
        return 0;
    }
    struct stat st;
    if (stat(file_name.c_str(), &st) == -1) {
        input.close();
        LOG(ERROR, "Resource backup mgr Get size from file failed" << file_name);
        return 0;
    } 
    uint32_t size = (uint32_t) st.st_size;
    *buf = new uint8_t [size];
    input.read((char *)(*buf), size);

    if (!input.good()) {
        input.close();
        LOG(ERROR, "Resource backup mgr  reading file failed" << file_name);
        return 0;
    }
    input.close();
    return size;
}

void ResourceBackupManager::Audit() {
    agent_->resource_manager()->FlushStale();
}

ResourceSandeshMaps& ResourceBackupManager::sandesh_maps() {
    return sandesh_maps_;
}


ResourceBackupReq::ResourceBackupReq(ResourceManager::KeyPtr key,
                                                   ResourceManager::DataPtr data,
                                                   bool del) :
    key_(key), data_(data), del_(del) {
}

ResourceBackupReq::~ResourceBackupReq() {
}

void ResourceBackupReq::Process() {
    key_.get()->Backup(data_.get(), del_);
}

ResourceRestoreReq::ResourceRestoreReq(ResourceManager::KeyPtr key,
                             ResourceManager::DataPtr data) :
    key_(key), data_(data) {
}

ResourceRestoreReq::~ResourceRestoreReq() {
}
