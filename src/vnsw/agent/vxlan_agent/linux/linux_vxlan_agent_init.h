/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_linux_vlan_agent_init_hpp
#define vnsw_linux_vlan_agent_init_hpp

#include <boost/program_options.hpp>

class Agent;
class AgentParam;
class KSyncVxlan;

// The class to drive agent initialization.
// Defines control parameters used to enable/disable agent features
class LinuxVxlanAgentInit {
public:
    LinuxVxlanAgentInit() :
        agent_(NULL), params_(NULL), init_done_(false), trigger_(),
        ksync_vxlan_(NULL) {
    }

    ~LinuxVxlanAgentInit() {
        trigger_->Reset();
        ksync_vxlan_.reset(NULL);
    }

    bool Run();
    void Start();
    void Shutdown();

    void InitLogging();
    void InitCollector();
    void CreateModules();
    void CreateDBTables();
    void RegisterDBClients();
    void InitModules();
    void InitPeers();
    void CreateVrf();
    void CreateNextHops();
    void CreateInterfaces();
    void InitDiscovery();
    void InitDone();
    void InitXenLinkLocalIntf();

    void Init(AgentParam *param, Agent *agent,
              const boost::program_options::variables_map &var_map);

    bool init_done() const { return init_done_; }
private:
    Agent *agent_;
    AgentParam *params_;
    bool init_done_;
    std::auto_ptr<TaskTrigger> trigger_;
    std::auto_ptr<KSyncVxlan> ksync_vxlan_;
    DISALLOW_COPY_AND_ASSIGN(LinuxVxlanAgentInit);
};

#endif // vnsw_linux_vlan_agent_init_hpp
