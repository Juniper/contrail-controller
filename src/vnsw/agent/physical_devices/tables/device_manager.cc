/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <db/db.h>
#include <cmn/agent_factory.h>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <oper/operdb_init.h>
#include <oper/ifmap_dependency_manager.h>

#include <physical_devices/tables/device_manager.h>
#include <physical_devices/tables/physical_device.h>
#include <physical_devices/tables/physical_port.h>
#include <physical_devices/tables/logical_port.h>
#include <physical_devices/tables/physical_device_vn.h>

using boost::assign::map_list_of;
using boost::assign::list_of;
using AGENT::PhysicalDeviceTable;
using AGENT::PhysicalPortTable;
using AGENT::LogicalPortTable;
using AGENT::PhysicalDeviceVnTable;

SandeshTraceBufferPtr
PhysicalDeviceManagerTraceBuf(SandeshTraceBufferCreate("Device Manager", 500));

void PhysicalDeviceManager::CreateDBTables(DB *db) {
    DB::RegisterFactory("db.physical_devices.0",
                        &PhysicalDeviceTable::CreateTable);
    DB::RegisterFactory("db.physical_port.0", &PhysicalPortTable::CreateTable);
    DB::RegisterFactory("db.logical_port.0", &LogicalPortTable::CreateTable);
    DB::RegisterFactory("db.physical_device_vn.0",
                        &PhysicalDeviceVnTable::CreateTable);

    device_table_ = static_cast<PhysicalDeviceTable *>
        (db->CreateTable("db.physical_devices.0"));
    assert(device_table_);
    device_table_->set_agent(agent_);

    physical_port_table_ = static_cast<PhysicalPortTable *>
        (db->CreateTable("db.physical_port.0"));
    assert(physical_port_table_);
    physical_port_table_->set_agent(agent_);

    logical_port_table_ = static_cast<LogicalPortTable *>
        (db->CreateTable("db.logical_port.0"));
    assert(logical_port_table_);
    logical_port_table_->set_agent(agent_);

    physical_device_vn_table_ = static_cast<PhysicalDeviceVnTable *>
        (db->CreateTable("db.physical_device_vn.0"));
    assert(physical_device_vn_table_);
    physical_device_vn_table_->set_agent(agent_);
}

void PhysicalDeviceManager::RegisterDBClients() {
    typedef IFMapDependencyTracker::PropagateList PropagateList;
    typedef IFMapDependencyTracker::ReactionMap ReactionMap;

    IFMapDependencyManager *mgr = agent_->oper_db()->dependency_manager();

    ReactionMap device_react = map_list_of<std::string, PropagateList>
        ("virtual-network", list_of("self"))
        ("self", list_of("self"));
    mgr->RegisterReactionMap("physical-router", device_react);

    ReactionMap physical_port_react = map_list_of<std::string, PropagateList>
        ("physical-router", list_of("self"))
        ("self", list_of("self"));
    mgr->RegisterReactionMap("physical-interface", physical_port_react);

    ReactionMap logical_port_react = map_list_of<std::string, PropagateList>
        ("physical-interface", list_of("self"))
        ("self", list_of("self"));
    mgr->RegisterReactionMap("logical-interface", logical_port_react);

    device_table_->RegisterDBClients(mgr);
    physical_port_table_->RegisterDBClients(mgr);
    logical_port_table_->RegisterDBClients(mgr);
    physical_device_vn_table_->RegisterDBClients(mgr);
}

void PhysicalDeviceManager::Init() {
}

void PhysicalDeviceManager::Shutdown() {
}
