/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <base/util.h>

#include <ifmap/ifmap_node.h>
#include <cmn/agent_cmn.h>
#include <oper/operdb_init.h>
#include <oper/ifmap_dependency_manager.h>
#include <oper/config_manager.h>

#include <oper/interface_common.h>
#include <oper/physical_device.h>
#include <oper/physical_device_vn.h>

#include <vector>
#include <string>

using std::string;

ConfigManager::ConfigManager(Agent *agent) : agent_(agent), trigger_(NULL) {
    int task_id = TaskScheduler::GetInstance()->GetTaskId("db::DBTable");
    trigger_.reset
        (new TaskTrigger(boost::bind(&ConfigManager::Run, this), task_id, 0));
}

ConfigManager::~ConfigManager() {
}

ConfigManager::Node::Node(IFMapDependencyManager::IFMapNodePtr state) :
    state_(state) {
}

ConfigManager::Node::~Node() {
}

// Run the change-list
bool ConfigManager::Run() {
    uint32_t count = 0;
    NodeListIterator it;

    // LI has reference to VMI. So, create VMI before LI
    it = vmi_list_.begin();
    while ((count < kIterationCount) && (it != vmi_list_.end())) {
        NodeListIterator prev = it++;
        IFMapNodeState *state = prev->state_.get();
        IFMapNode *node = state->node();
        DBRequest req;
        if (agent_->interface_table()->VmiProcessConfig(node, req)) {
            agent_->interface_table()->Enqueue(&req);
        }
        vmi_list_.erase(prev);
        count++;
    }

    // Run thru changelist for LI
    it = logical_interface_list_.begin();
    while ((count < kIterationCount) && (it != logical_interface_list_.end())) {
        NodeListIterator prev = it++;
        IFMapNodeState *state = prev->state_.get();
        IFMapNode *node = state->node();
        DBRequest req;
        if (agent_->interface_table()->LogicalInterfaceProcessConfig(node, req)) {
            agent_->interface_table()->Enqueue(&req);
        }
        logical_interface_list_.erase(prev);
        count++;
    }

    // Run thru changelist for physical-device
    it = physical_device_list_.begin();
    while ((count < kIterationCount) && (it != physical_device_list_.end())) {
        NodeListIterator prev = it++;
        IFMapNodeState *state = prev->state_.get();
        IFMapNode *node = state->node();
        DBRequest req;
        if (agent_->physical_device_table()->ProcessConfig(node, req)) {
            agent_->physical_device_table()->Enqueue(&req);
        }
        physical_device_list_.erase(prev);
        count++;
    }

    // changelist for physical-device-vn entries
    PhysicalDeviceVnIterator it_dev_vn = physical_device_vn_list_.begin();
    while ((count < kIterationCount) &&
           (it_dev_vn != physical_device_vn_list_.end())) {
        PhysicalDeviceVnIterator prev = it_dev_vn++;
        PhysicalDeviceVnTable *table = agent_->physical_device_vn_table();
        table->ProcessConfig(prev->dev_, prev->vn_);
        physical_device_vn_list_.erase(prev);
        count++;
    }

    trigger_->Reset();
    if (vmi_list_.size() != 0 || logical_interface_list_.size() != 0 ||
        physical_device_vn_list_.size() != 0) {
        trigger_->Set();
        return false;
    }

    return true;
}

void ConfigManager::AddVmiNode(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    Node n(dep->SetState(node));
    vmi_list_.insert(n);
    trigger_->Set();
}

void ConfigManager::DelVmiNode(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    IFMapNodeState *state = dep->IFMapNodeGet(node);
    if (state == NULL)
        return;
    Node n(state);
    vmi_list_.erase(n);
}

uint32_t ConfigManager::VmiNodeCount() {
    return vmi_list_.size();
}

void ConfigManager::AddLogicalInterfaceNode(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    Node n(dep->SetState(node));
    logical_interface_list_.insert(n);
    trigger_->Set();
}

void ConfigManager::DelLogicalInterfaceNode(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    IFMapNodeState *state = dep->IFMapNodeGet(node);
    if (state == NULL)
        return;
    Node n(state);
    logical_interface_list_.erase(n);
}

uint32_t ConfigManager::LogicalInterfaceNodeCount() {
    return logical_interface_list_.size();
}

void ConfigManager::AddPhysicalDeviceNode(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    Node n(dep->SetState(node));
    physical_device_list_.insert(n);
    trigger_->Set();
}

void ConfigManager::DelPhysicalDeviceNode(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    IFMapNodeState *state = dep->IFMapNodeGet(node);
    if (state == NULL)
        return;
    Node n(state);
    physical_device_list_.erase(n);
}

uint32_t ConfigManager::PhysicalDeviceNodeCount() {
    return physical_device_list_.size();
}

ConfigManager::PhysicalDeviceVnEntry::PhysicalDeviceVnEntry
    (const boost::uuids::uuid &dev, const boost::uuids::uuid &vn) :
        dev_(dev), vn_(vn) {
}

void ConfigManager::AddPhysicalDeviceVn(const boost::uuids::uuid &dev,
                                        const boost::uuids::uuid &vn) {
    physical_device_vn_list_.insert(PhysicalDeviceVnEntry(dev, vn));
    trigger_->Set();
}

void ConfigManager::DelPhysicalDeviceVn(const boost::uuids::uuid &dev,
                                        const boost::uuids::uuid &vn) {
    physical_device_vn_list_.erase(PhysicalDeviceVnEntry(dev, vn));
}

uint32_t ConfigManager::PhysicalDeviceVnCount() {
    return physical_device_vn_list_.size();
}
