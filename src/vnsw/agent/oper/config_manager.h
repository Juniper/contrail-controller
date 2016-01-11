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
 * The changelist is implemented to objects
 * security-group
 * virtual-machine-interface
 * logical-interfaces
 * physical-device-vn
 * physical-router
 *****************************************************************************/

#include <cmn/agent_cmn.h>
#include <cmn/agent_db.h>
#include <operdb_init.h>
#include <ifmap_dependency_manager.h>

class ConfigManagerNodeList;
class ConfigManagerDeviceVnList;

class ConfigManager {
public:
    // Number of changelist entries to pick in one run
    const static uint32_t kIterationCount = 64;
    const static uint32_t kMinTimeout = 1;
    const static uint32_t kMaxTimeout = 10;

    ConfigManager(Agent *agent);
    virtual ~ConfigManager();

    void Init();
    int Run();
    bool TriggerRun();
    bool TimerRun();
    void Start();

    uint32_t Size() const;
    uint32_t ProcessCount() const;
    uint32_t timeout() const { return timeout_; }
    std::string ProfileInfo() const;

    void AddVmiNode(IFMapNode *node);
    void DelVmiNode(IFMapNode *node);
    uint32_t VmiNodeCount() const;

    void AddLogicalInterfaceNode(IFMapNode *node);
    void AddPhysicalInterfaceNode(IFMapNode *node);
    void AddSgNode(IFMapNode *node);
    void AddVnNode(IFMapNode *node);
    void AddVrfNode(IFMapNode *node);
    void AddVmNode(IFMapNode *node);
    uint32_t LogicalInterfaceNodeCount() const;

    void AddPhysicalDeviceNode(IFMapNode *node);
    void AddPhysicalDeviceVn(const boost::uuids::uuid &dev,
                             const boost::uuids::uuid &vn);
    void DelPhysicalDeviceVn(const boost::uuids::uuid &dev,
                             const boost::uuids::uuid &vn);
    void AddHealthCheckServiceNode(IFMapNode *node);
    uint32_t PhysicalDeviceVnCount() const;
    bool CanUseNode(IFMapNode *node);
    bool CanUseNode(IFMapNode *node, IFMapAgentTable *table);
    bool SkipNode(IFMapNode *node);
    bool SkipNode(IFMapNode *node, IFMapAgentTable *table);
    IFMapNode * FindAdjacentIFMapNode(IFMapNode *node,
            const char *type);
    void NodeResync(IFMapNode *node);
    Agent *agent() { return agent_; }

private:
    Agent *agent_;
    std::auto_ptr<TaskTrigger> trigger_;
    Timer *timer_;
    uint32_t timeout_;

    std::auto_ptr<ConfigManagerNodeList> vmi_list_;
    std::auto_ptr<ConfigManagerNodeList> physical_interface_list_;
    std::auto_ptr<ConfigManagerNodeList> logical_interface_list_;
    std::auto_ptr<ConfigManagerNodeList> device_list_;
    std::auto_ptr<ConfigManagerNodeList> sg_list_;
    std::auto_ptr<ConfigManagerNodeList> vn_list_;
    std::auto_ptr<ConfigManagerNodeList> vrf_list_;
    std::auto_ptr<ConfigManagerNodeList> vm_list_;
    std::auto_ptr<ConfigManagerNodeList> hc_list_;
    std::auto_ptr<ConfigManagerDeviceVnList> device_vn_list_;

    uint64_t process_config_count_[kMaxTimeout + 1];
    DISALLOW_COPY_AND_ASSIGN(ConfigManager);
};

#endif  // SRC_VNSW_AGENT_OPER_CONFIG_MANAGER_H_
