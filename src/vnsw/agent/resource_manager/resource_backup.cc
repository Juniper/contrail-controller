/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <cmn/agent.h>
#include <base/timer.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "resource_manager/resource_backup.h"
#include "resource_manager/resource_type.h"
#include "resource_manager/sandesh_map.h"
#include "resource_manager/resource_manager.h"
#include "resource_manager/resource_manager_types.h"
#include <boost/filesystem.hpp>

ResourceBackupManager::ResourceBackupManager(ResourceManager *mgr) :
    resource_manager_(mgr), agent_(mgr->agent()), sandesh_maps_(this) {
}

ResourceBackupManager::~ResourceBackupManager() {
}

void ResourceBackupManager::Init() {
    //Read stored resource file and restore resources
    ReadFromFile();
    sandesh_maps_.RestoreResource();
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
        LOG(ERROR, "Resource backup mgr write to file failed" << file_tmp_name);
        return; 
    }

    output.write((char *)buffer, size);
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
        LOG(ERROR, "Resource backup mgr read from file failed" << file_name);
        return 0;
    }

    input.seekg(0, input.end);
    std::streampos size = input.tellg();
    input.seekg(0, input.beg);
    *buf = new uint8_t [size];
    input.read((char *)(*buf), size);
    input.close();
    return (uint32_t)size;
}

void ResourceBackupManager::Audit() {
    agent_->resource_manager()->FlushStale();
}

ResourceSandeshMaps& ResourceBackupManager::sandesh_maps() {
    return sandesh_maps_;
}

ResourceBackupData::ResourceBackupData() {
}

ResourceBackupData::~ResourceBackupData() {
}

ResourceBackupDataEncode::ResourceBackupDataEncode(ResourceManager::KeyPtr key,
                                                   ResourceManager::DataPtr data,
                                                   bool del) :
    ResourceBackupData(), key_(key), data_(data), del_(del) {
}

ResourceBackupDataEncode::~ResourceBackupDataEncode() {
}

void ResourceBackupDataEncode::Process() {
    key_.get()->Backup(data_.get(), del_);
}

ResourceBackupDataDecode::ResourceBackupDataDecode(ResourceManager::KeyPtr key,
                             ResourceManager::DataPtr data) :
    ResourceBackupData(), key_(key), data_(data) {
}

ResourceBackupDataDecode::~ResourceBackupDataDecode() {
}
