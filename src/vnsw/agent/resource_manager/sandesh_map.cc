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
#include "resource_manager/mpls_index.h"

ResourceSandeshMaps::ResourceSandeshMaps() : sequence_number_(0),
    timer_processed_sequence_number_(0) {
        interface_mpls_index_map_.clear();
        vrf_mpls_index_map_.clear();
        route_mpls_index_map_.clear();
}

ResourceSandeshMaps::~ResourceSandeshMaps() {
}

void ResourceSandeshMaps::WriteToFile(ResourceBackupManager *mgr) {
    std::auto_ptr<uint8_t>write_buf;
    uint32_t write_buff_size = 0;
    uint32_t size = 0;
    int error = 0;

    InterfaceIndexResourceMapSandesh map_1;
    map_1.set_index_map(interface_mpls_index_map_);
    // this code needs to be checked for giving correct size
    size = map_1.ToString().size();
    write_buf.reset(new uint8_t [size]);
    error = 0;
    write_buff_size = map_1.WriteBinary(write_buf.get(), size, &error);
    std::stringstream interface_file_name_str;
    //TODO use the path from agent.conf
    //TODO Remove hardcoding of file name
    interface_file_name_str << "/tmp/contrail_interface_resource";
    mgr->SaveResourceDataToFile(interface_file_name_str.str(), write_buf.get(),
                                write_buff_size);

    VrfMplsResourceMapSandesh map_2;
    map_2.set_index_map(vrf_mpls_index_map_);
    // this code needs to be checked for giving correct size
    size = map_2.ToString().size();
    write_buf.reset(new uint8_t [size]);
    error = 0;
    write_buff_size = map_2.WriteBinary(write_buf.get(), size, &error);
    std::stringstream vrf_file_name_str;
    //TODO use the path from agent.conf
    //TODO Remove hardcoding of file name
    vrf_file_name_str << "/tmp/contrail_vrf_resource";
    mgr->SaveResourceDataToFile(vrf_file_name_str.str(), write_buf.get(),
                                write_buff_size);

    RouteMplsResourceMapSandesh map_3;
    map_3.set_index_map(route_mpls_index_map_);
    // this code needs to be checked for giving correct size
    size = map_3.ToString().size();
    write_buf.reset(new uint8_t [size]);
    error = 0;
    write_buff_size = map_3.WriteBinary(write_buf.get(), size, &error);
    std::stringstream route_file_name_str;
    //TODO use the path from agent.conf
    //TODO Remove hardcoding of file name
    route_file_name_str << "/tmp/contrail_route_resource";
    mgr->SaveResourceDataToFile(route_file_name_str.str(), write_buf.get(),
                                write_buff_size);
}

void ResourceSandeshMaps::ReadFromFile(ResourceBackupManager *mgr) {
    uint32_t size = 0;
    int error = 0;

    std::stringstream interface_file_name_str;
    //TODO use the path from agent.conf
    //TODO Remove hardcoding of file name
    interface_file_name_str << "/tmp/contrail_interface_resource";
    uint8_t *interface_read_buf = NULL;
    size = mgr->ReadResourceDataFromFile(interface_file_name_str.str(),
                                         &(interface_read_buf));
    InterfaceIndexResourceMapSandesh map_1;
    map_1.ReadBinary(interface_read_buf, size, &error);
    interface_mpls_index_map_ = map_1.get_index_map();
    delete interface_read_buf;

    std::stringstream vrf_file_name_str;
    uint8_t *vrf_read_buf = NULL;
    //TODO use the path from agent.conf
    //TODO Remove hardcoding of file name
    vrf_file_name_str << "/tmp/contrail_vrf_resource";
    size = mgr->ReadResourceDataFromFile(vrf_file_name_str.str(),
                                         &(vrf_read_buf));
    VrfMplsResourceMapSandesh map_2;
    map_2.ReadBinary(vrf_read_buf, size, &error);
    vrf_mpls_index_map_ = map_2.get_index_map();
    delete vrf_read_buf;

    std::stringstream route_file_name_str;
    uint8_t *route_read_buf = NULL;
    //TODO use the path from agent.conf
    //TODO Remove hardcoding of file name
    route_file_name_str << "/tmp/contrail_route_resource";
    size = mgr->ReadResourceDataFromFile(route_file_name_str.str(),
                                         &(route_read_buf));
    RouteMplsResourceMapSandesh map_3;
    map_3.ReadBinary(route_read_buf, size, &error);
    route_mpls_index_map_ = map_3.get_index_map();
    delete route_read_buf;
}

void ResourceSandeshMaps::RestoreResource(Agent *agent) {
    //Interface
    for (ResourceSandeshMaps::InterfaceIndexResourceMapIter it =
         interface_mpls_index_map_.begin();
         it != interface_mpls_index_map_.end(); it++) {
        uint32_t index = it->first;
        InterfaceIndexResource sandesh_key = it->second;
        ResourceManager::KeyPtr key(new InterfaceIndexResourceKey(agent->
                                        resource_manager(),
                                        StringToUuid(sandesh_key.get_uuid()),
                                        sandesh_key.get_type()));
        IndexResourceKey *index_resource_key = static_cast<IndexResourceKey *>
            (key.get());
        ResourceManager::DataPtr data(new IndexResourceData(agent->
                                          resource_manager(),
            static_cast<IndexResourceType*>(index_resource_key->resource_type_),
                                          index));                  
        agent->resource_manager()->EnqueueRestore(key, data);
    }

    //Vrf
    for (ResourceSandeshMaps::VrfMplsResourceMapIter it =
         vrf_mpls_index_map_.begin();
         it != vrf_mpls_index_map_.end(); it++) {
        uint32_t index = it->first;
        VrfMplsResource sandesh_key = it->second;
        ResourceManager::KeyPtr key(new VrfMplsResourceKey(agent->
                                resource_manager(), sandesh_key.get_name()));
        IndexResourceKey *index_resource_key = static_cast<IndexResourceKey *>
            (key.get());
        ResourceManager::DataPtr data(new IndexResourceData(agent->
                                          resource_manager(),
            static_cast<IndexResourceType*>(index_resource_key->resource_type_),
                                          index));                  
        agent->resource_manager()->EnqueueRestore(key, data);
    }

    //Route
    for (ResourceSandeshMaps::RouteMplsResourceMapIter it =
         route_mpls_index_map_.begin();
         it != route_mpls_index_map_.end(); it++) {
        uint32_t index = it->first;
        RouteMplsResource sandesh_key = it->second;
        ResourceManager::KeyPtr key(new RouteMplsResourceKey(agent->
                                resource_manager(), sandesh_key.get_vrf_name(),
                                sandesh_key.get_route_prefix()));
        IndexResourceKey *index_resource_key = static_cast<IndexResourceKey *>
            (key.get());
        ResourceManager::DataPtr data(new IndexResourceData(agent->
                                          resource_manager(),
            static_cast<IndexResourceType*>(index_resource_key->resource_type_),
                                          index));                  
        agent->resource_manager()->EnqueueRestore(key, data);
     }
}

void InterfaceIndexResourceMapSandesh::Process(SandeshContext*) {
}

void VrfMplsResourceMapSandesh::Process(SandeshContext*) {
}

void RouteMplsResourceMapSandesh::Process(SandeshContext*) {
}
