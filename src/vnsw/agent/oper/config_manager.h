/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_OPER_CONFIG_MANAGER_H_
#define SRC_VNSW_AGENT_OPER_CONFIG_MANAGER_H_

/*****************************************************************************
 * Implements change list for the oper table. Normally, the oper objects
 * process configuration thru IFNodeToReq or IFLinkToReq callback methods.
 *
 * In scaled environments there can be large number of invocations to
 * IFNodeToReq and IFLinkToReq APIs. The config processing routines are 
 * written to act on the latest version of configuration. So, an invocation
 * of config routine will override all previous invocations. Ideally, we wnat
 * to invoke the config callbacks only once after all configuraiton is
 * received. However, we dont have any marker to identify end of configuration.
 * 
 * The next best design we follow is to add the objects changed into a 
 * change list. The change-list is run from a task-trigger. The list can
 * potentially compress multiple changes to a node
 *
 * The changelist should also take care of dependency between objects. For 
 * example, VMInterface has reference to VirtualNetwork. So, the change list 
 * for virtual-network should be invoked before VMInterface. The changelist
 * should take of all dependencies.
 *
 * To simplify current design, the changelist is implemented only to objects
 * virtual-machine-interface, logical-interfaces and physical-device-vn
 *****************************************************************************/

#include <cmn/agent_cmn.h>
#include <cmn/agent_db.h>
#include <operdb_init.h>
#include <ifmap_dependency_manager.h>

class ConfigManager {
public:
    // Number of changelist entries to pick in one run
    const static uint32_t kIterationCount = 32;
    // Set of changed IFMapNodes
    struct Node {
        Node(IFMapDependencyManager::IFMapNodePtr state);
        ~Node();
        IFMapDependencyManager::IFMapNodePtr state_;
    };

    struct NodeCmp {
        bool operator() (const Node &lhs, const Node &rhs) {
            return lhs.state_.get() < rhs.state_.get();
        }
    };
    typedef std::set<Node, NodeCmp> NodeList;
    typedef NodeList::iterator NodeListIterator;

    // Set of changed PhysicalDeviceVn entries
    struct PhysicalDeviceVnEntry {
        PhysicalDeviceVnEntry(const boost::uuids::uuid &dev,
                              const boost::uuids::uuid &vn);
        boost::uuids::uuid dev_;
        boost::uuids::uuid vn_;
    };

    struct PhysicalDeviceVnEntryCmp {
        bool operator() (const PhysicalDeviceVnEntry &lhs,
                         const PhysicalDeviceVnEntry &rhs) {
            if (lhs.dev_ != rhs.dev_)
                return lhs.dev_ < rhs.dev_;

            return lhs.vn_ < rhs.vn_;
        }
    };

    typedef std::set<PhysicalDeviceVnEntry, PhysicalDeviceVnEntryCmp>
        PhysicalDeviceVnList;
    typedef PhysicalDeviceVnList::iterator PhysicalDeviceVnIterator;

    ConfigManager(Agent *agent);
    virtual ~ConfigManager();

    bool Run();

    void AddVmiNode(IFMapNode *node);
    void DelVmiNode(IFMapNode *node);
    uint32_t VmiNodeCount();

    void AddLogicalInterfaceNode(IFMapNode *node);
    void DelLogicalInterfaceNode(IFMapNode *node);
    uint32_t LogicalInterfaceNodeCount();

    void AddPhysicalDeviceNode(IFMapNode *node);
    void DelPhysicalDeviceNode(IFMapNode *node);
    uint32_t PhysicalDeviceNodeCount();

    void AddPhysicalDeviceVn(const boost::uuids::uuid &dev,
                             const boost::uuids::uuid &vn);
    void DelPhysicalDeviceVn(const boost::uuids::uuid &dev,
                             const boost::uuids::uuid &vn);
    uint32_t PhysicalDeviceVnCount();

private:
    Agent *agent_;
    std::auto_ptr<TaskTrigger> trigger_;
    NodeList vmi_list_;
    NodeList logical_interface_list_;
    NodeList physical_device_list_;
    PhysicalDeviceVnList physical_device_vn_list_;

    DISALLOW_COPY_AND_ASSIGN(ConfigManager);
};

#endif  // SRC_VNSW_AGENT_OPER_CONFIG_MANAGER_H_
