/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <cmn/agent.h>
#include "init/agent_param.h"
#include <base/timer.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "resource_manager/resource_backup.h"
#include "resource_manager/resource_table.h"
#include "resource_manager/sandesh_map.h"
#include "resource_manager/resource_manager.h"
#include "resource_manager/mpls_index.h"
#include <boost/filesystem.hpp>
#include <openssl/md5.h>
#include <dirent.h>

BackUpResourceTable::BackUpResourceTable(ResourceBackupManager *manager,
                                         const std::string &name) :
    backup_manager_(manager), agent_(manager->agent()), name_(name),
    last_modified_time_(UTCTimestampUsec()) {
    backup_dir_ = agent_->params()->restart_backup_dir();
    backup_idle_timeout_ = agent_->params()
        ->restart_backup_idle_timeout();
    boost::filesystem::path dir(backup_dir_.c_str());
    if (!boost::filesystem::exists(dir))
        boost::filesystem::create_directory(dir);
    timer_ = TimerManager::CreateTimer(*(agent_->event_manager()->io_service()),
                                       name,
                                       agent_->task_scheduler()->
                                       GetTaskId(kAgentResourceBackUpTask));
}

BackUpResourceTable::~BackUpResourceTable() {
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
}

bool BackUpResourceTable::TimerExpiry() {
    // Check for Update required otherwise wait for fallback time.
    if (UpdateRequired() || fall_back_count_ >= kFallBackCount) {
        if (WriteToFile()) {
            last_modified_time_ = UTCTimestampUsec();
            fall_back_count_ = 0;
            return false;
        }
    }
    fall_back_count_++;
    return true;
}

void BackUpResourceTable::StartTimer() {
    timer_->Start(backup_idle_timeout_,
                  boost::bind(&BackUpResourceTable::TimerExpiry, this));
}

// Don't Update the file if there is any activity seen with in
// backup_idle_timeout_ and start the fallback count so that
// after 6th itteration file can be updated.
// if write to file fails trigger the timer.
void BackUpResourceTable::TriggerBackup() {
    if (timer_->running() == false)
        //Start Fallback timer.
        StartTimer();
    last_modified_time_ = UTCTimestampUsec();
}

bool BackUpResourceTable::UpdateRequired() {
    uint64_t current_time_stamp = UTCTimestampUsec();
    // dont update the file if the frequent updates are seen with in 10sec.
    if (current_time_stamp - last_modified_time_ <
            backup_idle_timeout_) {
        return false;
    }
    return true;
}

void BackUpResourceTable::EnqueueRestore(ResourceManager::KeyPtr key,
                                         ResourceManager::DataPtr data) {
    backup_manager()->resource_manager()->EnqueueRestore(key, data);
}

const std::string
BackUpResourceTable::FilePath(const std::string &file_name) {
    std::stringstream file_stream;
    file_stream << backup_dir_ << file_name;
    return file_stream.str();
}
// Write the content to file temprory file and rename the file
static const std::string TempFilePath(const std::string &file_name) {
    std::stringstream temp_file_stream;
    temp_file_stream << file_name << ".tmp";
    std::ofstream output;
    // Trunctate the file as it is a fresh write for the modified Map
    output.open(temp_file_stream.str().c_str(),
            std::ofstream::binary | std::ofstream::trunc);
    if (!output.good()) {
       LOG(ERROR, "File open failed" << temp_file_stream.str());
       output.close();
       return std::string();
    }

    output.close();
    return temp_file_stream.str();
}
// Rename the Temp file
static bool RenameFile(const std::string &file_tmp_name,
                     const std::string &file_name) {
    boost::system::error_code ec;
    boost::filesystem::path tmp_file_path(file_tmp_name.c_str());
    boost::filesystem::path backup_file_path(file_name.c_str());
    boost::filesystem::rename(tmp_file_path, backup_file_path, ec);
    if (ec != 0) {
        LOG(ERROR, "Resource backup mgr Rename file failed" << ec);
        return false;
    }
    return true;
}
// Calulate the MD5 Sum for the stored file
// This MD5 sum will be validated while reading the content.
bool BackUpResourceTable::CalculateMd5Sum(const std::string &file_name,
                                          uint8_t *md5sum) {
    std::stringstream error_str;
    uint32_t size;
    std::auto_ptr<uint8_t>buf;
    std::ifstream input(file_name.c_str(), std::ofstream::binary);
    if (!input.good()) {
        error_str << "Open failed to read file";
        goto error;
    }
    struct stat st;
    if (stat(file_name.c_str(), &st) == -1) {
        error_str << "Get size from file failed";
        goto error;
    }
    size = (uint32_t) st.st_size;
    buf.reset(new uint8_t [size]());
    input.read((char *)(buf.get()), size);

    if (!input.good()) {
        error_str << "reading file failed";
        goto error;
    }

    MD5(buf.get(), size, md5sum);
    input.close();
    return true;
    error: {
        error_str << file_name; 
        LOG(ERROR, error_str.str());
        input.close();
        return false;
    }
}
// Type T1 is Final output sandesh structure writes in to file
// Type T2 index map for the specific table
// Write the Map to file
// Calculate the md5sum and append that to file name
// so that validated while reading file
template <typename T1, typename T2>
bool BackUpResourceTable::WriteMapToFile(T1* sandesh_data, const T2& index_map,
                                         const std::string &file_name,
                                         const std::string &file_prefix) {
    uint32_t write_buff_size = 0;
    int error = 0;

    const std::string temp_file = TempFilePath(file_name);
    if (temp_file.empty()) {
        return false;
    }

    sandesh_data->set_index_map(index_map);
    sandesh_data->set_time_stamp(UTCTimestampUsec());
    write_buff_size = sandesh_data->WriteBinaryToFile(temp_file, &error);
    if (error != 0) {
        LOG(ERROR, "Sandesh Write Binary failed " << write_buff_size);
        return false;
    }

    uint8_t md5sum[MD5_DIGEST_LENGTH];
    memset(md5sum, 0, MD5_DIGEST_LENGTH);
    if (CalculateMd5Sum(temp_file, md5sum)) {
        // remove the file  with existing md5sum extention.
        std::string file = FindFile(backup_dir(), file_prefix);
        if (!file.empty()) {
            std::string file_path = backup_dir_ + "/" + file;
            std::remove(file_path.c_str());
        }
        // rename the tmp file to new file by appending md5sum
        std::string file_path;
        file_path = file_name + "_" + std::string((const char *)md5sum);
        return RenameFile(temp_file, file_path);
    }

    return false;
}
// Find the file with the prefix.
const std::string BackUpResourceTable::FindFile(const std::string &root,
                                                const std::string & file_prefix) {
    DIR *dir_path;
    struct dirent *dir;
    if ((dir_path = opendir(root.c_str())) == NULL) {
        return std::string();
    }
    while ((dir = readdir(dir_path)) != NULL) {
        std::string file_name = dir->d_name;
        // Match with the file prefix name if is not there return empty string
        if (file_name.find(file_prefix) != std::string::npos) {
            closedir(dir_path);
            return file_name;
        }
    }
    closedir(dir_path);
    return std::string();
}
// Read Map from the file.
// First read the file to a buffer
// verify that MD5 sum matches
template <typename T>
void BackUpResourceTable::ReadMapFromFile(T* sandesh_data,
                                          const std::string &root,
                                          const std::string& file_prefix) {
    // Find the File with prefix name
    const std::string file_name = FindFile(root, file_prefix);
    int error = 0;
    if (file_name.empty())
        return;
    // Make the complete file path with mds5sum
    std::stringstream file_path;
    file_path << root << "/"<< file_name;
    uint8_t *buffer;
    uint32_t size = backup_manager()->ReadResourceDataFromFile(file_path.str(),
                                                               &(buffer));
    if (buffer) {
        if (size) {
            uint8_t md5sum[MD5_DIGEST_LENGTH];
            memset(md5sum, 0, MD5_DIGEST_LENGTH);
            // calculate md5sum and verify it matches.
            MD5(buffer, size, md5sum);
            if (std::string::npos !=
                    file_name.find(std::string((const char*) md5sum))) {
                sandesh_data->ReadBinary(buffer, size, &error);
                if (error != 0) {
                    LOG(ERROR, "Sandesh Read Binary failed ");
                }
            }
        }
        delete [] buffer;
    }
}

VrfMplsBackUpResourceTable::VrfMplsBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "VrfMplsBackUpResourceTable"),
    vrf_file_name_str_(FilePath("/contrail_vrf_resource")) {
}

VrfMplsBackUpResourceTable::~VrfMplsBackUpResourceTable() {
}

bool VrfMplsBackUpResourceTable::WriteToFile() {
    VrfMplsResourceMapSandesh sandesh_data;
    return WriteMapToFile<VrfMplsResourceMapSandesh, Map>
        (&sandesh_data, map_, vrf_file_name_str_, "contrail_vrf_resource_");
}

void VrfMplsBackUpResourceTable::ReadFromFile() {
    VrfMplsResourceMapSandesh sandesh_data;
    ReadMapFromFile<VrfMplsResourceMapSandesh>(&sandesh_data,
                                               backup_dir(),
                                               "contrail_vrf_resource_");
    map_ = sandesh_data.get_index_map();
}

void VrfMplsBackUpResourceTable::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        VrfMplsResource sandesh_key = it->second;
        ResourceManager::KeyPtr key(new VrfMplsResourceKey
                                    (backup_manager()->resource_manager(),
                                     sandesh_key.get_name()));
        ResourceManager::DataPtr data(new IndexResourceData
                                      (backup_manager()->resource_manager(),
                                       index));
        EnqueueRestore(key, data);
    }

}

RouteMplsBackUpResourceTable::RouteMplsBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "RouteMplsBackUpResourceTable"),
    route_file_name_str_(FilePath("/contrail_route_resource")) {
 }

RouteMplsBackUpResourceTable::~RouteMplsBackUpResourceTable() {
}

bool  RouteMplsBackUpResourceTable::WriteToFile() {
    RouteMplsResourceMapSandesh sandesh_data;
    return WriteMapToFile<RouteMplsResourceMapSandesh, Map>
        (&sandesh_data, map_, route_file_name_str_, "contrail_route_resource_");
}

void RouteMplsBackUpResourceTable::ReadFromFile() {
    RouteMplsResourceMapSandesh sandesh_data;
    ReadMapFromFile<RouteMplsResourceMapSandesh>(&sandesh_data, backup_dir(),
                                                 "contrail_route_resource_");
    map_ = sandesh_data.get_index_map();
}

void RouteMplsBackUpResourceTable::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        RouteMplsResource sandesh_key = it->second;
        ResourceManager::KeyPtr key(new RouteMplsResourceKey
                                    (backup_manager()->resource_manager(),
                                     sandesh_key.get_vrf_name(),
                                     sandesh_key.get_route_prefix()));
        ResourceManager::DataPtr data(new IndexResourceData
                                      (backup_manager()->resource_manager(),
                                       index));
        EnqueueRestore(key, data);
     }
}

InterfaceMplsBackUpResourceTable::InterfaceMplsBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "InterfaceMplsBackUpResourceTable"),
    interface_file_name_str_(FilePath("/contrail_interface_resource")) {
}

InterfaceMplsBackUpResourceTable::~InterfaceMplsBackUpResourceTable() {
}

bool InterfaceMplsBackUpResourceTable::WriteToFile() {
    InterfaceIndexResourceMapSandesh sandesh_data;
    return WriteMapToFile<InterfaceIndexResourceMapSandesh, Map>
        (&sandesh_data, map_, interface_file_name_str_,
         "contrail_interface_resource_");
}

void InterfaceMplsBackUpResourceTable::ReadFromFile() {
    InterfaceIndexResourceMapSandesh sandesh_data;
    ReadMapFromFile<InterfaceIndexResourceMapSandesh>
        (&sandesh_data, backup_dir(), "contrail_interface_resource_");
    map_ = sandesh_data.get_index_map();
}

void InterfaceMplsBackUpResourceTable::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        InterfaceIndexResource sandesh_key = it->second;
        MacAddress mac = MacAddress::FromString(sandesh_key.get_mac());
        ResourceManager::KeyPtr key(new InterfaceIndexResourceKey
                                    (backup_manager()->resource_manager(),
                                     StringToUuid(sandesh_key.get_uuid()),
                                     mac, sandesh_key.get_policy(),
                                     sandesh_key.get_type(),
                                     sandesh_key.get_vlan_tag()));
        ResourceManager::DataPtr data(new IndexResourceData
                                      (backup_manager()->resource_manager(),
                                       index));
        EnqueueRestore(key, data);
    }
}

ResourceSandeshMaps::ResourceSandeshMaps(ResourceBackupManager *manager) :
    backup_manager_(manager), agent_(manager->agent()),
    interface_mpls_index_table_(manager), vrf_mpls_index_table_(manager),
    route_mpls_index_table_(manager) {
}

ResourceSandeshMaps::~ResourceSandeshMaps() {
}

void ResourceSandeshMaps::ReadFromFile() {
    interface_mpls_index_table_.ReadFromFile();
    vrf_mpls_index_table_.ReadFromFile();
    route_mpls_index_table_.ReadFromFile();
}

void ResourceSandeshMaps::EndOfBackup() {
    ResourceManager::KeyPtr key
        (new ResourceBackupEndKey(agent_->resource_manager()));
    ResourceManager::DataPtr data;
    agent_->resource_manager()->EnqueueRestore(key, data);
}

void ResourceSandeshMaps::RestoreResource() {
    interface_mpls_index_table_.RestoreResource();
    vrf_mpls_index_table_.RestoreResource();
    route_mpls_index_table_.RestoreResource();
    EndOfBackup();
}

void InterfaceIndexResourceMapSandesh::Process(SandeshContext*) {

}

void VrfMplsResourceMapSandesh::Process(SandeshContext*) {

}

void RouteMplsResourceMapSandesh::Process(SandeshContext*) {

}

void ResourceSandeshMaps::AddInterfaceMplsResourceEntry(uint32_t index,
                                       InterfaceIndexResource data ) {
    interface_mpls_index_table_.map().insert(InterfaceMplsResourcePair(index,
                                                                    data));
}
void ResourceSandeshMaps::DeleteInterfaceMplsResourceEntry(uint32_t index) {
    interface_mpls_index_table_.map().erase(index);
}

void ResourceSandeshMaps::AddVrfMplsResourceEntry(uint32_t index,
                                                  VrfMplsResource data) {
    vrf_mpls_index_table_.map().insert(VrfMplsResourcePair(index, data));
}

void ResourceSandeshMaps::DeleteVrfMplsResourceEntry(uint32_t index) {
    vrf_mpls_index_table_.map().erase(index);
}

void ResourceSandeshMaps::AddRouteMplsResourceEntry(uint32_t index,
                                                    RouteMplsResource data) {
    route_mpls_index_table_.map().insert(RouteMplsResourcePair(index, data));
}

void ResourceSandeshMaps::DeleteRouteMplsResourceEntry(uint32_t index) {
    route_mpls_index_table_.map().erase(index);
}
