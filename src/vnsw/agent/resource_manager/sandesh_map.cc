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
#include "resource_manager/vm_interface_index.h"
#include "resource_manager/vrf_index.h"
#include "resource_manager/qos_index.h"
#include "resource_manager/bgp_as_service_index.h"
#include "resource_manager/mirror_index.h"
#include "resource_manager/nexthop_index.h"
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>

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
// Vm interface  backup.
VmInterfaceBackUpResourceTable::VmInterfaceBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "VmInterfaceBackUpResourceTable",
                        "contrail_vm_interface_resource") {
}

VmInterfaceBackUpResourceTable::~VmInterfaceBackUpResourceTable() {
}

bool VmInterfaceBackUpResourceTable::WriteToFile() {
    VmInterfaceIndexResourceMapSandesh sandesh_data;
    return WriteMapToFile<VmInterfaceIndexResourceMapSandesh, Map>
        (&sandesh_data, map_);
}

void VmInterfaceBackUpResourceTable::ReadFromFile() {
    VmInterfaceIndexResourceMapSandesh sandesh_data;
    ReadMapFromFile<VmInterfaceIndexResourceMapSandesh>
        (&sandesh_data, backup_dir());
    map_ = sandesh_data.get_index_map();
}

void VmInterfaceBackUpResourceTable::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        VmInterfaceIndexResource sandesh_key = it->second;

        ResourceManager::KeyPtr key(new VmInterfaceIndexResourceKey(
                                    backup_manager()->resource_manager(),
                                    StringToUuid(sandesh_key.get_uuid()),
                                    sandesh_key.get_interface_name()));
        ResourceManager::DataPtr data(new IndexResourceData
                                      (backup_manager()->resource_manager(),
                                       index));
        EnqueueRestore(key, data);
    }
}
// Vrf  backup.
VrfBackUpResourceTable::VrfBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "VrfBackUpResourceTable",
                        "contrail_vrf_index_resource") {
}

VrfBackUpResourceTable::~VrfBackUpResourceTable() {
}

bool VrfBackUpResourceTable::WriteToFile() {
    VrfIndexResourceMapSandesh sandesh_data;
    return WriteMapToFile<VrfIndexResourceMapSandesh, Map>
        (&sandesh_data, map_);
}

void VrfBackUpResourceTable::ReadFromFile() {
    VrfIndexResourceMapSandesh sandesh_data;
    ReadMapFromFile<VrfIndexResourceMapSandesh>
        (&sandesh_data, backup_dir());
    map_ = sandesh_data.get_index_map();
}

void VrfBackUpResourceTable::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        VrfIndexResource sandesh_key = it->second;

        ResourceManager::KeyPtr key(new VrfIndexResourceKey(
                                    backup_manager()->resource_manager(),
                                    sandesh_key.get_vrf_name()));
        ResourceManager::DataPtr data(new IndexResourceData
                                      (backup_manager()->resource_manager(),
                                       index));
        EnqueueRestore(key, data);
    }
}

// Qos id backup.
QosBackUpResourceTable::QosBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "QosBackUpResourceTable",
                        "contrail_qos_resource") {
}

QosBackUpResourceTable::~QosBackUpResourceTable() {
}

bool QosBackUpResourceTable::WriteToFile() {
    QosIndexResourceMapSandesh sandesh_data;
    return WriteMapToFile<QosIndexResourceMapSandesh, Map>
        (&sandesh_data, map_);
}

void QosBackUpResourceTable::ReadFromFile() {
    QosIndexResourceMapSandesh sandesh_data;
    ReadMapFromFile<QosIndexResourceMapSandesh>
        (&sandesh_data, backup_dir());
    map_ = sandesh_data.get_index_map();
}

void QosBackUpResourceTable::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        QosIndexResource sandesh_key = it->second;

        ResourceManager::KeyPtr key(new QosIndexResourceKey(
                                    backup_manager()->resource_manager(),
                                    StringToUuid(sandesh_key.get_uuid())));
        ResourceManager::DataPtr data(new IndexResourceData
                                      (backup_manager()->resource_manager(),
                                       index));
        EnqueueRestore(key, data);
    }
}

// bgp as service id backup.
BgpAsServiceBackUpResourceTable::BgpAsServiceBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "BgpAsServiceBackUpResourceTable",
                        "contrail_bgp_as_service_resource") {
}

BgpAsServiceBackUpResourceTable::~BgpAsServiceBackUpResourceTable() {
}

bool BgpAsServiceBackUpResourceTable::WriteToFile() {
    BgpAsServiceIndexResourceMapSandesh sandesh_data;
    return WriteMapToFile<BgpAsServiceIndexResourceMapSandesh, Map>
        (&sandesh_data, map_);
}

void BgpAsServiceBackUpResourceTable::ReadFromFile() {
    BgpAsServiceIndexResourceMapSandesh sandesh_data;
    ReadMapFromFile<BgpAsServiceIndexResourceMapSandesh>
        (&sandesh_data, backup_dir());
    map_ = sandesh_data.get_index_map();
}

void BgpAsServiceBackUpResourceTable::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        BgpAsServiceIndexResource sandesh_key = it->second;

        ResourceManager::KeyPtr key(new BgpAsServiceIndexResourceKey(
                                    backup_manager()->resource_manager(),
                                    StringToUuid(sandesh_key.get_uuid())));
        ResourceManager::DataPtr data(new IndexResourceData
                                      (backup_manager()->resource_manager(),
                                       index));
        EnqueueRestore(key, data);
    }
}

// Mirror  backup.
MirrorBackUpResourceTable::MirrorBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "MirrorBackUpResourceTable",
                        "contrail_mirror_index_resource") {
}

MirrorBackUpResourceTable::~MirrorBackUpResourceTable() {
}

bool MirrorBackUpResourceTable::WriteToFile() {
    MirrorIndexResourceMapSandesh sandesh_data;
    return WriteMapToFile<MirrorIndexResourceMapSandesh, Map>
        (&sandesh_data, map_);
}

void MirrorBackUpResourceTable::ReadFromFile() {
    MirrorIndexResourceMapSandesh sandesh_data;
    ReadMapFromFile<MirrorIndexResourceMapSandesh>
        (&sandesh_data, backup_dir());
    map_ = sandesh_data.get_index_map();
}

void MirrorBackUpResourceTable::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        MirrorIndexResource sandesh_key = it->second;

        ResourceManager::KeyPtr key(new MirrorIndexResourceKey(
                                    backup_manager()->resource_manager(),
                                    sandesh_key.get_analyzer_name()));
        ResourceManager::DataPtr data(new IndexResourceData
                                      (backup_manager()->resource_manager(),
                                       index));
        EnqueueRestore(key, data);
    }
}

//next  backup.
NextHopBackUpResourceTable::NextHopBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "NextHopBackUpResourceTable",
                        "contrail_nexthop_index_resource") {
}

NextHopBackUpResourceTable::~NextHopBackUpResourceTable() {
}

bool NextHopBackUpResourceTable::WriteToFile() {
    NextHopIndexResourceMapSandesh sandesh_data;
    return WriteMapToFile<NextHopIndexResourceMapSandesh, Map>
        (&sandesh_data, map_);
}

void NextHopBackUpResourceTable::ReadFromFile() {
    NextHopIndexResourceMapSandesh sandesh_data;
    ReadMapFromFile<NextHopIndexResourceMapSandesh>
        (&sandesh_data, backup_dir());
    map_ = sandesh_data.get_index_map();
}

// Restore the NHResourceKey based the type of the nexthop key.
void NextHopBackUpResourceTable::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        NextHopResource sandesh_key = it->second;
        ResourceManager::DataPtr data(new IndexResourceData
                                      (backup_manager()->resource_manager(),
                                       index));
        switch (sandesh_key.get_type()) {
            case NextHop::INTERFACE: {
                InterfaceNHKey *itf_nh_key = NULL;
                InterfaceKey *itf_key = NULL;
                MacAddress mac = MacAddress::FromString(sandesh_key.get_mac());
                if (sandesh_key.get_intf_type() == Interface::VM_INTERFACE) {
                    //Vm interface
                    itf_key = new VmInterfaceKey
                        (AgentKey::ADD_DEL_CHANGE,
                        StringToUuid(sandesh_key.get_uuid()),
                        sandesh_key.get_name());
                } else {
                    //Inet interface
                    itf_key = new InetInterfaceKey(sandesh_key.get_name());
                }
                itf_nh_key = new InterfaceNHKey(itf_key,
                                                sandesh_key.get_policy(),
                                                sandesh_key.get_flags(), mac);
                ResourceManager::KeyPtr key (new NHIndexResourceKey
                     ( backup_manager()->resource_manager(), NextHop::INTERFACE,
                       itf_nh_key));
                EnqueueRestore(key, data);
                break;
            }
            case NextHop::VLAN: {
                VlanNHKey *vlan_nh_key = new VlanNHKey
                    (StringToUuid(sandesh_key.get_uuid()),
                     sandesh_key.get_tag());
                ResourceManager::KeyPtr key(new NHIndexResourceKey
                     ( backup_manager()->resource_manager(), NextHop::VLAN,
                       vlan_nh_key));
                EnqueueRestore(key, data);
                break;
            }
            case  NextHop::VRF: {
                VrfNHKey *vrf_nh_key = new VrfNHKey(sandesh_key.get_name(),
                                                    sandesh_key.get_policy(),
                                                    sandesh_key.get_vxlan_nh());
                ResourceManager::KeyPtr key(new NHIndexResourceKey
                     ( backup_manager()->resource_manager(), NextHop::VRF,
                       vrf_nh_key));
                EnqueueRestore(key, data);
                break;
            }
            case NextHop::RECEIVE: {
                InterfaceKey *itf_key = NULL;
                if (sandesh_key.get_intf_type() == Interface::VM_INTERFACE) {
                    //Vm interface
                    itf_key = new VmInterfaceKey
                        (AgentKey::ADD_DEL_CHANGE,
                        StringToUuid(sandesh_key.get_uuid()),
                        sandesh_key.get_name());
                } else {
                    //Inet interface
                    itf_key = new InetInterfaceKey(sandesh_key.get_name());
                }
                ReceiveNHKey *receive_nh_key = new ReceiveNHKey
                    (itf_key, sandesh_key.get_policy());
                ResourceManager::KeyPtr key(new NHIndexResourceKey
                     ( backup_manager()->resource_manager(), NextHop::RECEIVE,
                       receive_nh_key));
                EnqueueRestore(key, data);
                break;
            }
            case NextHop::RESOLVE: {
                InterfaceKey *itf_key = NULL;
                if (sandesh_key.get_intf_type() == Interface::VM_INTERFACE) {
                    //Vm interface
                    itf_key = new VmInterfaceKey
                        (AgentKey::ADD_DEL_CHANGE,
                        StringToUuid(sandesh_key.get_uuid()),
                        sandesh_key.get_name());
                } else {
                    //Inet interface
                    itf_key = new InetInterfaceKey(sandesh_key.get_name());
                }
                ResolveNHKey *resolve_nh_key = new ResolveNHKey
                    (itf_key, sandesh_key.get_policy());
                ResourceManager::KeyPtr key(new NHIndexResourceKey
                     ( backup_manager()->resource_manager(), NextHop::RESOLVE,
                       resolve_nh_key));
                EnqueueRestore(key, data);
                break;
            }
            case NextHop::ARP: {
                ArpNHKey *arp_nh_key = new ArpNHKey(sandesh_key.get_vrf_name(),
                                                    Ip4Address(sandesh_key.get_dip()),
                                                    sandesh_key.get_policy());
                ResourceManager::KeyPtr key(new NHIndexResourceKey
                     ( backup_manager()->resource_manager(), NextHop::ARP,
                       arp_nh_key));
                EnqueueRestore(key, data);
                break;
            }
            case NextHop::TUNNEL: {
                TunnelType type ((TunnelType::Type)sandesh_key.get_tunnel_type());
                TunnelNHKey *tunnel_nh_key =
                    new TunnelNHKey(sandesh_key.get_vrf_name(), 
                                    Ip4Address(sandesh_key.get_sip()),
                                    Ip4Address(sandesh_key.get_dip()),
                                    sandesh_key.get_policy(),
                                    type);
                ResourceManager::KeyPtr key(new NHIndexResourceKey
                     ( backup_manager()->resource_manager(), NextHop::TUNNEL,
                       tunnel_nh_key));
                EnqueueRestore(key, data);
                break;
            }
            case NextHop::PBB: {
                MacAddress mac = MacAddress::FromString(sandesh_key.get_mac());
                PBBNHKey *pbb_nh_key = new PBBNHKey(sandesh_key.get_vrf_name(),
                                                    mac,
                                                    sandesh_key.get_isid());
                ResourceManager::KeyPtr key(new NHIndexResourceKey
                     ( backup_manager()->resource_manager(), NextHop::PBB,
                       pbb_nh_key));
                EnqueueRestore(key, data);

                break;
            }
            case NextHop::MIRROR: {
                MirrorNHKey *mirror_nh_key =
                    new MirrorNHKey(sandesh_key.get_vrf_name(),
                                    Ip4Address(sandesh_key.get_sip()),
                                    sandesh_key.get_sport(),
                                    Ip4Address(sandesh_key.get_dip()),
                                    sandesh_key.get_dport());
                ResourceManager::KeyPtr key(new NHIndexResourceKey
                     ( backup_manager()->resource_manager(), NextHop::MIRROR,
                       mirror_nh_key));
                EnqueueRestore(key, data);
                break;
            }
            default:
                break;
        }
    }
}

//next  backup.
ComposteNHBackUpResourceTable::ComposteNHBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "ComposteNHBackUpResourceTable",
                        "contrail_composite_index_resource") {
}

ComposteNHBackUpResourceTable::~ComposteNHBackUpResourceTable() {

}

void ComposteNHBackUpResourceTable::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        CompositeNHIndexResource sandesh_key = it->second;
        ResourceManager::DataPtr data(new IndexResourceData
                                      (backup_manager()->resource_manager(),
                                       index));
        std::vector<cnhid_label_map> nhid_label_map;
        for (uint32_t i=0; i< sandesh_key.get_nhid_label_map().size(); i++) {
            cnhid_label_map nhid_lable;
            nhid_lable.nh_id = sandesh_key.get_nhid_label_map()[i].nh_id;
            nhid_lable.label = sandesh_key.get_nhid_label_map()[i].label;
            nhid_label_map.push_back(nhid_lable);
        }
        ResourceManager::KeyPtr key(new NHIndexResourceKey
                                    ( backup_manager()->resource_manager(),
                                      NextHop::COMPOSITE,
                                      sandesh_key.get_type(),
                                      nhid_label_map,
                                      sandesh_key.get_policy(),
                                      sandesh_key.get_vrf_name()));
        EnqueueRestore(key, data);
    }
}

bool ComposteNHBackUpResourceTable::WriteToFile() {
    CompositeNHIndexResourceMapSandesh sandesh_data;
    return WriteMapToFile<CompositeNHIndexResourceMapSandesh, Map>
        (&sandesh_data, map_);
}

void ComposteNHBackUpResourceTable::ReadFromFile() {
    CompositeNHIndexResourceMapSandesh sandesh_data;
    ReadMapFromFile<CompositeNHIndexResourceMapSandesh>
        (&sandesh_data, backup_dir());
}

ResourceSandeshMaps::ResourceSandeshMaps(ResourceBackupManager *manager) :
    backup_manager_(manager), agent_(manager->agent()),
    interface_mpls_index_table_(manager), vrf_mpls_index_table_(manager),
    vlan_mpls_index_table_(manager), route_mpls_index_table_(manager),
    vm_interface_index_table_(manager), vrf_index_table_ (manager),
    qos_index_table_ (manager), bgp_as_service_index_table_(manager),
    mirror_index_table_(manager), nexthop_index_table_(manager), 
    compositenh_index_table_(manager) {
}

ResourceSandeshMaps::~ResourceSandeshMaps() {
}

void ResourceSandeshMaps::ReadFromFile() {
    interface_mpls_index_table_.ReadFromFile();
    vrf_mpls_index_table_.ReadFromFile();
    vlan_mpls_index_table_.ReadFromFile();
    route_mpls_index_table_.ReadFromFile();
    vm_interface_index_table_.ReadFromFile();
    vrf_index_table_.ReadFromFile();
    qos_index_table_.ReadFromFile();
    bgp_as_service_index_table_.ReadFromFile();
    mirror_index_table_.ReadFromFile();
    nexthop_index_table_.ReadFromFile();
    compositenh_index_table_.ReadFromFile();
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
    vm_interface_index_table_.RestoreResource();
    vrf_index_table_.RestoreResource();
    qos_index_table_.RestoreResource();
    bgp_as_service_index_table_.RestoreResource();
    mirror_index_table_.RestoreResource();
    nexthop_index_table_.RestoreResource();
    compositenh_index_table_.ReadFromFile();
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

void VmInterfaceIndexResourceMapSandesh::Process(SandeshContext*) {

}

void VrfIndexResourceMapSandesh::Process(SandeshContext*) {

}

void QosIndexResourceMapSandesh::Process(SandeshContext*) {

}
void BgpAsServiceIndexResourceMapSandesh::Process(SandeshContext*) {

}

void MirrorIndexResourceMapSandesh::Process(SandeshContext*) {

}

void CompositeNHIndexResourceMapSandesh::Process(SandeshContext*) {

}

void NextHopIndexResourceMapSandesh::Process(SandeshContext*) {

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

void ResourceSandeshMaps::AddVmInterfaceResourceEntry(uint32_t index,
                                       VmInterfaceIndexResource data ) {
    vm_interface_index_table_.map().insert(VmInterfaceIndexResourcePair
            (index, data));
}
void ResourceSandeshMaps::DeleteVmInterfaceResourceEntry(uint32_t index) {
    vm_interface_index_table_.map().erase(index);
}

void ResourceSandeshMaps::AddVrfResourceEntry(uint32_t index,
                                              VrfIndexResource data ) {
    vrf_index_table_.map().insert(VrfIndexResourcePair
            (index, data));
}
void ResourceSandeshMaps::DeleteVrfResourceEntry(uint32_t index) {
    vrf_index_table_.map().erase(index);
}

void ResourceSandeshMaps::AddQosResourceEntry(uint32_t index,
                                              QosIndexResource data ) {
    qos_index_table_.map().insert(QosIndexResourcePair
            (index, data));
}
void ResourceSandeshMaps::DeleteQosResourceEntry(uint32_t index) {
    qos_index_table_.map().erase(index);
}

void ResourceSandeshMaps::AddBgpAsServiceResourceEntry
(uint32_t index, BgpAsServiceIndexResource data ) {
    bgp_as_service_index_table_.map().insert(BgpAsServiceIndexResourcePair
            (index, data));
}

void ResourceSandeshMaps::DeleteBgpAsServiceResourceEntry(uint32_t index) {
    bgp_as_service_index_table_.map().erase(index);
}

void ResourceSandeshMaps::AddMirrorResourceEntry(uint32_t index,
                                                 MirrorIndexResource data ) {
    mirror_index_table_.map().insert(MirrorIndexResourcePair
            (index, data));
}

void ResourceSandeshMaps::DeleteMirrorResourceEntry(uint32_t index) {
    mirror_index_table_.map().erase(index);
}

void ResourceSandeshMaps::AddNextHopResourceEntry(uint32_t index,
                                                 NextHopResource data ) {
    nexthop_index_table_.map().insert(NexthopIndexResourcePair (index, data));
}

void ResourceSandeshMaps::DeleteNextHopResourceEntry(uint32_t index) {
    nexthop_index_table_.map().erase(index);
}
void ResourceSandeshMaps::AddCompositeNHResourceEntry
(uint32_t index, CompositeNHIndexResource data) {
    compositenh_index_table_.map().insert(ComposteNHIndexResourcePair(index,
                                                                      data));
}
void ResourceSandeshMaps::DeleteCompositeNHResourceEntry(uint32_t index) {
   compositenh_index_table_.map().erase(index);
}
