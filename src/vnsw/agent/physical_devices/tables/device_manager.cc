/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_factory.h>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <db/db.h>

#include <physical_devices/tables/device_manager.h>
#include <physical_devices/tables/physical_device.h>
#include <physical_devices/tables/physical_port.h>
#include <physical_devices/tables/logical_port.h>
#include <physical_devices/tables/physical_device_vn.h>

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
    physical_port_table_->RegisterDBClients();
    physical_device_vn_table_->RegisterDBClients();
}

void PhysicalDeviceManager::Init() {
}

void PhysicalDeviceManager::Shutdown() {
}
