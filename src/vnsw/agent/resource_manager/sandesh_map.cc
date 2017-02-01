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

BackUpResourceTable::BackUpResourceTable(ResourceBackupManager *manager,
                                         const std::string &name) :
    backup_manager_(manager), agent_(manager->agent()), name_(name),
    last_modified_time_(UTCTimestampUsec()), audit_required_(true) {
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
    // temporary Code to trigger Audit
    if (audit_required_ == true) {
        audit_required_ = false;
        agent_->resource_manager()->Audit();
    }
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

VrfMplsBackUpResourceTable::VrfMplsBackUpResourceTable
(ResourceBackupManager *manager) :
    BackUpResourceTable(manager, "VrfMplsBackUpResourceTable"),
    vrf_file_name_str_(FilePath("/contrail_vrf_resource")) {
}

VrfMplsBackUpResourceTable::~VrfMplsBackUpResourceTable() {
}

bool VrfMplsBackUpResourceTable::WriteToFile() {
    std::auto_ptr<uint8_t>write_buf;
    uint32_t write_buff_size = 0;
    uint32_t size = 0;
    int error = 0;

    VrfMplsResourceMapSandesh map;
    map.set_index_map(map_);
    map.set_time_stamp(UTCTimestampUsec());
    // calculating some heuristic size for the buffer
    // Can be enhanced by giving the Stream as argument to WriteBinary
    size = map_.size() * kVrfMplsRecordSize + kSandeshMetaDataSize;
    write_buf.reset(new uint8_t [size]);
    error = 0;
    write_buff_size = map.WriteBinary(write_buf.get(), size, &error);
    if (error != 0) {
        //calculating size by encoding the Sandesh structure
        size = map.ToString().size();
        write_buf.reset(new uint8_t [size]);
        write_buff_size = map.WriteBinary(write_buf.get(), size, &error);
        if (error != 0) {
            LOG(ERROR, "Sandesh Write Binary failed ");
            return false;
        }
    }

    return backup_manager()->SaveResourceDataToFile(vrf_file_name_str_,
                                                    write_buf.get(),
                                                    write_buff_size);
}

void VrfMplsBackUpResourceTable::ReadFromFile() {
    uint32_t size = 0;
    int error = 0;
    uint8_t *vrf_read_buf = NULL;
    size = backup_manager()->ReadResourceDataFromFile(vrf_file_name_str_,
                                 &(vrf_read_buf));
    if (vrf_read_buf) {
        if (size) {
            VrfMplsResourceMapSandesh map;
            map.ReadBinary(vrf_read_buf, size, &error);
            if (error != 0) {
                LOG(ERROR, "Sandesh Read Binary failed ");
                delete vrf_read_buf;
                return;
            }
            map_ = map.get_index_map();
        }
        delete vrf_read_buf;
    }
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
    route_file_name_str_(FilePath("/contrail_route_resource")){
}

RouteMplsBackUpResourceTable::~RouteMplsBackUpResourceTable() {
}

bool  RouteMplsBackUpResourceTable::WriteToFile() {
    std::auto_ptr<uint8_t>write_buf;
    uint32_t write_buff_size = 0;
    uint32_t size = 0;
    int error = 0;

    RouteMplsResourceMapSandesh map;
    map.set_index_map(map_);
    map.set_time_stamp(UTCTimestampUsec());
    // calculating some heuristic size for the buffer
    // Can be enhanced by giving the Stream as argument to WriteBinary
    size = map_.size() * KRouteMplsRecordSize + kSandeshMetaDataSize;
    write_buf.reset(new uint8_t [size]);
    error = 0;
    write_buff_size = map.WriteBinary(write_buf.get(), size, &error);
    if (error != 0) {
        //Calculating size by encoding Sandesh structure again
        size = map.ToString().size();
        write_buf.reset(new uint8_t [size]);
        write_buff_size = map.WriteBinary(write_buf.get(), size, &error);
        if (error != 0) {
            LOG(ERROR, "Sandesh Write Binary failed ");
            return false;
        }
    }
    
    return backup_manager()->SaveResourceDataToFile(route_file_name_str_,
                                                    write_buf.get(),
                                                    write_buff_size);
}

void RouteMplsBackUpResourceTable::ReadFromFile() {
    uint32_t size = 0;
    int error = 0;
    uint8_t *route_read_buf = NULL;
    size = backup_manager()->
        ReadResourceDataFromFile(route_file_name_str_,
                                 &(route_read_buf));
    if (route_read_buf) {
        if (size) {
            RouteMplsResourceMapSandesh map;
            map.ReadBinary(route_read_buf, size, &error);
            if (error != 0) {
                LOG(ERROR, "Sandesh Read Binary failed ");
                delete route_read_buf;
                return;
            }
            map_ = map.get_index_map();
        }
        delete route_read_buf;
    }
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
    std::auto_ptr<uint8_t>write_buf;
    uint32_t write_buff_size = 0;
    uint32_t size = 0;
    int error = 0;

    InterfaceIndexResourceMapSandesh map;
    map.set_index_map(map_);
    map.set_time_stamp(UTCTimestampUsec());
    // calculating some heuristic size for the buffer
    // Can be enhanced by giving the Stream as argument to WriteBinary
    size = map_.size() * KInterfaceMplsRecordSize + kSandeshMetaDataSize;
    write_buf.reset(new uint8_t [size]);
    error = 0;
    write_buff_size = map.WriteBinary(write_buf.get(), size, &error);
    if (error != 0) {
        //Calculating size by encoding Sandesh structure again
        size = map.ToString().size();
        write_buf.reset(new uint8_t [size]);
        write_buff_size = map.WriteBinary(write_buf.get(), size, &error);
        if (error != 0) {
            LOG(ERROR, "Sandesh Write Binary failed ");
            return false;
        }
    }

    return backup_manager()->SaveResourceDataToFile(interface_file_name_str_,
                                                    write_buf.get(),
                                                    write_buff_size);
}

void InterfaceMplsBackUpResourceTable::ReadFromFile() {
    uint32_t size = 0;
    int error = 0;
    uint8_t *interface_read_buf = NULL;
    size = backup_manager()->
        ReadResourceDataFromFile(interface_file_name_str_,
                                 &(interface_read_buf));
    if (interface_read_buf) {
        if (size) {
            InterfaceIndexResourceMapSandesh map;
            map.ReadBinary(interface_read_buf, size, &error);
            if (error != 0) {
                LOG(ERROR, "Sandesh Read Binary failed ");
                delete interface_read_buf;
                return;
            }
            map_ = map.get_index_map();
        }
        delete interface_read_buf;
    }

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
