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
#include "resource_manager/resource_type.h"
#include "resource_manager/sandesh_map.h"
#include "resource_manager/resource_manager.h"
#include "resource_manager/resource_manager_types.h"
#include "resource_manager/mpls_index.h"
#include <boost/filesystem.hpp>

SandeshResourceType::SandeshResourceType(ResourceBackupManager *manager,
                                         const std::string &name) :
    backup_manager_(manager), agent_(manager->agent()), name_(name) {
    backup_dir_ = agent_->params()->restart_backup_dir();
    boost::filesystem::path dir(backup_dir_.c_str());
	if (!boost::filesystem::exists(dir))
    	boost::filesystem::create_directory(dir);
    timer_ = TimerManager::CreateTimer(*(agent_->event_manager()->io_service()),
                                       name);
    StartTimer();
}

SandeshResourceType::~SandeshResourceType() {
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
}

bool SandeshResourceType::TimerExpiry() {
    WriteToFile();
    return true;
}

void SandeshResourceType::StartTimer() {
    timer_->Start(kResourceSyncTimeout,
                  boost::bind(&SandeshResourceType::TimerExpiry, this));
}

void SandeshResourceType::TriggerBackup() {
    if (timer_->running() == false)
        StartTimer();

}

VrfMplsSandeshResourceType::VrfMplsSandeshResourceType
(ResourceBackupManager *manager) :
    SandeshResourceType(manager, "VrfMplsSandeshResourceType") {
        map_.clear();
}

VrfMplsSandeshResourceType::~VrfMplsSandeshResourceType() {
}

void VrfMplsSandeshResourceType::WriteToFile() {
    tbb::mutex::scoped_lock lock(mutex_);
    std::auto_ptr<uint8_t>write_buf;
    uint32_t write_buff_size = 0;
    uint32_t size = 0;
    int error = 0;

    VrfMplsResourceMapSandesh map;
    map.set_index_map(map_);
    // this code needs to be checked for giving correct size
    size = map.ToString().size();
    write_buf.reset(new uint8_t [size]);
    error = 0;
    write_buff_size = map.WriteBinary(write_buf.get(), size, &error);
    std::stringstream vrf_file_name_str;
    //TODO use the path from agent.conf
    //TODO Remove hardcoding of file name
    vrf_file_name_str << backup_dir_ <<"/contrail_vrf_resource";
    backup_manager()->SaveResourceDataToFile(vrf_file_name_str.str(),
                                             write_buf.get(),
                                             write_buff_size);
}

void VrfMplsSandeshResourceType::ReadFromFile() {
    tbb::mutex::scoped_lock lock(mutex_);
    uint32_t size = 0;
    int error = 0;

    std::stringstream vrf_file_name_str;
    uint8_t *vrf_read_buf = NULL;
    //TODO use the path from agent.conf
    //TODO Remove hardcoding of file name
    vrf_file_name_str << backup_dir_ <<"/contrail_vrf_resource";
    size = backup_manager()->ReadResourceDataFromFile(vrf_file_name_str.str(),
                                 &(vrf_read_buf));
    if (vrf_read_buf) {
        if (size) {
            VrfMplsResourceMapSandesh map;
            map.ReadBinary(vrf_read_buf, size, &error);
            map_ = map.get_index_map();
        }
        delete vrf_read_buf;
    }

}

void VrfMplsSandeshResourceType::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        VrfMplsResource sandesh_key = it->second;
        ResourceManager::KeyPtr key(new VrfMplsResourceKey(backup_manager()->
                                resource_manager(), sandesh_key.get_name()));
        IndexResourceKey *index_resource_key = static_cast<IndexResourceKey *>
            (key.get());
        ResourceManager::DataPtr data(new IndexResourceData(backup_manager()->
                                          resource_manager(),
            static_cast<IndexResourceType*>(index_resource_key->resource_type_),
                                          index));                  
        backup_manager()->resource_manager()->EnqueueRestore(key, data);
    }

}

RouteMplsSandeshResourceType::RouteMplsSandeshResourceType
(ResourceBackupManager *manager) :
    SandeshResourceType(manager, "RouteMplsSandeshResourceType") {
        map_.clear();
}

RouteMplsSandeshResourceType::~RouteMplsSandeshResourceType() {
}

void RouteMplsSandeshResourceType::WriteToFile() {
    tbb::mutex::scoped_lock lock(mutex_);
    std::auto_ptr<uint8_t>write_buf;
    uint32_t write_buff_size = 0;
    uint32_t size = 0;
    int error = 0;

    RouteMplsResourceMapSandesh map;
    map.set_index_map(map_);
    // this code needs to be checked for giving correct size
    size = map.ToString().size();
    write_buf.reset(new uint8_t [size]);
    error = 0;
    write_buff_size = map.WriteBinary(write_buf.get(), size, &error);
    std::stringstream file_name_str;
    //TODO use the path from agent.conf
    //TODO Remove hardcoding of file name
    file_name_str << backup_dir_<< "/contrail_route_resource";
    backup_manager()->SaveResourceDataToFile(file_name_str.str(),
                                             write_buf.get(),
                                             write_buff_size);
}

void RouteMplsSandeshResourceType::ReadFromFile() {
    tbb::mutex::scoped_lock lock(mutex_);
    uint32_t size = 0;
    int error = 0;
    std::stringstream route_file_name_str;
    uint8_t *route_read_buf = NULL;

    //TODO use the path from agent.conf
    //TODO Remove hardcoding of file name
    route_file_name_str << backup_dir_ <<"/contrail_route_resource";
    size = backup_manager()->
        ReadResourceDataFromFile(route_file_name_str.str(),
                                 &(route_read_buf));
    if (route_read_buf) {
        if (size) {
            RouteMplsResourceMapSandesh map;
            map.ReadBinary(route_read_buf, size, &error);
            map_ = map.get_index_map();
        }
        delete route_read_buf;
    }
}

void RouteMplsSandeshResourceType::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        RouteMplsResource sandesh_key = it->second;
        ResourceManager::KeyPtr key(new RouteMplsResourceKey(backup_manager()->
                                resource_manager(), sandesh_key.get_vrf_name(),
                                sandesh_key.get_route_prefix()));
        IndexResourceKey *index_resource_key = static_cast<IndexResourceKey *>
            (key.get());
        ResourceManager::DataPtr data(new IndexResourceData(backup_manager()->
                                          resource_manager(),
            static_cast<IndexResourceType*>(index_resource_key->resource_type_),
                                          index));                  
        backup_manager()->resource_manager()->EnqueueRestore(key, data);
     }
}

InterfaceMplsSandeshResourceType::InterfaceMplsSandeshResourceType
(ResourceBackupManager *manager) :
    SandeshResourceType(manager, "InterfaceMplsSandeshResourceType") {
        map_.clear();
}

InterfaceMplsSandeshResourceType::~InterfaceMplsSandeshResourceType() {
}

void InterfaceMplsSandeshResourceType::WriteToFile() {
    tbb::mutex::scoped_lock lock(mutex_);
    std::auto_ptr<uint8_t>write_buf;
    uint32_t write_buff_size = 0;
    uint32_t size = 0;
    int error = 0;

    InterfaceIndexResourceMapSandesh map;
    map.set_index_map(map_);
    // this code needs to be checked for giving correct size
    size = map.ToString().size();
    write_buf.reset(new uint8_t [size]);
    error = 0;
    write_buff_size = map.WriteBinary(write_buf.get(), size, &error);
    std::stringstream file_name_str;
    //TODO use the path from agent.conf
    //TODO Remove hardcoding of file name
    file_name_str << backup_dir_ <<"/contrail_interface_resource";
    backup_manager()->SaveResourceDataToFile(file_name_str.str(),
                                             write_buf.get(),
                                             write_buff_size);
}

void InterfaceMplsSandeshResourceType::ReadFromFile() {
    tbb::mutex::scoped_lock lock(mutex_);
    uint32_t size = 0;
    int error = 0;
    std::stringstream interface_file_name_str;

    //TODO use the path from agent.conf
    //TODO Remove hardcoding of file name
    interface_file_name_str << backup_dir_ <<"/contrail_interface_resource";
    uint8_t *interface_read_buf = NULL;
    size = backup_manager()->
        ReadResourceDataFromFile(interface_file_name_str.str(),
                                 &(interface_read_buf));
    if (interface_read_buf) {
        if (size) {
            InterfaceIndexResourceMapSandesh map;
            map.ReadBinary(interface_read_buf, size, &error);
            map_ = map.get_index_map();
        }
        delete interface_read_buf;
    }

}

void InterfaceMplsSandeshResourceType::RestoreResource() {
    for (MapIter it = map_.begin(); it != map_.end(); it++) {
        uint32_t index = it->first;
        InterfaceIndexResource sandesh_key = it->second;
        MacAddress mac = MacAddress::FromString(sandesh_key.get_mac());
        ResourceManager::KeyPtr key(new InterfaceIndexResourceKey
                                    (backup_manager()->resource_manager(),
                                     StringToUuid(sandesh_key.get_uuid()),
                                     mac, sandesh_key.get_policy(),
                                     sandesh_key.get_type()));
        IndexResourceKey *index_resource_key = static_cast<IndexResourceKey *>
            (key.get());
        ResourceManager::DataPtr data(new IndexResourceData(backup_manager()->
                                          resource_manager(),
            static_cast<IndexResourceType*>(index_resource_key->resource_type_),
                                          index));                  
        backup_manager()->resource_manager()->EnqueueRestore(key, data);
    }
}

ResourceSandeshMaps::ResourceSandeshMaps(ResourceBackupManager *manager) :
    interface_mpls_index_map_(manager), vrf_mpls_index_map_(manager),
    route_mpls_index_map_(manager), backup_manager_(manager),
    agent_(manager->agent()) {
}

ResourceSandeshMaps::~ResourceSandeshMaps() {
}

void ResourceSandeshMaps::ReadFromFile() {
    interface_mpls_index_map_.ReadFromFile();
    vrf_mpls_index_map_.ReadFromFile();
    route_mpls_index_map_.ReadFromFile();
}

void ResourceSandeshMaps::EndOfBackup() {
    ResourceManager::KeyPtr key
        (new ResourceBackupEndKey(agent_->resource_manager()));
    ResourceManager::DataPtr data;
    agent_->resource_manager()->EnqueueRestore(key, data);
}

void ResourceSandeshMaps::RestoreResource() {
    interface_mpls_index_map_.RestoreResource();
    vrf_mpls_index_map_.RestoreResource();
    route_mpls_index_map_.RestoreResource();
    EndOfBackup();
}

void InterfaceIndexResourceMapSandesh::Process(SandeshContext*) {
}

void VrfMplsResourceMapSandesh::Process(SandeshContext*) {
}

void RouteMplsResourceMapSandesh::Process(SandeshContext*) {
}
