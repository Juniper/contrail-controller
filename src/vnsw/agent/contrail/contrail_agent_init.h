/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_contrail_agent_init_hpp
#define vnsw_contrail_agent_init_hpp

class Agent;
class AgentParam;

// The class to drive agent initialization. 
// Defines control parameters used to enable/disable agent features
class ContrailAgentInit {
public:
    ContrailAgentInit() : agent_(NULL), params_(NULL), trigger_() { }

    ~ContrailAgentInit() {
        trigger_->Reset();
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
    void InitVmwareInterface();

    void Init(AgentParam *param, Agent *agent,
              const boost::program_options::variables_map &var_map);

private:
    Agent *agent_;
    AgentParam *params_;
    std::auto_ptr<TaskTrigger> trigger_;
    std::auto_ptr<DiagTable> diag_table_;
    std::auto_ptr<ServicesModule> services_;
    std::auto_ptr<PktModule> pkt_;
    DISALLOW_COPY_AND_ASSIGN(ContrailAgentInit);
};

#endif // vnsw_contrail_agent_init_hpp
