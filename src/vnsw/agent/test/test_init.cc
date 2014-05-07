/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "test/test_init.h"
#include "oper/mirror_table.h"
#include "vgw/cfg_vgw.h"
#include "vgw/vgw.h"
#include "ksync/ksync_init.h"
#include <uve/test/agent_uve_test.h>
#include <ksync/test/ksync_test.h>
#include <boost/functional/factory.hpp>
#include <cmn/agent_factory.h>

static AgentTestInit *agent_init;
namespace opt = boost::program_options;

pthread_t asio_thread;

void *asio_poll(void *arg){
    Agent::GetInstance()->GetEventManager()->Run();
    return NULL;
}

void AsioRun() {
    pthread_attr_t attr;
    int ret;

    pthread_attr_init(&attr);
    if ((ret = pthread_create(&asio_thread, &attr, asio_poll, NULL)) != 0) {
        LOG(ERROR, "pthread_create error : " <<  strerror(ret) );
        assert(0);
    }
}

void AsioStop() {
    pthread_join(asio_thread, NULL);
}

static void InitTestFactory() {
    AgentObjectFactory::Register<AgentUve>(boost::factory<AgentUveTest *>());
    AgentObjectFactory::Register<KSync>(boost::factory<KSyncTest *>());
}

TestClient *TestInit(const char *init_file, bool ksync_init, bool pkt_init,
                     bool services_init, bool uve_init,
                     int agent_stats_interval, int flow_stats_interval,
                     bool asio, bool ksync_sync_mode) {
    TestClient *client = new TestClient();
    agent_init = new AgentTestInit(client);
    client->set_init(agent_init);

    // Read agent parameters from config file and arguments
    Agent *agent = agent_init->agent();
    AgentParam *param = agent_init->param();
    AgentInit *init = agent_init->init();

    opt::variables_map var_map;
    param->Init(init_file, "test", var_map);
    param->set_agent_stats_interval(agent_stats_interval);
    param->set_flow_stats_interval(flow_stats_interval);

    // Initialize the agent-init control class
    int sandesh_port = 0;
    Sandesh::InitGeneratorTest("VNSWAgent", "Agent", "Test", "Test",
                               Agent::GetInstance()->GetEventManager(),
                               sandesh_port, NULL);

    InitTestFactory();
    init->Init(param, agent, var_map);
    init->set_ksync_enable(ksync_init);
    init->set_packet_enable(true);
    init->set_services_enable(services_init);
    init->set_create_vhost(false);
    init->set_uve_enable(uve_init);
    init->set_vgw_enable(false);
    init->set_router_id_dep_enable(false);
    if (!ksync_init) {
        param->set_test_mode(true);
    }
    agent->set_ksync_sync_mode(ksync_sync_mode);

    // Initialize agent and kick start initialization
    agent->CopyConfig(param, init);
    init->Start();

    WAIT_FOR(1000, 10000, init->init_done());
    client->Init();
    client->WaitForIdle();
    client->SetFlowFlushExclusionPolicy();
    client->SetFlowAgeExclusionPolicy();

    if (asio) {
        AsioRun();
    }

    if (init_file == NULL) {
        agent->set_vhost_interface_name("vhost0");
        InetInterface::CreateReq(Agent::GetInstance()->GetInterfaceTable(),
                                 "vhost0", InetInterface::VHOST,
                                 Agent::GetInstance()->GetDefaultVrf(),
                                 Ip4Address(0), 0, Ip4Address(0), "");
        boost::system::error_code ec;
        Agent::GetInstance()->SetRouterId
            (Ip4Address::from_string("10.1.1.1", ec));
        //Add a receive router
        agent->GetDefaultInet4UnicastRouteTable()->AddVHostRecvRoute
            (Agent::GetInstance()->local_peer(),
             Agent::GetInstance()->GetDefaultVrf(), "vhost0",
             Agent::GetInstance()->GetRouterId(), 32, "", false);
    }

    return client;
}

TestClient *StatsTestInit() {
    TestClient *client = new TestClient();
    agent_init = new AgentTestInit(client);
    client->set_init(agent_init);
    Agent *agent = agent_init->agent();
    AgentParam *param = agent_init->param();
    AgentInit *init = agent_init->init();

    // Read agent parameters from config file and arguments
    opt::variables_map var_map;
    param->Init("controller/src/vnsw/agent/test/vnswa_cfg.ini", "test", var_map);

    // Initialize the agent-init control class
    int sandesh_port = 0;
    Sandesh::InitGeneratorTest("VNSWAgent", "Agent", "Test", "Test",
                               Agent::GetInstance()->GetEventManager(),
                               sandesh_port, NULL);

    InitTestFactory();
    init->Init(param, agent, var_map);
    init->set_ksync_enable(true);
    init->set_packet_enable(true);
    init->set_services_enable(true);
    init->set_create_vhost(false);
    init->set_uve_enable(false);
    init->set_vgw_enable(false);
    init->set_router_id_dep_enable(false);
    param->set_test_mode(true);

    // Initialize agent and kick start initialization
    agent->CopyConfig(param, init);
    init->Start();

    AsioRun();

    sleep(1);
    Agent::GetInstance()->set_vhost_interface_name("vhost0");
    InetInterface::CreateReq(Agent::GetInstance()->GetInterfaceTable(),
                             "vhost0", InetInterface::VHOST,
                             Agent::GetInstance()->GetDefaultVrf(),
                             Ip4Address(0), 0, Ip4Address(0), "");

    boost::system::error_code ec;
    Agent::GetInstance()->SetRouterId(Ip4Address::from_string("10.1.1.1", ec));

    // Wait for host and vhost interface creation
    client->WaitForIdle();

    return client;
}

TestClient *VGwInit(const string &init_file, bool ksync_init) {
    TestClient *client = new TestClient();
    agent_init = new AgentTestInit(client);
    client->set_init(agent_init);
    Agent *agent = agent_init->agent();
    AgentParam *param = agent_init->param();
    AgentInit *init = agent_init->init();

    // Read agent parameters from config file and arguments
    opt::variables_map var_map;
    param->Init(init_file, "test", var_map);

    // Initialize the agent-init control class
    Sandesh::InitGeneratorTest("VNSWAgent", "Agent", "Test", "Test",
                               Agent::GetInstance()->GetEventManager(),
                               0, NULL);

    InitTestFactory();
    init->Init(param, agent, var_map);
    init->set_ksync_enable(ksync_init);
    init->set_packet_enable(true);
    init->set_services_enable(true);
    init->set_create_vhost(false);
    init->set_uve_enable(true);
    init->set_vgw_enable(true);
    init->set_router_id_dep_enable(false);
    if (!ksync_init) {
        param->set_test_mode(true);
    }

    // Initialize agent and kick start initialization
    agent->CopyConfig(param, init);
    init->Start();

    WAIT_FOR(1000, 10000, init->init_done());
    client->Init();
    client->WaitForIdle();

    AsioRun();

    usleep(100);
    Agent::GetInstance()->set_vhost_interface_name("vhost0");
    InetInterface::CreateReq(Agent::GetInstance()->GetInterfaceTable(),
                             "vhost0", InetInterface::VHOST,
                             Agent::GetInstance()->GetDefaultVrf(),
                             Ip4Address(0), 0, Ip4Address(0), "");
    boost::system::error_code ec;
    Agent::GetInstance()->SetRouterId(Ip4Address::from_string("10.1.1.1", ec));

    // Wait for host and vhost interface creation
    client->WaitForIdle();

    return client;
}

static bool WaitForDbCount(DBTableBase *table, uint32_t count, int msec) {
    int i = 0;

    msec = msec * 1000;
    while ((table->Size() > count)  && (i < msec)) {
        usleep(1000);
        i += 1000;
    }

    return (table->Size() == count);
}

static bool WaitForDbFree(const string &name, int msec) {
    int i = 0;

    msec = msec * 1000;
    while (i < msec) {
        if (Agent::GetInstance()->GetDB()->FindTable(name) == NULL) {
            break;
        }

        usleep(1000);
        i += 1000;
    }

    return (Agent::GetInstance()->GetDB()->FindTable(name) == NULL);
}

void TestClient::Shutdown() {
    Agent::GetInstance()->init()->Shutdown();
    Agent::GetInstance()->uve()->Shutdown();
    KSyncTest *ksync = static_cast<KSyncTest *>(Agent::GetInstance()->ksync());
    ksync->NetlinkShutdownTest();
    Agent::GetInstance()->ksync()->Shutdown();
    Agent::GetInstance()->pkt()->Shutdown();  
    Agent::GetInstance()->services()->Shutdown();
    MulticastHandler::Shutdown();
    Agent::GetInstance()->oper_db()->Shutdown();
    Agent::GetInstance()->GetDB()->Clear();
    Agent::GetInstance()->GetDB()->ClearFactoryRegistry();
}

void TestShutdown() {
    client->WaitForIdle();

    Agent::GetInstance()->controller()->DisConnect();
    client->WaitForIdle();

    if (Agent::GetInstance()->vgw()) {
        Agent::GetInstance()->vgw()->Shutdown();
    }

    Agent::GetInstance()->init()->DeleteRoutes();
    client->WaitForIdle();

    Agent::GetInstance()->init()->DeleteInterfaces();
    client->WaitForIdle();

    Agent::GetInstance()->init()->DeleteVrfs();
    client->WaitForIdle();

    Agent::GetInstance()->init()->DeleteNextHops();
    client->WaitForIdle();

    WaitForDbCount(Agent::GetInstance()->GetInterfaceTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetInterfaceTable()->Size());

    WaitForDbCount(Agent::GetInstance()->GetVrfTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetVrfTable()->Size());

    WaitForDbCount(Agent::GetInstance()->GetNextHopTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetNextHopTable()->Size());

    WaitForDbFree(Agent::GetInstance()->GetDefaultVrf(), 100);
    assert(Agent::GetInstance()->GetDB()->FindTable(Agent::GetInstance()->GetDefaultVrf()) == NULL);

    WaitForDbCount(Agent::GetInstance()->GetVmTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetVmTable()->Size());

    WaitForDbCount(Agent::GetInstance()->GetVnTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetVnTable()->Size());

    WaitForDbCount(Agent::GetInstance()->GetMplsTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetMplsTable()->Size());

    WaitForDbCount(Agent::GetInstance()->GetIntfCfgTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetIntfCfgTable()->Size());

    WaitForDbCount(Agent::GetInstance()->GetAclTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetAclTable()->Size());
    client->WaitForIdle();

    agent_init->Shutdown();
    client->WaitForIdle();

    Sandesh::Uninit();
    client->WaitForIdle();

    Agent::GetInstance()->GetEventManager()->Shutdown();
    AsioStop();
    TaskScheduler::GetInstance()->Terminate();

    AgentStats::GetInstance()->Shutdown();
    Agent::GetInstance()->Shutdown();
    delete agent_init;
}
