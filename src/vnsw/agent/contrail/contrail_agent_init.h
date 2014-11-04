/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_contrail_agent_init_hpp
#define vnsw_contrail_agent_init_hpp

#include <boost/program_options.hpp>
#include <init/contrail_init_common.h>
#include <contrail/pkt0_interface.h>

class Agent;
class AgentParam;
class DiagTable;
class ServicesModule;
class PktModule;

// The class to drive agent initialization. 
// Defines control parameters used to enable/disable agent features
class ContrailAgentInit : public ContrailInitCommon {
public:
    ContrailAgentInit();
    virtual ~ContrailAgentInit();

    void ProcessOptions(const std::string &config_file,
                        const std::string &program_name);

    // Initialization virtual methods
    void FactoryInit();
    void CreateModules();
    void ConnectToController();

    // Shutdown virtual methods
    void KSyncShutdown();
    void UveShutdown();
    void WaitForIdle();

private:
    std::auto_ptr<KSync> ksync_;
    std::auto_ptr<AgentUveBase> uve_;
    std::auto_ptr<Pkt0Interface> pkt0_;

    DISALLOW_COPY_AND_ASSIGN(ContrailAgentInit);
};

#endif // vnsw_contrail_agent_init_hpp
