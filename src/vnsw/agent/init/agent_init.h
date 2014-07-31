/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_init_agent_init_hpp
#define vnsw_agent_init_agent_init_hpp

#include <boost/program_options.hpp>
#include <cmn/agent_param.h>

class Agent;
class AgentParam;
class DiagTable;
class ServicesModule;
class PktModule;

// The class to drive agent initialization. Does init of basic modules
// Daemons should derive from AgentInit class and tune / implement virtual
// methods
class AgentInit {
public:
    AgentInit();
    virtual ~AgentInit();

    Agent *agent() const { return agent_.get(); }
    AgentParam *agent_param() const { return agent_param_.get(); }

    // Process options
    virtual void ProcessOptions
        (const std::string &config_file, const std::string &program_name,
         const boost::program_options::variables_map &var_map);

    // Kickstarts initialization
    virtual int Start();

    // Initialize the agent factory based on platform
    virtual void FactoryInit() = 0;

    // Init has two set of routines
    // - *Base routines that provide basic common initialization
    // - Another set of routines for platform specific initializations

    // Start method called from DBTable-task context
    virtual bool InitBase();
    virtual bool Init() { return true; }

    virtual void InitLoggingBase();
    virtual void InitLogging() { }

    // Init connection to collector if relavent config present
    virtual void InitCollectorBase();
    virtual void InitCollector() { }

    // Create modules. AgentInit creates Config, OperDB and Controller
    virtual void CreateModulesBase();
    virtual void CreateModules() { }

    // Create DBTables
    virtual void CreateDBTablesBase();
    virtual void CreateDBTables() { }

    // DBTable client registerations
    virtual void RegisterDBClientsBase();
    virtual void RegisterDBClients() { }

    // Module specific inits. Called after DBTable and DBTable clients are
    // created
    virtual void InitModulesBase();
    virtual void InitModules() { }

    // Create Route Peers
    virtual void CreatePeersBase();
    virtual void CreatePeers() { }

    // Create static VRFs. Fabric VRF is created by default
    virtual void CreateVrfBase();
    virtual void CreateVrf() { }

    // Create static nexthops including drop-nh and receive-nh
    virtual void CreateNextHopsBase();
    virtual void CreateNextHops() { }

    // Create static interfaces
    virtual void CreateInterfacesBase();
    virtual void CreateInterfaces() { }

    // Init Discovery process
    virtual void InitDiscoveryBase();
    virtual void InitDiscovery() { }

    // Connect to controller. Should be called after IP address is known
    // for vhost0 interface
    virtual void ConnectToControllerBase();
    virtual void ConnectToController() { }

    virtual void InitDoneBase();
    virtual void InitDone() { }

    /////////////////////////////////////////////////////////////////////////
    // Shutdown routines
    /////////////////////////////////////////////////////////////////////////
    virtual void Shutdown();

    // Shutdown IO operations
    void IoShutdownBase();
    virtual void IoShutdown() { }

    // Flush all flows
    virtual void FlushFlowsBase();
    virtual void FlushFlows() { }

    // Shutdown VGW
    virtual void VgwShutdownBase();
    virtual void VgwShutdown() { }

    // Delete routes
    virtual void DeleteRoutesBase();
    virtual void DeleteRoutes() { }

    // Delete other DB Entries
    virtual void DeleteDBEntriesBase();
    virtual void DeleteDBEntries() { }

    // Shutdown services
    virtual void ServicesShutdownBase();
    virtual void ServicesShutdown() { }

    // Shutdown pkt interface
    virtual void PktShutdownBase();
    virtual void PktShutdown() { }

    // Shutdown other modules
    virtual void ModulesShutdownBase();
    virtual void ModulesShutdown() { }

    // Shutdown UVE
    virtual void UveShutdownBase();
    virtual void UveShutdown() { }

    // Shutdown KSync
    virtual void KSyncShutdownBase();
    virtual void KSyncShutdown() { }

    // Utility
    virtual void WaitForIdle() = 0;
    void WaitForDBEmpty();

private:
    std::auto_ptr<Agent> agent_;
    std::auto_ptr<AgentParam> agent_param_;

    std::auto_ptr<TaskTrigger> trigger_;

    std::auto_ptr<AgentConfig> cfg_;
    std::auto_ptr<OperDB> oper_;

    bool enable_controller_;
    std::auto_ptr<VNController> controller_;

    DISALLOW_COPY_AND_ASSIGN(AgentInit);
};

#endif // vnsw_agent_init_agent_init_hpp
