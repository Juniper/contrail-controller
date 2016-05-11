/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_test_ovs_agent_init_hpp
#define vnsw_test_ovs_agent_init_hpp

#include <boost/program_options.hpp>
#include <init/contrail_init_common.h>
#include <test/test_pkt0_interface.h>
#include <test/test_agent_init.h>
#include <ovs_tor_agent/ovsdb_client/ovsdb_client_tcp.h>
#include <ovs_tor_agent/ovsdb_client/ovsdb_client_ssl.h>
#include "test-xml/test_xml.h"

class Agent;
class AgentParam;
class TestClient;
class OvsPeerManager;

void LoadAndRun(const std::string &file_name);
bool LoadXml(AgentUtXmlTest &test);

TestClient *OvsTestInit(const char *init_file, bool ovs_init,
                        bool use_ssl = false);

namespace OVSDB {
// OVSDB::OvsdbClientTcp objects override for test code to
// provide test functionality
class OvsdbClientTcpSessionTest : public OvsdbClientTcpSession {
public:
    OvsdbClientTcpSessionTest(Agent *agent, OvsPeerManager *manager,
                              TcpServer *server, Socket *sock,
                              bool async_ready = true);
    virtual ~OvsdbClientTcpSessionTest();

    virtual bool TestConcurrencyAllow() { return true; }
};

class OvsdbClientTcpTest : public OvsdbClientTcp {
public:
    OvsdbClientTcpTest(Agent *agent, IpAddress tor_ip, int tor_port,
                       IpAddress tsn_ip, int keepalive_interval,
                       OvsPeerManager *manager);
    virtual ~OvsdbClientTcpTest();

    virtual TcpSession *AllocSession(Socket *socket);

    virtual void Connect(TcpSession *session, Endpoint remote);

    void set_enable_connect(bool enable);

private:
    bool enable_connect_;
};
};

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
    OVSDB::OvsdbClient *ovsdb_client() const;

    void set_ovs_init(bool ovs_init);
    void set_use_ssl(bool use_ssl);

    void KSyncShutdown();

private:
    std::auto_ptr<OvsPeerManager> ovs_peer_manager_;
    std::auto_ptr<OVSDB::OvsdbClient> ovsdb_client_;

    bool ovs_init_;
    bool use_ssl_;
    DISALLOW_COPY_AND_ASSIGN(TestOvsAgentInit);
};

#endif // vnsw_test_ovs_agent_init_hpp
