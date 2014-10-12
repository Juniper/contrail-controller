/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
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
                     bool asio, bool ksync_sync_mode,
                     int vrouter_stats_interval) {

    TestClient *client = new TestClient(new TestAgentInit());
    TestAgentInit *init = client->agent_init();
    Agent *agent = client->agent();

    AgentParam *param = client->param();
    init->set_agent_param(param);
    // Read agent parameters from config file and arguments
    init->ProcessOptions(init_file, "test");
    param->set_agent_stats_interval(agent_stats_interval);
    param->set_flow_stats_interval(flow_stats_interval);
    param->set_vrouter_stats_interval(vrouter_stats_interval);

    // Initialize the agent-init control class
    int introspect_port = 0;
    Sandesh::InitGeneratorTest("VNSWAgent", "Agent", "Test", "Test",
                               agent->event_manager(), introspect_port, NULL);

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
        InetInterface::CreateReq(agent->interface_table(),
                                 "vhost0", InetInterface::VHOST,
                                 agent->fabric_vrf_name(),
                                 Ip4Address(0), 0, Ip4Address(0), param->eth_port(), "");
        boost::system::error_code ec;
        agent->set_router_id
            (Ip4Address::from_string("10.1.1.1", ec));
        //Add a receive router
        agent->fabric_inet4_unicast_table()->AddVHostRecvRoute
            (agent->local_peer(),
             agent->fabric_vrf_name(), "vhost0",
             agent->router_id(), 32, "", false);
    }

    return client;
}

TestClient *VGwInit(const string &init_file, bool ksync_init) {
    TestClient *client = new TestClient(new TestAgentInit());

    TestAgentInit *init = client->agent_init();
    Agent *agent = client->agent();

    // Initialize the agent-init control class
    Sandesh::InitGeneratorTest("VNSWAgent", "Agent", "Test", "Test",
                               agent->event_manager(),
                               0, NULL);

    // Read agent parameters from config file and arguments
    AgentParam *param(client->param());
    init->set_agent_param(param);
    init->ProcessOptions(init_file, "test");

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

    // kick start initialization
    init->Start();

    WaitForInitDone(agent);

    client->Init();
    client->WaitForIdle();

    AsioRun();

    usleep(100);
    agent->set_vhost_interface_name("vhost0");
    InetInterface::CreateReq(agent->interface_table(), "vhost0",
                             InetInterface::VHOST, agent->fabric_vrf_name(),
                             Ip4Address(0), 0, Ip4Address(0), param->eth_port(),
                             "");
    boost::system::error_code ec;
    agent->set_router_id(Ip4Address::from_string("10.1.1.1", ec));

    // Wait for host and vhost interface creation
    client->WaitForIdle();

    return client;
}

void ShutdownAgentController(Agent *agent) {
    TaskScheduler::GetInstance()->Stop();
    agent->controller()->multicast_cleanup_timer().cleanup_timer_->Fire();
    agent->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
    agent->controller()->config_cleanup_timer().cleanup_timer_->Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
    agent->controller()->Cleanup();
    client->WaitForIdle();
    agent->set_controller_ifmap_xmpp_client(NULL, 0);
    agent->set_controller_ifmap_xmpp_init(NULL, 0);
    agent->set_controller_ifmap_xmpp_client(NULL, 1);
    agent->set_controller_ifmap_xmpp_init(NULL, 1);
}

void TestShutdown() {
    TestAgentInit *init = client->agent_init();
    init->Shutdown();
    AgentStats::GetInstance()->Shutdown();
    AsioStop();
}
