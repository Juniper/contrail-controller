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
#include <oper/health_check.h>
#include <oper/physical_device.h>
#include <oper/physical_device_vn.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/sg.h>
#include <oper/vm.h>
#include <oper/interface_common.h>

#include <vector>
#include <string>

using std::string;

class ConfigManagerNodeList {
public:
    struct Node {
        Node(IFMapDependencyManager::IFMapNodePtr state) : state_(state) { }
        ~Node() { }

        IFMapDependencyManager::IFMapNodePtr state_;
    };

    struct NodeCmp {
        bool operator() (const Node &lhs, const Node &rhs) {
            return lhs.state_.get() < rhs.state_.get();
        }
    };
    typedef std::set<Node, NodeCmp> NodeList;
    typedef NodeList::iterator NodeListIterator;

    ConfigManagerNodeList(AgentDBTable *table) :
        table_(table), enqueue_count_(0), process_count_(0) {
    }

    ~ConfigManagerNodeList() {
        assert(list_.size() == 0);
    }

    bool Add(Agent *agent, ConfigManager *mgr, IFMapNode *node) {
        IFMapDependencyManager *dep = agent->oper_db()->dependency_manager();
        Node n(dep->SetState(node));
        list_.insert(n);
        enqueue_count_++;
        mgr->Start();
        return true;
    }

    bool Delete(Agent *agent, ConfigManager *mgr, IFMapNode *node) {
        IFMapDependencyManager *dep = agent->oper_db()->dependency_manager();
        IFMapNodeState *state = dep->IFMapNodeGet(node);
        if (state == NULL)
            return false;
        Node n(state);
        list_.erase(n);
        return true;
    }

    uint32_t Process(uint32_t weight) {
        uint32_t count = 0;
        NodeListIterator it = list_.begin();
        while (weight && (it != list_.end())) {
            NodeListIterator prev = it++;
            IFMapNodeState *state = prev->state_.get();
            IFMapNode *node = state->node();

            DBRequest req;
            boost::uuids::uuid id = state->uuid();
            if (table_->ProcessConfig(node, req, id)) {
                table_->Enqueue(&req);
            }
            list_.erase(prev);
            weight--;
            count++;
            process_count_++;
        }

        return count;
    }

    uint32_t Size() const { return list_.size(); }
    uint32_t enqueue_count() const { return enqueue_count_; }
    uint32_t process_count() const { return process_count_; }

private:
    AgentDBTable *table_;
    NodeList list_;
    uint32_t enqueue_count_;
    uint32_t process_count_;
    DISALLOW_COPY_AND_ASSIGN(ConfigManagerNodeList);
};

class ConfigManagerDeviceVnList {
public:
    struct DeviceVnEntry {
        DeviceVnEntry(const boost::uuids::uuid &dev,
                      const boost::uuids::uuid &vn) : dev_(dev), vn_(vn) {
        }

        ~DeviceVnEntry() { }

        boost::uuids::uuid dev_;
        boost::uuids::uuid vn_;
    };

    struct DeviceVnEntryCmp {
        bool operator() (const DeviceVnEntry &lhs, const DeviceVnEntry &rhs) {
            if (lhs.dev_ != rhs.dev_)
                return lhs.dev_ < rhs.dev_;

            return lhs.vn_ < rhs.vn_;
        }
    };

    typedef std::set<DeviceVnEntry, DeviceVnEntryCmp> DeviceVnList;
    typedef DeviceVnList::iterator DeviceVnIterator;

    ConfigManagerDeviceVnList(PhysicalDeviceVnTable *table) :
        table_(table), enqueue_count_(0), process_count_(0) {
    }

    ~ConfigManagerDeviceVnList() {
        assert(list_.size() == 0);
    }

    bool Add(Agent *agent, ConfigManager *mgr, const boost::uuids::uuid &dev,
             const boost::uuids::uuid &vn) {
        list_.insert(DeviceVnEntry(dev, vn));
        enqueue_count_++;
        mgr->Start();
        return true;
    }

    bool Delete(Agent *agent, ConfigManager *mgr, const boost::uuids::uuid &dev,
                const boost::uuids::uuid &vn) {
        list_.erase(DeviceVnEntry(dev, vn));
        return true;
    }

    uint32_t Process(uint32_t weight) {
        uint32_t count = 0;
        DeviceVnIterator it = list_.begin();
        while (weight && (it != list_.end())) {
            DeviceVnIterator prev = it++;
            DBRequest req;
            table_->ProcessConfig(prev->dev_, prev->vn_);
            list_.erase(prev);
            weight--;
            count++;
        }
        return count;
    }

    uint32_t Size() const { return list_.size(); }
    uint32_t enqueue_count() const { return enqueue_count_; }
    uint32_t process_count() const { return process_count_; }

private:
    PhysicalDeviceVnTable *table_;
    DeviceVnList list_;
    uint32_t enqueue_count_;
    uint32_t process_count_;
    DISALLOW_COPY_AND_ASSIGN(ConfigManagerDeviceVnList);
};

ConfigManager::ConfigManager(Agent *agent) :
    agent_(agent), trigger_(NULL), timer_(NULL), timeout_(kMinTimeout) {

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

void ConfigManager::Init() {
    AgentDBTable *intf_table = agent_->interface_table();
    vmi_list_.reset(new ConfigManagerNodeList(intf_table));
    physical_interface_list_.reset(new ConfigManagerNodeList(intf_table));
    logical_interface_list_.reset(new ConfigManagerNodeList(intf_table));

    device_list_.reset(new ConfigManagerNodeList
                       (agent_->physical_device_table()));
    sg_list_.reset(new ConfigManagerNodeList(agent_->sg_table()));
    vn_list_.reset(new ConfigManagerNodeList(agent_->vn_table()));
    vrf_list_.reset(new ConfigManagerNodeList(agent_->vrf_table()));
    vm_list_.reset(new ConfigManagerNodeList(agent_->vm_table()));
    hc_list_.reset(new ConfigManagerNodeList
                       (agent_->health_check_table()));
    device_vn_list_.reset(new ConfigManagerDeviceVnList
                          (agent_->physical_device_vn_table()));
}

uint32_t ConfigManager::Size() const {
    return
        vmi_list_->Size() +
        physical_interface_list_->Size() +
        logical_interface_list_->Size() +
        device_list_->Size() +
        sg_list_->Size() +
        vn_list_->Size() +
        vrf_list_->Size() +
        vm_list_->Size() +
        hc_list_->Size() +
        device_vn_list_->Size();
}

uint32_t ConfigManager::ProcessCount() const {
    return
        vmi_list_->process_count() +
        physical_interface_list_->process_count() +
        logical_interface_list_->process_count() +
        device_list_->process_count() +
        sg_list_->process_count() +
        vn_list_->process_count() +
        vrf_list_->process_count() +
        vm_list_->process_count() +
        hc_list_->process_count() +
        device_vn_list_->process_count();
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
    uint32_t max_count = kIterationCount;
    uint32_t count = 0;

    count += sg_list_->Process(max_count - count);
    count += physical_interface_list_->Process(max_count - count);
    count += vn_list_->Process(max_count - count);
    count += vm_list_->Process(max_count - count);
    count += vrf_list_->Process(max_count - count);
    count += logical_interface_list_->Process(max_count - count);
    count += vmi_list_->Process(max_count - count);
    count += device_list_->Process(max_count - count);
    count += hc_list_->Process(max_count - count);
    count += device_vn_list_->Process(max_count - count);
    return count;
}

void ConfigManager::AddVmiNode(IFMapNode *node) {
    vmi_list_->Add(agent_, this, node);
}

uint32_t ConfigManager::VmiNodeCount() const {
    return vmi_list_->Size();
}

void ConfigManager::DelVmiNode(IFMapNode *node) {
    vmi_list_->Delete(agent_, this, node);
}

void ConfigManager::AddLogicalInterfaceNode(IFMapNode *node) {
    logical_interface_list_->Add(agent_, this, node);
}

uint32_t ConfigManager::LogicalInterfaceNodeCount() const {
    return logical_interface_list_->Size();
}

void ConfigManager::AddPhysicalDeviceNode(IFMapNode *node) {
    device_list_->Add(agent_, this, node);
}

void ConfigManager::AddHealthCheckServiceNode(IFMapNode *node) {
    hc_list_->Add(agent_, this, node);
}

void ConfigManager::AddSgNode(IFMapNode *node) {
    sg_list_->Add(agent_, this, node);
}

void ConfigManager::AddVnNode(IFMapNode *node) {
    vn_list_->Add(agent_, this, node);
}

void ConfigManager::AddVrfNode(IFMapNode *node) {
    vrf_list_->Add(agent_, this, node);
}

void ConfigManager::AddVmNode(IFMapNode *node) {
    vm_list_->Add(agent_, this, node);
}

void ConfigManager::AddPhysicalInterfaceNode(IFMapNode *node) {
    physical_interface_list_->Add(agent_, this, node);
}

void ConfigManager::AddPhysicalDeviceVn(const boost::uuids::uuid &dev,
                                        const boost::uuids::uuid &vn) {
    device_vn_list_->Add(agent_, this, dev, vn);
}

void ConfigManager::DelPhysicalDeviceVn(const boost::uuids::uuid &dev,
                                        const boost::uuids::uuid &vn) {
    device_vn_list_->Delete(agent_, this, dev, vn);
}
uint32_t ConfigManager::PhysicalDeviceVnCount() const {
    return device_vn_list_->Size();
}

string ConfigManager::ProfileInfo() const {
    stringstream str;
    str << setw(16) << "CfgMgr"
        << " Queue" << setw(8) << Size()
        << " Timeout " << setw(8) << timeout()
        << " Process" << setw(8) << ProcessCount() << endl;
    str << setw(22)
        << " VMI-Q " << setw(8) << vmi_list_->Size()
        << " Enqueue " << setw(8) << vmi_list_->enqueue_count()
        << " Process" << setw(8) << vmi_list_->process_count() << endl;
    str << setw(22)
        << " LI-Q " << setw(8) << logical_interface_list_->Size()
        << " Enqueue " << setw(8) << logical_interface_list_->enqueue_count()
        << " Process" << setw(8) << logical_interface_list_->process_count() << endl;
    return str.str();
}

// When traversing graph, check if an IFMapNode can be used. Conditions are,
// - The node is not in deleted state
// - The node was notified earlier
bool ConfigManager::CanUseNode(IFMapNode *node) {
    if (node->IsDeleted()) {
        return false;
    }

    IFMapDependencyManager *dep =
        agent()->oper_db()->dependency_manager();
    IFMapNodeState *state = dep->IFMapNodeGet(node);
    if (state == NULL) {
        // State not set
        // - Table not managed by dependency manager. Node can be used
        // - Table managed by dependency manager. Node cannot be used
        if (dep->IsRegistered(node))
            return false;
        else
            return true;
    }

    if (state->notify() == false ||
        state->oper_db_request_enqueued() == false) {
        return false;
    }

    return true;
}

// When traversing graph, check if an IFMapNode can be used. Conditions are,
// - The node is not in deleted state
// - The node was notified earlier
// - The node is an entry in IFMapAgentTable specified
bool ConfigManager::CanUseNode(IFMapNode *node, IFMapAgentTable *table) {
    if (table != static_cast<IFMapAgentTable *>(node->table())) {
        return false;
    }

    return CanUseNode(node);
}

bool ConfigManager::SkipNode(IFMapNode *node) {
    return !CanUseNode(node);
}

bool ConfigManager::SkipNode(IFMapNode *node, IFMapAgentTable *table) {
    return !CanUseNode(node, table);
}


IFMapNode *ConfigManager::FindAdjacentIFMapNode(IFMapNode *node,
                                              const char *type) {
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (SkipNode(adj_node)) {
            continue;
        }
        if (strcmp(adj_node->table()->Typename(), type) == 0) {
            return adj_node;
        }
    }

    return NULL;
}

void ConfigManager::NodeResync(IFMapNode *node) {
    IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
    dep->PropogateNodeAndLinkChange(node);
}
