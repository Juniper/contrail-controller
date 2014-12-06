/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_test_ovs_agent_init_hpp
#define vnsw_test_ovs_agent_init_hpp

#include <boost/program_options.hpp>
#include <init/contrail_init_common.h>
#include <test/test_pkt0_interface.h>
#include <test/test_agent_init.h>

class Agent;
class AgentParam;
class TestClient;
class OvsPeerManager;

TestClient *OvsTestInit(const char *init_file, bool ksync_init);

// The class to drive agent initialization.
// Defines control parameters used to enable/disable agent features
class TestOvsAgentInit : public TestAgentInit {
public:
    TestOvsAgentInit();
    virtual ~TestOvsAgentInit();

    void CreatePeers();
    void CreateModules();
    void CreateDBTables();
    void RegisterDBClients();

    OvsPeerManager *ovs_peer_manager() const;
private:
    std::auto_ptr<OvsPeerManager> ovs_peer_manager_;
    DISALLOW_COPY_AND_ASSIGN(TestOvsAgentInit);
};

#endif // vnsw_test_ovs_agent_init_hpp
