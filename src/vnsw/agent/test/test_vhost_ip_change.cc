/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test_cmn_util.h"

void RouterIdDepInit(Agent *agent) {
}

class IpChangeTest : public ::testing::Test {
public:
    static void TestSetup(bool ksync_init) {
        ksync_init_ = ksync_init;
    }
protected:
    static bool ksync_init_;
};

bool IpChangeTest::ksync_init_;

//Change vhost0 IP Address and verify the router-id and routes for vhost0
TEST_F(IpChangeTest, vhost_ip_change) {
    if (ksync_init_) {
        client->WaitForIdle();
        system("ip addr add 27.0.0.1/24 dev vhost0");
        client->WaitForIdle();
        sleep(1);
        client->WaitForIdle();
        Ip4Address addr = Ip4Address::from_string("27.0.0.1");
        EXPECT_TRUE(ResolvRouteFind(Agent::GetInstance()->fabric_vrf_name(), addr, 24));
        EXPECT_TRUE(VhostRecvRouteFind(Agent::GetInstance()->fabric_vrf_name(), addr, 32));
        EXPECT_TRUE(RouterIdMatch(addr));

        system("ip addr add 49.0.0.1/24 dev vhost0");
        client->WaitForIdle();
        sleep(1);
        client->WaitForIdle();
        addr = Ip4Address::from_string("49.0.0.1");
        EXPECT_TRUE(ResolvRouteFind(Agent::GetInstance()->fabric_vrf_name(), addr, 24));
        EXPECT_TRUE(VhostRecvRouteFind(Agent::GetInstance()->fabric_vrf_name(), addr, 32));
        EXPECT_TRUE(RouterIdMatch(addr));

        system("ip addr del 49.0.0.1/24 dev vhost0");
        system("ip addr del 27.0.0.1/24 dev vhost0");
    }
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, false);
    IpChangeTest::TestSetup(ksync_init);
    
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
