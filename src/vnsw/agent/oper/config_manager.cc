/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <base/util.h>
#include <db/db_partition.h>

#include <ifmap/ifmap_node.h>
#include <cmn/agent_cmn.h>
#include <oper/operdb_init.h>
#include <oper/ifmap_dependency_manager.h>
#include <oper/config_manager.h>

#include <oper/interface_common.h>
#include <oper/physical_device.h>
#include <oper/physical_device_vn.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/sg.h>
#include <oper/vm.h>

#include <vector>
#include <string>

using std::string;

ConfigManager::ConfigManager(Agent *agent) : agent_(agent), trigger_(NULL),
    timer_(NULL), timeout_(kMinTimeout), timer_count_(0) {
    int task_id = TaskScheduler::GetInstance()->GetTaskId("db::DBTable");
    trigger_.reset
        (new TaskTrigger(boost::bind(&ConfigManager::TriggerRun, this),
                         task_id, 0));
    timer_ = TimerManager::CreateTimer(*(agent->event_manager()->io_service()),
                                       "Config Manager", task_id, 0);
    for (uint32_t i = 0; i < kMaxTimeout; i++) {
        process_config_count_[i] = 0;
    }
}

ConfigManager::~ConfigManager() {
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
}

ConfigManager::Node::Node(IFMapDependencyManager::IFMapNodePtr state) :
    state_(state) {
}

ConfigManager::Node::~Node() {
}

int ConfigManager::Size() {
    return
        (sg_list_.size() +
         physical_interface_list_.size() +
         vn_list_.size() +
         vrf_list_.size() +
         vm_list_.size() +
         logical_interface_list_.size() +
         vmi_list_.size() +
         physical_device_list_.size() +
         physical_device_vn_list_.size());
}

void ConfigManager::Start() {
    if (agent_->test_mode()) {
        trigger_->Set();
    } else {
        timeout_++;
        if (timeout_ > kMaxTimeout)
            timeout_ = kMaxTimeout;
        if (timer_->Idle())
            timer_->Start(timeout_, boost::bind(&ConfigManager::TimerRun,
                                                this));
    }
}

bool ConfigManager::TimerRun() {
    int count = Run();
    process_config_count_[timeout_] += count;
    timer_count_++;
    if (Size() == 0) {
        timeout_ = kMinTimeout;
        return false;
    }

    timeout_--;
    if (timeout_ <= kMinTimeout)
        timeout_ = kMinTimeout;
    timer_->Reschedule(timeout_);
    return true;
}

bool ConfigManager::TriggerRun() {
    Run();
    return (Size() == 0);
}

// Run the change-list
int ConfigManager::Run() {
    uint32_t count = 0;
    NodeListIterator it;

    it = sg_list_.begin();
    while ((count < kIterationCount) && (it != sg_list_.end())) {
        NodeListIterator prev = it++;
        IFMapNodeState *state = prev->state_.get();
        IFMapNode *node = state->node();
        DBRequest req;
        if (agent_->sg_table()->ProcessConfig(node, req)) {
            agent_->sg_table()->Enqueue(&req);
        }
        sg_list_.erase(prev);
        count++;
    }

    it = physical_interface_list_.begin();
    while ((count < kIterationCount) && (it != physical_interface_list_.end())) {
        NodeListIterator prev = it++;
        IFMapNodeState *state = prev->state_.get();
        IFMapNode *node = state->node();
        DBRequest req;
        if (agent_->interface_table()->PhysicalInterfaceProcessConfig(node, req)) {
            agent_->interface_table()->Enqueue(&req);
        }
        physical_interface_list_.erase(prev);
        count++;
    }

    it = vn_list_.begin();
    while ((count < kIterationCount) && (it != vn_list_.end())) {
        NodeListIterator prev = it++;
        IFMapNodeState *state = prev->state_.get();
        IFMapNode *node = state->node();
        DBRequest req;
        if (agent_->vn_table()->ProcessConfig(node, req)) {
            agent_->vn_table()->Enqueue(&req);
        }
        vn_list_.erase(prev);
        count++;
    }

    it = vm_list_.begin();
    while ((count < kIterationCount) && (it != vm_list_.end())) {
        NodeListIterator prev = it++;
        IFMapNodeState *state = prev->state_.get();
        IFMapNode *node = state->node();
        DBRequest req;
        if (agent_->vm_table()->ProcessConfig(node, req)) {
            agent_->vm_table()->Enqueue(&req);
        }
        vm_list_.erase(prev);
        count++;
    }

    it = vrf_list_.begin();
    while ((count < kIterationCount) && (it != vrf_list_.end())) {
        NodeListIterator prev = it++;
        IFMapNodeState *state = prev->state_.get();
        IFMapNode *node = state->node();
        DBRequest req;
        if (agent_->vrf_table()->ProcessConfig(node, req)) {
            agent_->vrf_table()->Enqueue(&req);
        }
        vrf_list_.erase(prev);
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

    return count;
}

void ConfigManager::AddVmiNode(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    Node n(dep->SetState(node));
    vmi_list_.insert(n);
    Start();
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
    Start();
}

uint32_t ConfigManager::LogicalInterfaceNodeCount() {
    return logical_interface_list_.size();
}

void ConfigManager::AddPhysicalDeviceNode(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    Node n(dep->SetState(node));
    physical_device_list_.insert(n);
    Start();
}

ConfigManager::PhysicalDeviceVnEntry::PhysicalDeviceVnEntry
    (const boost::uuids::uuid &dev, const boost::uuids::uuid &vn) :
        dev_(dev), vn_(vn) {
}

void ConfigManager::AddPhysicalDeviceVn(const boost::uuids::uuid &dev,
                                        const boost::uuids::uuid &vn) {
    physical_device_vn_list_.insert(PhysicalDeviceVnEntry(dev, vn));
    Start();
}

void ConfigManager::DelPhysicalDeviceVn(const boost::uuids::uuid &dev,
                                        const boost::uuids::uuid &vn) {
    physical_device_vn_list_.erase(PhysicalDeviceVnEntry(dev, vn));
}

uint32_t ConfigManager::PhysicalDeviceVnCount() {
    return physical_device_vn_list_.size();
}

void ConfigManager::AddSgNode(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    Node n(dep->SetState(node));
    sg_list_.insert(n);
    Start();
}

void ConfigManager::AddVnNode(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    Node n(dep->SetState(node));
    vn_list_.insert(n);
    Start();
}

void ConfigManager::AddVrfNode(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    Node n(dep->SetState(node));
    vrf_list_.insert(n);
    Start();
}

void ConfigManager::AddVmNode(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    Node n(dep->SetState(node));
    vm_list_.insert(n);
    Start();
}

void ConfigManager::AddPhysicalInterfaceNode(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    Node n(dep->SetState(node));
    physical_interface_list_.insert(n);
    Start();
}
