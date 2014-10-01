/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <base/logging.h>
#include <pugixml/pugixml.hpp>
#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include <oper/operdb_init.h>
#include <oper/global_vrouter.h>
#include <test/test_cmn_util.h>

#define MAX_SERVICES 5
#define MAX_COUNT 100
#define VHOST_IP "10.1.2.1"

std::string linklocal_name[] = 
    { "metadata", "myservice", "testservice", "newservice", "experimental" };

std::string linklocal_ip[] = 
    { "169.254.169.254", "169.254.1.1", "169.254.10.20",
      "169.254.20.30", "169.254.30.40" };

uint16_t linklocal_port[] = { 80, 8000, 12125, 22000, 34567 };

std::string fabric_dns_name[] =
    { "localhost", "www.juniper.net", "www.google.com",
      "www.cnn.com", "github.com" };

std::string fabric_ip[] = 
    { "10.1.2.3", "10.1.2.4", "10.1.2.5", "10.1.2.6", "10.1.2.7" };

uint16_t fabric_port[] = { 8775, 8080, 9000, 12345, 8000 };

class LinkLocalTest : public ::testing::Test {
public:
    LinkLocalTest() : response_count_(0) {}
    ~LinkLocalTest() {}

    void FillServices(TestLinkLocalService *services, int count) {
        for (int i = 0; i < count; ++i) {
            services[i].linklocal_name = linklocal_name[i];
            services[i].linklocal_ip = linklocal_ip[i];
            services[i].linklocal_port = linklocal_port[i];
            services[i].fabric_dns_name = fabric_dns_name[i];
            for (int j = 0; j < i + 1; ++j) {
                services[i].fabric_ip.push_back(fabric_ip[j]);
            }
            services[i].fabric_port = fabric_port[i];
        }
    }

    void CheckSandeshResponse(Sandesh *sandesh) {
        response_count_++;
    }

    int sandesh_response_count() { return response_count_; }
    void ClearSandeshResponseCount() { response_count_ = 0; }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
    }

    virtual void TearDown() {
        WAIT_FOR(10000, 100,
                 (agent_->oper_db()->global_vrouter()->PendingFabricDnsRequests()
                  == 0));
    }

private:
    int response_count_;
    Agent *agent_;
};

TEST_F(LinkLocalTest, LinkLocalReqTest) {
    Agent::GetInstance()->set_router_id(Ip4Address::from_string(VHOST_IP));
    TestLinkLocalService services[MAX_SERVICES];
    FillServices(services, MAX_SERVICES);
    AddLinkLocalConfig(services, MAX_SERVICES);

    struct PortInfo input[] = { 
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}, 
    };  
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
    };
    AddIPAM("ipam1", ipam_info, 3);
    client->WaitForIdle();

    struct PortInfo new_input[] = { 
        {"vnet2", 2, "2.2.2.2", "00:00:00:02:02:02", 2, 2}, 
    };  
    CreateVmportEnv(new_input, 1, 0);
    client->WaitForIdle();
    client->Reset();

    // check that all expected routes are added
    for (int i = 0; i < MAX_SERVICES; ++i) {
        InetUnicastRouteEntry *rt =
            RouteGet("vrf1", Ip4Address::from_string(linklocal_ip[i]), 32);
        EXPECT_TRUE(rt != NULL);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RECEIVE);
    }
    for (int i = 0; i < MAX_SERVICES; ++i) {
        InetUnicastRouteEntry *rt =
            RouteGet("vrf2", Ip4Address::from_string(linklocal_ip[i]), 32);
        EXPECT_TRUE(rt != NULL);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RECEIVE);
    }
    for (int i = 0; i < MAX_SERVICES; ++i) {
        InetUnicastRouteEntry *rt =
            RouteGet(Agent::GetInstance()->fabric_vrf_name(),
                     Ip4Address::from_string(fabric_ip[i]), 32);
        EXPECT_TRUE(rt != NULL);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    }
    int count = 0;
    while (Agent::GetInstance()->oper_db()->global_vrouter()->IsAddressInUse(
                Ip4Address::from_string("127.0.0.1")) == false &&
           count++ < MAX_COUNT)
        usleep(1000);
    EXPECT_TRUE(count < MAX_COUNT);

    // Delete linklocal config
    DelLinkLocalConfig();
    client->WaitForIdle();

    // check that all routes are deleted
    for (int i = 0; i < MAX_SERVICES; ++i) {
        InetUnicastRouteEntry *rt =
            RouteGet("vrf1", Ip4Address::from_string(linklocal_ip[i]), 32);
        EXPECT_TRUE(rt == NULL);
    }
    for (int i = 0; i < MAX_SERVICES; ++i) {
        InetUnicastRouteEntry *rt =
            RouteGet("vrf2", Ip4Address::from_string(linklocal_ip[i]), 32);
        EXPECT_TRUE(rt == NULL);
    }
    EXPECT_FALSE(Agent::GetInstance()->oper_db()->global_vrouter()->IsAddressInUse(
                 Ip4Address::from_string("127.0.0.1")));

    client->Reset();
    DelIPAM("ipam1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, 1, 0); 
    client->WaitForIdle();
    DeleteVmportEnv(new_input, 1, 1, 0); 
    client->WaitForIdle();
}

TEST_F(LinkLocalTest, LinkLocalChangeTest) {
    Agent::GetInstance()->set_router_id(Ip4Address::from_string(VHOST_IP));
    TestLinkLocalService services[MAX_SERVICES];
    FillServices(services, MAX_SERVICES);
    AddLinkLocalConfig(services, MAX_SERVICES);

    struct PortInfo input[] = { 
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}, 
    };  
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();

    // change linklocal request and send request for 3 services
    services[2].linklocal_ip = "169.254.100.100";
    services[2].fabric_ip.clear();
    AddLinkLocalConfig(services, 3);
    client->WaitForIdle();
    for (int i = 0; i < MAX_SERVICES; ++i) {
        InetUnicastRouteEntry *rt =
            RouteGet("vrf1", Ip4Address::from_string(linklocal_ip[i]), 32);
        if (i < 2) {
            EXPECT_TRUE(rt != NULL);
            EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RECEIVE);
        } else {
            EXPECT_TRUE(rt == NULL);
        }
    }
    InetUnicastRouteEntry *local_rt =
        RouteGet("vrf1", Ip4Address::from_string("169.254.100.100"), 32);
    EXPECT_TRUE(local_rt != NULL);
    EXPECT_TRUE(local_rt->GetActiveNextHop()->GetType() == NextHop::RECEIVE);
    for (int i = 0; i < 3; ++i) {
        InetUnicastRouteEntry *rt =
            RouteGet(Agent::GetInstance()->fabric_vrf_name(),
                     Ip4Address::from_string(fabric_ip[i]), 32);
        EXPECT_TRUE(rt != NULL);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    }

    // call introspect
    LinkLocalServiceInfo *sandesh_info = new LinkLocalServiceInfo();
    Sandesh::set_response_callback(boost::bind(&LinkLocalTest::CheckSandeshResponse, this, _1));
    sandesh_info->HandleRequest();
    client->WaitForIdle();
    sandesh_info->Release();
    EXPECT_TRUE(sandesh_response_count() == 1);
    ClearSandeshResponseCount();

    // Delete linklocal config
    DelLinkLocalConfig();
    client->WaitForIdle();

    // check that all routes are deleted
    for (int i = 0; i < MAX_SERVICES; ++i) {
        InetUnicastRouteEntry *rt =
            RouteGet("vrf1", Ip4Address::from_string(linklocal_ip[i]), 32);
        EXPECT_TRUE(rt == NULL);
    }
    local_rt = RouteGet("vrf1", Ip4Address::from_string("169.254.100.100"), 32);
    EXPECT_TRUE(local_rt == NULL);
    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0); 
    client->WaitForIdle();
}

TEST_F(LinkLocalTest, GlobalVrouterDeleteTest) {
    Agent::GetInstance()->set_router_id(Ip4Address::from_string(VHOST_IP));
    TestLinkLocalService services[MAX_SERVICES];
    FillServices(services, MAX_SERVICES);
    AddLinkLocalConfig(services, MAX_SERVICES);

    struct PortInfo input[] = { 
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}, 
    };  
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
        {"1.2.3.128", 27, "1.2.3.129", true},
        {"7.8.9.0", 24, "7.8.9.12", true},
    };
    AddIPAM("ipam1", ipam_info, 3);
    client->WaitForIdle();

    struct PortInfo new_input[] = { 
        {"vnet2", 2, "2.2.2.2", "00:00:00:02:02:02", 2, 2}, 
    };  
    CreateVmportEnv(new_input, 1, 0);
    client->WaitForIdle();
    client->Reset();

    // check that all expected routes are added
    for (int i = 0; i < MAX_SERVICES; ++i) {
        InetUnicastRouteEntry *rt =
            RouteGet("vrf1", Ip4Address::from_string(linklocal_ip[i]), 32);
        EXPECT_TRUE(rt != NULL);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RECEIVE);
    }
    for (int i = 0; i < MAX_SERVICES; ++i) {
        InetUnicastRouteEntry *rt =
            RouteGet("vrf2", Ip4Address::from_string(linklocal_ip[i]), 32);
        EXPECT_TRUE(rt != NULL);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RECEIVE);
    }
    for (int i = 0; i < MAX_SERVICES; ++i) {
        InetUnicastRouteEntry *rt =
            RouteGet(Agent::GetInstance()->fabric_vrf_name(),
                     Ip4Address::from_string(fabric_ip[i]), 32);
        EXPECT_TRUE(rt != NULL);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    }

    // Delete global vrouter config; this is different from the earlier case
    // where we delete the link local entries
    DeleteGlobalVrouterConfig();
    client->WaitForIdle();

    // check that all routes are deleted
    for (int i = 0; i < MAX_SERVICES; ++i) {
        InetUnicastRouteEntry *rt =
            RouteGet("vrf1", Ip4Address::from_string(linklocal_ip[i]), 32);
        EXPECT_TRUE(rt == NULL);
    }
    for (int i = 0; i < MAX_SERVICES; ++i) {
        InetUnicastRouteEntry *rt =
            RouteGet("vrf2", Ip4Address::from_string(linklocal_ip[i]), 32);
        EXPECT_TRUE(rt == NULL);
    }

    client->Reset();
    DelIPAM("ipam1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, 1, 0); 
    client->WaitForIdle();
    DeleteVmportEnv(new_input, 1, 1, 0); 
    client->WaitForIdle();
}

void RouterIdDepInit(Agent *agent) {
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, false, false, false);
    usleep(100000);
    client->WaitForIdle();

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    client->WaitForIdle();
    delete client;
    return ret;
}
