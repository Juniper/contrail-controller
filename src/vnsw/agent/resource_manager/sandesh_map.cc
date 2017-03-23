/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <dirent.h>
#include <cctype>
#include <stdlib.h>
#include <cmn/agent.h>
#include <boost/filesystem.hpp>
#include <boost/functional/hash.hpp>
#include <boost/algorithm/string.hpp>
#include "init/agent_param.h"
#include <base/timer.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "resource_manager/resource_backup.h"
#include "resource_manager/resource_table.h"
#include "resource_manager/sandesh_map.h"
#include "resource_manager/resource_manager.h"
#include "resource_manager/mpls_index.h"
#include <oper/nexthop.h>

BackUpResourceTable::BackUpResourceTable(ResourceBackupManager *manager,
                                         const std::string &name,
                                         const std::string &file_name) :
    backup_manager_(manager), agent_(manager->agent()), name_(name),
    last_modified_time_(UTCTimestampUsec()) {
    backup_dir_ = agent_->params()->restart_backup_dir();
    backup_idle_timeout_ = agent_->params()
        ->restart_backup_idle_timeout();
    file_name_str_ = backup_dir_ + "/" + file_name;
    file_name_prefix_ = file_name + "-";
    boost::filesystem::path dir(backup_dir_.c_str());
    if (!boost::filesystem::exists(backup_dir_))
        boost::filesystem::create_directory(backup_dir_);
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

// Calulate the Hash value for the stored file
// This hash sum will be validated while reading the content.
bool BackUpResourceTable::CalculateHashSum(const std::string &file_name,
                                           uint32_t *hashsum) {
    std::auto_ptr<uint8_t> buffer;
    uint32_t size = ResourceBackupManager::ReadResourceDataFromFile(file_name,
                                                                    &(buffer));
    if (size && buffer.get()) {
        *hashsum = (uint32_t)boost::hash_range(buffer.get(),
                                               buffer.get()+size);
        return true;
    }
    return false;
}

// Type T1 is Final output sandesh structure writes in to file
// Type T2 index map for the specific table
// Write the Map to file
// Calculate the hashsum and append that to file name
// so that validated while reading file
template <typename T1, typename T2>
bool BackUpResourceTable::WriteMapToFile(T1* sandesh_data,
                                         const T2& index_map) {
    uint32_t write_buff_size = 0;
    int error = 0;

    const std::string temp_file = TempFilePath(file_name_str());
    if (temp_file.empty()) {
        LOG(ERROR, "Temp file is not created");
        return false;
    }

    sandesh_data->set_index_map(index_map);
    sandesh_data->set_time_stamp(UTCTimestampUsec());
    write_buff_size = sandesh_data->WriteBinaryToFile(temp_file, &error);
    if (error != 0) {
        LOG(ERROR, "Sandesh Write Binary failed " << write_buff_size);
        return false;
    }

    uint32_t hashsum;
    if (CalculateHashSum(temp_file, &hashsum)) {
        // remove the file  with existing hashsum extention.
        std::string file = FindFile(backup_dir(), file_name_prefix());
        if (!file.empty()) {
            std::string file_path = backup_dir_ + "/" + file;
            std::remove(file_path.c_str());
        }
        // rename the tmp file to new file by appending hashsum
        std::stringstream file_path;
        file_path << file_name_str() << "-" << hashsum;
        return RenameFile(temp_file, file_path.str());
    }

    return false;
}

// TODO final file format needs to be defined along with 3rd backup file.
// function needs to be enhanced with 3rd backup file.
// Find the file with the prefix.
const std::string
BackUpResourceTable::FindFile(const std::string &root,
                              const std::string & file_prefix) {
    DIR *dir_path;
    struct dirent *dir;
    if ((dir_path = opendir(root.c_str())) == NULL) {
        return std::string();
    }
    while ((dir = readdir(dir_path)) != NULL) {
        std::string file_name = dir->d_name;
        // Match with the file prefix name if is not there return empty string
        // Check Start of the file_name matches with prefix name.
        if (!file_name.find(file_prefix)) {
            // check for file format filename-hashvalue(digits)
            // example name contrail_interface_resource-12345678
            std::string tmpstr(file_name.c_str());
            std::vector<string> tokens;
            boost::split(tokens, tmpstr, boost::is_any_of("-"));
            // split the string check after file prefix hashsum is number.
            std::string token;
            if (tokens.size()) {
                // TODO file format changes this needs to be revisited.
                token = tokens[tokens.size() - 1];
            }

            bool found = true;
            for (int i =0; token[i] != '\0'; i++) {
                if (!isdigit(token[i])) {
                    found = false;
                    break;
                }
            }

            if (found) {
                closedir(dir_path);
                return file_name;
            }
        }
    }
    closedir(dir_path);
    return std::string();
}

// Read Map from the file.
// First read the file to a buffer
// verify that hash sum matches
template <typename T>
void BackUpResourceTable::ReadMapFromFile(T* sandesh_data,
                                          const std::string &root) {
    // Find the File with prefix name
    const std::string file_name = FindFile(root, file_name_prefix());
    int error = 0;
    if (file_name.empty()) {
        LOG(DEBUG, "File name with prefix not found ");
        return;
    }
    // Make the complete file path with hash value
    std::stringstream file_path;
    file_path << root << "/"<< file_name;
    if (!boost::filesystem::exists( file_path.str().c_str())) {
        LOG(DEBUG, "File path not found " << file_path.str());
        return;
    }
    std::auto_ptr<uint8_t> buffer;
    uint32_t size = ResourceBackupManager::ReadResourceDataFromFile(file_path.str(),
                                                                    &(buffer));
    if (buffer.get()) {
        if (size) {
            uint32_t hashsum = (uint32_t)boost::hash_range(buffer.get(),
                                                           buffer.get()+size);
            std::stringstream hash_value;
            hash_value << hashsum;
            // Check for hashsum present.
            if (std::string::npos !=
                    file_name.find(hash_value.str())) {
                sandesh_data->ReadBinary(buffer.get(), size, &error);
                if (error != 0) {
                    LOG(ERROR, "Sandesh Read Binary failed ");
                }
            }
        }
    }
}

VrfMplsBackUpResourceTable::VrfMplsBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "VrfMplsBackUpResourceTable",
                        "contrail_vrf_resource") {
}

VrfMplsBackUpResourceTable::~VrfMplsBackUpResourceTable() {
}

bool VrfMplsBackUpResourceTable::WriteToFile() {
    VrfMplsResourceMapSandesh sandesh_data;
    return WriteMapToFile<VrfMplsResourceMapSandesh, Map> (&sandesh_data, map_);
}

void VrfMplsBackUpResourceTable::ReadFromFile() {
    VrfMplsResourceMapSandesh sandesh_data;
    ReadMapFromFile<VrfMplsResourceMapSandesh>(&sandesh_data,
                                               backup_dir());
    map_ = sandesh_data.get_index_map();
}

void VrfMplsBackUpResourceTable::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        VrfMplsResource sandesh_key = it->second;
        VrfNHKey *vrf_nh_key = new VrfNHKey(sandesh_key.get_name(), false,
                                            sandesh_key.get_vxlan_nh());
        ResourceManager::KeyPtr key(new NexthopIndexResourceKey(
                                    backup_manager()->resource_manager(),
                                    vrf_nh_key));
        ResourceManager::DataPtr data(new IndexResourceData
                                      (backup_manager()->resource_manager(),
                                       index));
        EnqueueRestore(key, data);
    }
}

VlanMplsBackUpResourceTable::VlanMplsBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "VlanMplsBackUpResourceTable",
                        "contrail_vlan_resource") {
}

VlanMplsBackUpResourceTable::~VlanMplsBackUpResourceTable() {
}

bool VlanMplsBackUpResourceTable::WriteToFile() {
    VlanMplsResourceMapSandesh sandesh_data;
    return WriteMapToFile<VlanMplsResourceMapSandesh, Map>
               (&sandesh_data, map_);
}

void VlanMplsBackUpResourceTable::ReadFromFile() {
    VlanMplsResourceMapSandesh sandesh_data;
    ReadMapFromFile<VlanMplsResourceMapSandesh>(&sandesh_data,
                                               backup_dir());
    map_ = sandesh_data.get_index_map();
}

void VlanMplsBackUpResourceTable::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        VlanMplsResource sandesh_key = it->second;
        VlanNHKey *vlan_nh_key = new VlanNHKey(
                                         StringToUuid(sandesh_key.get_uuid()),
                                         sandesh_key.get_tag());
        ResourceManager::KeyPtr key(new NexthopIndexResourceKey(
                                    backup_manager()->resource_manager(),
                                    vlan_nh_key));
        ResourceManager::DataPtr data(new IndexResourceData
                                      (backup_manager()->resource_manager(),
                                       index));
        EnqueueRestore(key, data);
    }
}

RouteMplsBackUpResourceTable::RouteMplsBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "RouteMplsBackUpResourceTable",
                        "contrail_route_resource") {
}

RouteMplsBackUpResourceTable::~RouteMplsBackUpResourceTable() {
}

bool RouteMplsBackUpResourceTable::WriteToFile() {
    RouteMplsResourceMapSandesh sandesh_data;
    return WriteMapToFile<RouteMplsResourceMapSandesh, Map> (&sandesh_data, map_);
}

void RouteMplsBackUpResourceTable::ReadFromFile() {
    RouteMplsResourceMapSandesh sandesh_data;
    ReadMapFromFile<RouteMplsResourceMapSandesh>(&sandesh_data, backup_dir());
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
    BackUpResourceTable(manager, "InterfaceMplsBackUpResourceTable",
                        "contrail_interface_resource") {
}

InterfaceMplsBackUpResourceTable::~InterfaceMplsBackUpResourceTable() {
}

bool InterfaceMplsBackUpResourceTable::WriteToFile() {
    InterfaceIndexResourceMapSandesh sandesh_data;
    return WriteMapToFile<InterfaceIndexResourceMapSandesh, Map>
        (&sandesh_data, map_);
}

void InterfaceMplsBackUpResourceTable::ReadFromFile() {
    InterfaceIndexResourceMapSandesh sandesh_data;
    ReadMapFromFile<InterfaceIndexResourceMapSandesh>
        (&sandesh_data, backup_dir());
    map_ = sandesh_data.get_index_map();
}

void InterfaceMplsBackUpResourceTable::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        InterfaceIndexResource sandesh_key = it->second;
        MacAddress mac = MacAddress::FromString(sandesh_key.get_mac());
        std::string type = sandesh_key.get_type();
        InterfaceNHKey *itf_nh_key = NULL;
        InterfaceKey *itf_key = NULL;
        if (type == "vmi") {
            //Vm interface
            itf_key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                         StringToUuid(sandesh_key.get_uuid()),
                                         sandesh_key.get_name());
        } else {
            //Inet interface
            itf_key = new InetInterfaceKey(sandesh_key.get_name());
        }
        itf_nh_key = new InterfaceNHKey(itf_key, sandesh_key.get_policy(),
                                        sandesh_key.get_flags(), mac);
        ResourceManager::KeyPtr key(new NexthopIndexResourceKey(
                                    backup_manager()->resource_manager(),
                                    itf_nh_key));
        ResourceManager::DataPtr data(new IndexResourceData
                                      (backup_manager()->resource_manager(),
                                       index));
        EnqueueRestore(key, data);
    }
}

ResourceSandeshMaps::ResourceSandeshMaps(ResourceBackupManager *manager) :
    backup_manager_(manager), agent_(manager->agent()),
    interface_mpls_index_table_(manager), vrf_mpls_index_table_(manager),
    vlan_mpls_index_table_(manager), route_mpls_index_table_(manager) {
}

ResourceSandeshMaps::~ResourceSandeshMaps() {
}

void ResourceSandeshMaps::ReadFromFile() {
    interface_mpls_index_table_.ReadFromFile();
    vrf_mpls_index_table_.ReadFromFile();
    vlan_mpls_index_table_.ReadFromFile();
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
    vlan_mpls_index_table_.RestoreResource();
    route_mpls_index_table_.RestoreResource();
    EndOfBackup();
}

void InterfaceIndexResourceMapSandesh::Process(SandeshContext*) {

}

void VrfMplsResourceMapSandesh::Process(SandeshContext*) {

}

void VlanMplsResourceMapSandesh::Process(SandeshContext*) {

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

void ResourceSandeshMaps::AddVlanMplsResourceEntry(uint32_t index,
                                                   VlanMplsResource data) {
    vlan_mpls_index_table_.map().insert(VlanMplsResourcePair(index, data));
}

void ResourceSandeshMaps::DeleteVlanMplsResourceEntry(uint32_t index) {
    vlan_mpls_index_table_.map().erase(index);
}

void ResourceSandeshMaps::AddRouteMplsResourceEntry(uint32_t index,
                                                    RouteMplsResource data) {
    route_mpls_index_table_.map().insert(RouteMplsResourcePair(index, data));
}

void ResourceSandeshMaps::DeleteRouteMplsResourceEntry(uint32_t index) {
    route_mpls_index_table_.map().erase(index);
}
