/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "vgw/cfg_vgw.h"
#include "vgw/vgw.h"

namespace opt = boost::program_options;

void RouterIdDepInit() {
}

class VgwTest : public ::testing::Test {
public:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    opt::options_description desc;
    opt::variables_map var_map;
};

TEST_F(VgwTest, conf_file_1) {
    AgentParam param;

    VirtualGatewayConfig *cfg = Agent::GetInstance()->params()->vgw_config();
    EXPECT_TRUE(cfg != NULL);
    if (cfg == NULL)
        return;

    EXPECT_STREQ(cfg->vrf().c_str(), "default-domain:admin:public:public");
    EXPECT_STREQ(cfg->interface().c_str(), "vgw");
    EXPECT_STREQ(cfg->ip().to_string().c_str(), "1.1.1.1");
    EXPECT_EQ(cfg->plen(), 24);
}

// The route for public-vn in fabric-vrf shold point to receive NH
TEST_F(VgwTest, fabric_route_1) {
    Inet4UnicastRouteEntry *route;
    route = RouteGet(Agent::GetInstance()->GetDefaultVrf(),
                     Ip4Address::from_string("1.1.1.0"), 24);
    EXPECT_TRUE(route != NULL);
    if (route == NULL)
        return;

    const NextHop *nh = route->GetActiveNextHop();
    EXPECT_TRUE(nh != NULL);
    if (nh == NULL)
        return;

    EXPECT_TRUE(nh->GetType() == NextHop::RECEIVE);
}

// The default route in public-vn vrf should point to interface-nh for vgw
TEST_F(VgwTest, vn_route_1) {
    Inet4UnicastRouteEntry *route;

    VirtualGatewayConfig *cfg = Agent::GetInstance()->params()->vgw_config();
    EXPECT_TRUE(cfg != NULL);
    if (cfg == NULL)
        return;

    route = RouteGet(cfg->vrf(), Ip4Address::from_string("0.0.0.0"), 0);
    EXPECT_TRUE(route != NULL);
    if (route == NULL)
        return;

    const NextHop *nh = route->GetActiveNextHop();
    EXPECT_TRUE(nh != NULL);
    if (nh == NULL)
        return;

    EXPECT_TRUE(nh->GetType() == NextHop::INTERFACE);
    if (nh->GetType() != NextHop::INTERFACE)
        return;

    const Interface *intf = static_cast<const InterfaceNH *>(nh)->GetInterface();
    EXPECT_TRUE(intf != NULL);
    if (intf == NULL)
        return;

    EXPECT_TRUE(intf->type() == Interface::INET);
    if (intf->type() != Interface::INET)
        return;

    const InetInterface *vhost_intf;
    vhost_intf = static_cast<const InetInterface *>(intf);
    EXPECT_TRUE(vhost_intf->sub_type() == InetInterface::SIMPLE_GATEWAY);

}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = VGwInit( "controller/src/vnsw/agent/vgw/test/cfg.xml",
                      ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
