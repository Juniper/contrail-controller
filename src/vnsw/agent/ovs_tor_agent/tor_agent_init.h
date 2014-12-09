/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_PHYSICAL_DEVICES_OVS_TOR_AGENT_TOR_AGENT_INIT_H_
#define SRC_VNSW_AGENT_PHYSICAL_DEVICES_OVS_TOR_AGENT_TOR_AGENT_INIT_H_

// Agent daemon on ovs-agent-node
#include <boost/program_options.hpp>
#include <init/agent_init.h>
#include <string>

class Agent;
class AgentParam;
class OvsPeerManager;
namespace OVSDB {
class OvsdbClient;
};

// The class to drive agent initialization.
// Defines control parameters used to enable/disable agent features
class TorAgentInit : public AgentInit {
 public:
    TorAgentInit();
    ~TorAgentInit();

    void ProcessOptions(const std::string &config_file,
                        const std::string &program_name);
    int Start();

    virtual std::string InstanceId();
    virtual std::string ModuleName();
    // Initialization virtual methods
    void FactoryInit();
    void CreatePeers();
    void CreateDBTables();
    void CreateModules();
    void RegisterDBClients();
    void InitModules();
    void ConnectToController();

    // Shutdown virtual methods
    void UveShutdown();
    void WaitForIdle();

    // Accessor methods
    OvsPeerManager *ovs_peer_manager() const;
    OVSDB::OvsdbClient *ovsdb_client() {return ovsdb_client_.get();}

 private:
    std::auto_ptr<OvsPeerManager> ovs_peer_manager_;
    std::auto_ptr<OVSDB::OvsdbClient> ovsdb_client_;
    std::auto_ptr<AgentUveBase> uve_;
    DISALLOW_COPY_AND_ASSIGN(TorAgentInit);
};

#endif  // SRC_VNSW_AGENT_PHYSICAL_DEVICES_OVS_TOR_AGENT_TOR_AGENT_INIT_H_
