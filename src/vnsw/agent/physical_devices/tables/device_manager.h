/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_DEVICE_MANAGER_H_
#define SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_DEVICE_MANAGER_H_

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>

namespace AGENT {
class PhysicalDeviceTable;
class PhysicalPortTable;
class LogicalPortTable;
class PhysicalDeviceVnTable;
}

class PhysicalDeviceManager {
 public:
    explicit PhysicalDeviceManager(Agent *agent) : agent_(agent) { }
    virtual ~PhysicalDeviceManager() { }

    void CreateDBTables(DB* db);
    void RegisterDBClients();
    void Init();
    void Shutdown();

    Agent *agent() const { return agent_; }
    AGENT::PhysicalDeviceTable *device_table() const { return device_table_; }
    AGENT::PhysicalPortTable *physical_port_table() const {
        return physical_port_table_;
    }
    AGENT::LogicalPortTable *logical_port_table() const {
        return logical_port_table_;
    }
    AGENT::PhysicalDeviceVnTable *physical_device_vn_table() const {
        return physical_device_vn_table_;
    }
 private:
    Agent *agent_;
    AGENT::PhysicalDeviceTable *device_table_;
    AGENT::PhysicalPortTable *physical_port_table_;
    AGENT::LogicalPortTable *logical_port_table_;
    AGENT::PhysicalDeviceVnTable *physical_device_vn_table_;

    DISALLOW_COPY_AND_ASSIGN(PhysicalDeviceManager);
};

#endif  // SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_DEVICE_MANAGER_H_
