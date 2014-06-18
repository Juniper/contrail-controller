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

namespace opt = boost::program_options;

pthread_t asio_thread;

void *asio_poll(void *arg){
    Agent::GetInstance()->event_manager()->Run();
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

void WaitForInitDone(Agent *agent) {
    WAIT_FOR(100000, 1000, agent->init_done());
    bool done = agent->init_done();
    EXPECT_TRUE(done);
    if (done == false) {
        exit(-1);
    }
    client->WaitForIdle(15);
}

TestClient *TestInit(const char *init_file, bool ksync_init, bool pkt_init,
                     bool services_init, bool uve_init,
                     int agent_stats_interval, int flow_stats_interval,
                     bool asio, bool ksync_sync_mode) {
    if (TaskScheduler::GetInstance() == NULL) {
        TaskScheduler::Initialize();
    }
    Agent *agent = new Agent();
    TestClient *client = new TestClient(agent);

    // Read agent parameters from config file and arguments
    AgentParam *param = client->param();
    TestAgentInit *init = client->agent_init();

    opt::variables_map var_map;
    param->Init(init_file, "test", var_map);
    param->set_agent_stats_interval(agent_stats_interval);
    param->set_flow_stats_interval(flow_stats_interval);

    // Initialize the agent-init control class
    int sandesh_port = 0;
    Sandesh::InitGeneratorTest("VNSWAgent", "Agent", "Test", "Test",
                               Agent::GetInstance()->event_manager(),
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
    agent->CopyConfig(param);
    init->Start();

    WaitForInitDone(agent);

    client->Init();
    client->WaitForIdle();
    client->SetFlowFlushExclusionPolicy();
    client->SetFlowAgeExclusionPolicy();

    if (asio) {
        AsioRun();
    }

    if (init_file == NULL) {
        agent->set_vhost_interface_name("vhost0");
        InetInterface::CreateReq(Agent::GetInstance()->interface_table(),
                                 "vhost0", InetInterface::VHOST,
                                 Agent::GetInstance()->fabric_vrf_name(),
                                 Ip4Address(0), 0, Ip4Address(0), param->eth_port(), "");
        boost::system::error_code ec;
        Agent::GetInstance()->set_router_id
            (Ip4Address::from_string("10.1.1.1", ec));
        //Add a receive router
        agent->fabric_inet4_unicast_table()->AddVHostRecvRoute
            (Agent::GetInstance()->local_peer(),
             Agent::GetInstance()->fabric_vrf_name(), "vhost0",
             Agent::GetInstance()->router_id(), 32, "", false);
    }

    return client;
}

TestClient *StatsTestInit() {
    if (TaskScheduler::GetInstance() == NULL) {
        TaskScheduler::Initialize();
    }
    Agent *agent = new Agent();
    TestClient *client = new TestClient(agent);
    AgentParam *param = client->param();
    TestAgentInit *init = client->agent_init();

    // Read agent parameters from config file and arguments
    opt::variables_map var_map;
    param->Init("controller/src/vnsw/agent/test/vnswa_cfg.ini", "test", var_map);

    // Initialize the agent-init control class
    int sandesh_port = 0;
    Sandesh::InitGeneratorTest("VNSWAgent", "Agent", "Test", "Test",
                               Agent::GetInstance()->event_manager(),
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
    agent->CopyConfig(param);
    init->Start();

    WaitForInitDone(agent);

    AsioRun();

    sleep(1);
    Agent::GetInstance()->set_vhost_interface_name("vhost0");
    InetInterface::CreateReq(Agent::GetInstance()->interface_table(),
                             "vhost0", InetInterface::VHOST,
                             Agent::GetInstance()->fabric_vrf_name(),
                             Ip4Address(0), 0, Ip4Address(0), param->eth_port(), "");

    boost::system::error_code ec;
    Agent::GetInstance()->set_router_id(Ip4Address::from_string("10.1.1.1", ec));

    // Wait for host and vhost interface creation
    client->WaitForIdle();

    return client;
}

TestClient *VGwInit(const string &init_file, bool ksync_init) {
    if (TaskScheduler::GetInstance() == NULL) {
        TaskScheduler::Initialize();
    }
    Agent *agent = new Agent();
    TestClient *client = new TestClient(agent);
    AgentParam *param = client->param();
    TestAgentInit *init = client->agent_init();

    // Read agent parameters from config file and arguments
    opt::variables_map var_map;
    param->Init(init_file, "test", var_map);

    // Initialize the agent-init control class
    Sandesh::InitGeneratorTest("VNSWAgent", "Agent", "Test", "Test",
                               Agent::GetInstance()->event_manager(),
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
    agent->CopyConfig(param);
    init->Start();

    WaitForInitDone(agent);

    client->Init();
    client->WaitForIdle();

    AsioRun();

    usleep(100);
    Agent::GetInstance()->set_vhost_interface_name("vhost0");
    InetInterface::CreateReq(Agent::GetInstance()->interface_table(),
                             "vhost0", InetInterface::VHOST,
                             Agent::GetInstance()->fabric_vrf_name(),
                             Ip4Address(0), 0, Ip4Address(0), param->eth_port(), "");
    boost::system::error_code ec;
    Agent::GetInstance()->set_router_id(Ip4Address::from_string("10.1.1.1", ec));

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
        if (Agent::GetInstance()->db()->FindTable(name) == NULL) {
            break;
        }

        usleep(1000);
        i += 1000;
    }

    return (Agent::GetInstance()->db()->FindTable(name) == NULL);
}

void TestClient::Shutdown() {
    agent_init_.Shutdown();
    Agent::GetInstance()->uve()->Shutdown();
    KSyncTest *ksync = static_cast<KSyncTest *>(Agent::GetInstance()->ksync());
    ksync->NetlinkShutdownTest();
    Agent::GetInstance()->ksync()->Shutdown();
    Agent::GetInstance()->pkt()->Shutdown();  
    MulticastHandler::Shutdown();
    Agent::GetInstance()->oper_db()->Shutdown();
    Agent::GetInstance()->db()->Clear();
    Agent::GetInstance()->db()->ClearFactoryRegistry();
}

void TestShutdown() {
    TestAgentInit *init = client->agent_init();
    client->WaitForIdle();

    init->IoShutdown();
    client->WaitForIdle();

    init->FlushFlows();
    client->WaitForIdle();

    init->DeleteRoutes();
    client->WaitForIdle();

    if (Agent::GetInstance()->vgw()) {
        Agent::GetInstance()->vgw()->Shutdown();
    }

    DBTableWalker *walker;
    walker = init->DeleteInterfaces();
    client->WaitForIdle();
    delete walker;

    walker = init->DeleteVms();
    client->WaitForIdle();
    delete walker;

    walker = init->DeleteVns();
    client->WaitForIdle();
    delete walker;

    walker = init->DeleteVrfs();
    client->WaitForIdle();
    delete walker;

    walker = init->DeleteNextHops();
    client->WaitForIdle();
    delete walker;

    walker = init->DeleteSecurityGroups();
    client->WaitForIdle();
    delete walker;

    walker = init->DeleteAcls();
    client->WaitForIdle();
    delete walker;

    init->ServicesShutdown();
    client->WaitForIdle();

    WaitForDbCount(Agent::GetInstance()->interface_table(), 0, 10000);
    EXPECT_EQ(0U, Agent::GetInstance()->interface_table()->Size());

    WaitForDbCount(Agent::GetInstance()->vrf_table(), 0, 10000);
    EXPECT_EQ(0U, Agent::GetInstance()->vrf_table()->Size());

    WaitForDbCount(Agent::GetInstance()->nexthop_table(), 0, 10000);
    EXPECT_EQ(0U, Agent::GetInstance()->nexthop_table()->Size());

    WaitForDbFree(Agent::GetInstance()->fabric_vrf_name(), 10000);
    assert(Agent::GetInstance()->db()->FindTable(Agent::GetInstance()->fabric_vrf_name()) == NULL);

    WaitForDbCount(Agent::GetInstance()->vm_table(), 0, 10000);
    EXPECT_EQ(0U, Agent::GetInstance()->vm_table()->Size());

    WaitForDbCount(Agent::GetInstance()->vn_table(), 0, 10000);
    EXPECT_EQ(0U, Agent::GetInstance()->vn_table()->Size());

    WaitForDbCount(Agent::GetInstance()->mpls_table(), 0, 10000);
    EXPECT_EQ(0U, Agent::GetInstance()->mpls_table()->Size());

    WaitForDbCount(Agent::GetInstance()->interface_config_table(), 0, 10000);
    EXPECT_EQ(0U, Agent::GetInstance()->interface_config_table()->Size());

    WaitForDbCount(Agent::GetInstance()->acl_table(), 0, 10000);
    EXPECT_EQ(0U, Agent::GetInstance()->acl_table()->Size());
    client->WaitForIdle();

    client->Shutdown();
    client->WaitForIdle();

    Sandesh::Uninit();
    client->WaitForIdle();

    Agent::GetInstance()->event_manager()->Shutdown();
    AsioStop();

    AgentStats::GetInstance()->Shutdown();
    Agent::GetInstance()->Shutdown();
    client->delete_agent();

    TaskScheduler::GetInstance()->Terminate();
}
