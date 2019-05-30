/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "pkt/flow_proto.h"
#include "pkt/flow_mgmt.h"
#include "pkt/pkt_handler.h"
#include "oper/bgp_as_service.h"
#include "oper/bgp_router.h"
#include "test/test_cmn_util.h"

using namespace std;
using namespace boost::assign;

void RouterIdDepInit(Agent *agent) {
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

class BgpRouterCfgTest : public ::testing::Test {
public:
    BgpRouterCfgTest() : agent_(Agent::GetInstance()) {
    }

    Agent *agent_;
};

/* Verify BgpRouter */
TEST_F(BgpRouterCfgTest, Test_1) {
    client->Reset();
    BgpRouterConfig *bgp_router_config =
        agent_->oper_db()->bgp_router_config();

    AddBgpRouterConfig("127.0.0.1", 0, 179,
                       1, "ip-fabric", "control-node");
    AddBgpRouterConfig("127.0.0.2", 0, 179,
                       2, "ip-fabric", "control-node");
    AddBgpRouterConfig("127.0.0.3", 0, 179,
                       3, "ip-fabric", "control-node");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 3);

    DeleteBgpRouterConfig("127.0.0.1", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.2", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.3", 0, "ip-fabric");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 0);
}

/* ControlNodeZone always tracked via BgpRouter.
 * ControlNodeZone count should be always zero without
 * BgpRouter Link */
TEST_F(BgpRouterCfgTest, Test_2) {
    client->Reset();
    BgpRouterConfig *bgp_router_config =
        agent_->oper_db()->bgp_router_config();

    AddControlNodeZone("cnz-a", 1);
    AddControlNodeZone("cnz-b", 2);
    AddControlNodeZone("cnz-c", 3);
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 0);

    DeleteControlNodeZone("cnz-a");
    DeleteControlNodeZone("cnz-b");
    DeleteControlNodeZone("cnz-c");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 0);
}

/* Verify BgpRouter-ControlNodeZone Link */
TEST_F(BgpRouterCfgTest, Test_3) {
    client->Reset();
    BgpRouterConfig *bgp_router_config =
        agent_->oper_db()->bgp_router_config();

    std::string bgp_router_1 = AddBgpRouterConfig("127.0.0.1", 0, 179,
        1, "ip-fabric", "control-node");
    std::string bgp_router_2 = AddBgpRouterConfig("127.0.0.2", 0, 179,
        2, "ip-fabric", "control-node");
    std::string bgp_router_3 = AddBgpRouterConfig("127.0.0.3", 0, 179,
        3, "ip-fabric", "control-node");
    std::string bgp_router_4 = AddBgpRouterConfig("127.0.0.4", 0, 179,
        4, "ip-fabric", "control-node");
    std::string bgp_router_5 = AddBgpRouterConfig("127.0.0.5", 0, 179,
        5, "ip-fabric", "control-node");
    std::string bgp_router_6 = AddBgpRouterConfig("127.0.0.6", 0, 179,
        6, "ip-fabric", "control-node");
    AddControlNodeZone("cnz-a", 1);
    AddControlNodeZone("cnz-b", 2);
    AddControlNodeZone("cnz-c", 3);
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 6);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-b") == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-c") == 0);

    AddLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    AddLink("bgp-router", bgp_router_2.c_str(), "control-node-zone", "cnz-b");
    AddLink("bgp-router", bgp_router_3.c_str(), "control-node-zone", "cnz-c");
    AddLink("bgp-router", bgp_router_4.c_str(), "control-node-zone", "cnz-a");
    AddLink("bgp-router", bgp_router_5.c_str(), "control-node-zone", "cnz-b");
    AddLink("bgp-router", bgp_router_6.c_str(), "control-node-zone", "cnz-a");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 6);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 3);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 3);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-b") == 2);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-c") == 1);

    DelLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    DelLink("bgp-router", bgp_router_2.c_str(), "control-node-zone", "cnz-b");
    DelLink("bgp-router", bgp_router_3.c_str(), "control-node-zone", "cnz-c");
    DelLink("bgp-router", bgp_router_4.c_str(), "control-node-zone", "cnz-a");
    DelLink("bgp-router", bgp_router_5.c_str(), "control-node-zone", "cnz-b");
    DelLink("bgp-router", bgp_router_6.c_str(), "control-node-zone", "cnz-a");
    DeleteBgpRouterConfig("127.0.0.1", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.2", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.3", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.4", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.5", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.6", 0, "ip-fabric");
    DeleteControlNodeZone("cnz-a");
    DeleteControlNodeZone("cnz-b");
    DeleteControlNodeZone("cnz-c");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 0);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-b") == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-c") == 0);
}

/* Verify BgpRouter-ControlNodeZone Update */
TEST_F(BgpRouterCfgTest, Test_4) {
    client->Reset();
    BgpRouterConfig *bgp_router_config =
        agent_->oper_db()->bgp_router_config();

    std::string bgp_router_1 = AddBgpRouterConfig("127.0.0.1", 0, 179,
        1, "ip-fabric", "control-node");
    std::string bgp_router_2 = AddBgpRouterConfig("127.0.0.2", 0, 179,
        2, "ip-fabric", "control-node");
    std::string bgp_router_3 = AddBgpRouterConfig("127.0.0.3", 0, 179,
        3, "ip-fabric", "control-node");
    AddControlNodeZone("cnz-a", 1);
    AddControlNodeZone("cnz-b", 2);
    AddControlNodeZone("cnz-c", 3);
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 3);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-b") == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-c") == 0);

    AddLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    AddLink("bgp-router", bgp_router_2.c_str(), "control-node-zone", "cnz-b");
    AddLink("bgp-router", bgp_router_3.c_str(), "control-node-zone", "cnz-c");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 3);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 3);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 1);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-b") == 1);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-c") == 1);

    DelLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 3);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 2);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-b") == 1);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-c") == 1);

    AddLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-b");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 3);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 2);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-b") == 2);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-c") == 1);

    DelLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-b");
    DelLink("bgp-router", bgp_router_2.c_str(), "control-node-zone", "cnz-b");
    DelLink("bgp-router", bgp_router_3.c_str(), "control-node-zone", "cnz-c");
    DeleteBgpRouterConfig("127.0.0.1", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.2", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.3", 0, "ip-fabric");
    DeleteControlNodeZone("cnz-a");
    DeleteControlNodeZone("cnz-b");
    DeleteControlNodeZone("cnz-c");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 0);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-b") == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-c") == 0);
}

TEST_F(BgpRouterCfgTest, Test_5) {
    client->Reset();
    BgpRouterConfig *bgp_router_config =
        agent_->oper_db()->bgp_router_config();

    AddBgpRouterConfig("127.0.0.1", 0, 179,
                       1, "ip-fabric", "control-node");
    AddBgpRouterConfig("127.0.0.2", 0, 179,
                       2, "ip-fabric", "control-node");
    client->WaitForIdle();

    boost::system::error_code ec;
    typedef boost::asio::ip::address_v4 Ipv4Address;

    std::string bgp_router_name = "bgp-router-0-127.0.0.1";
    BgpRouterPtr bgp_router =
        bgp_router_config->GetBgpRouterFromXmppServer(bgp_router_name);
    Ipv4Address ipv4 = Ipv4Address::from_string("127.0.0.1", ec);
    EXPECT_TRUE(bgp_router->ipv4_address() == ipv4);

    std::string bgp_router_ip = "127.0.0.1";
    bgp_router =
        bgp_router_config->GetBgpRouterFromXmppServer(bgp_router_ip);
    ipv4 = Ipv4Address::from_string("127.0.0.1", ec);
    EXPECT_TRUE(bgp_router->ipv4_address() == ipv4);

    bgp_router_name = "bgp-router-0-127.0.0.2";
    bgp_router =
        bgp_router_config->GetBgpRouterFromXmppServer(bgp_router_name);
    ipv4 = Ipv4Address::from_string("127.0.0.2", ec);
    EXPECT_TRUE(bgp_router->ipv4_address() == ipv4);

    bgp_router_ip = "127.0.0.2";
    bgp_router =
        bgp_router_config->GetBgpRouterFromXmppServer(bgp_router_ip);
    ipv4 = Ipv4Address::from_string("127.0.0.2", ec);
    EXPECT_TRUE(bgp_router->ipv4_address() == ipv4);

    bgp_router_name = "bgp-router-0-127.0.0.3";
    bgp_router =
        bgp_router_config->GetBgpRouterFromXmppServer(bgp_router_name);
    EXPECT_TRUE(bgp_router == NULL);

    bgp_router_ip = "127.0.0.3";
    bgp_router =
        bgp_router_config->GetBgpRouterFromXmppServer(bgp_router_ip);
    EXPECT_TRUE(bgp_router == NULL);

    DeleteBgpRouterConfig("127.0.0.1", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.2", 0, "ip-fabric");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 0);
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
