/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_linux_vlan_agent_init_hpp
#define vnsw_linux_vlan_agent_init_hpp

#include <boost/program_options.hpp>
#include <init/agent_init.h>

class Agent;
class AgentParam;
class KSyncVxlan;

// The class to drive agent initialization.
// Defines control parameters used to enable/disable agent features
class LinuxVxlanAgentInit : public AgentInit {
public:
    LinuxVxlanAgentInit();
    ~LinuxVxlanAgentInit();

    void ProcessOptions(const std::string &config_file,
                        const std::string &program_name,
                        const boost::program_options::variables_map &var_map);

    int Start();

    // Initialization virtual methods
    void FactoryInit();
    void CreateModules();
    void RegisterDBClients();
    void InitModules();
    void ConnectToController();

    // Shutdown virtual methods
    void KSyncShutdown();
    void WaitForIdle();

private:
    std::auto_ptr<KSyncVxlan> ksync_vxlan_;
    DISALLOW_COPY_AND_ASSIGN(LinuxVxlanAgentInit);
};

#endif // vnsw_linux_vlan_agent_init_hpp
