/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "vgw/cfg_vgw.h"
#include "vgw/vgw.h"
#include "oper/mpls.h"

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

    VirtualGatewayConfigTable *table = 
        Agent::GetInstance()->params()->vgw_config_table();
    VirtualGatewayConfigTable::Table::iterator it = 
        table->table().find(VirtualGatewayConfig("vgw"));
    EXPECT_TRUE(it != table->table().end());

    if (it == table->table().end())
        return;

    EXPECT_STREQ(it->vrf().c_str(), "default-domain:admin:public:public");
    EXPECT_STREQ(it->interface().c_str(), "vgw");

    VirtualGatewayConfig::Subnet subnet = it->subnets()[0];
    EXPECT_STREQ(subnet.ip_.to_string().c_str(), "1.1.1.1");
    EXPECT_EQ(subnet.plen_, 24);

    it = table->table().find(VirtualGatewayConfig("vgw1"));
    EXPECT_TRUE(it != table->table().end());

    if (it == table->table().end())
        return;

    EXPECT_STREQ(it->vrf().c_str(), "default-domain:admin:public1:public1");
    EXPECT_STREQ(it->interface().c_str(), "vgw1");
    EXPECT_EQ(it->subnets().size(), (unsigned int) 2);
    if (it->routes().size() == 2) {
        subnet = it->subnets()[0];
        EXPECT_STREQ(subnet.ip_.to_string().c_str(), "2.2.1.0");
        EXPECT_EQ(subnet.plen_, 24);

        subnet = it->subnets()[1];
        EXPECT_STREQ(subnet.ip_.to_string().c_str(), "2.2.2.0");
        EXPECT_EQ(subnet.plen_, 24);
    }

    EXPECT_EQ(it->routes().size(), (unsigned int) 2);
    if (it->routes().size() == 2) {
        subnet = it->routes()[0];
        EXPECT_STREQ(subnet.ip_.to_string().c_str(), "10.10.10.1");
        EXPECT_EQ(subnet.plen_, 24);

        subnet = it->routes()[1];
        EXPECT_STREQ(subnet.ip_.to_string().c_str(), "0.0.0.0");
        EXPECT_EQ(subnet.plen_, 0);
    }
}

// Validate interface configuration
TEST_F(VgwTest, interface_1) {
    const InetInterface *gw = InetInterfaceGet("vgw");
    EXPECT_TRUE(gw != NULL);
    if (gw == NULL)
        return;

    EXPECT_TRUE(gw->sub_type() == InetInterface::SIMPLE_GATEWAY);
    EXPECT_TRUE(gw->ipv4_active());
    EXPECT_NE(gw->label(), 0xFFFFFFFF);

    gw = InetInterfaceGet("vgw1");
    EXPECT_TRUE(gw != NULL);
    if (gw == NULL)
        return;

    EXPECT_TRUE(gw->sub_type() == InetInterface::SIMPLE_GATEWAY);
    EXPECT_TRUE(gw->ipv4_active());
    EXPECT_NE(gw->label(), 0xFFFFFFFF);
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

    route = RouteGet(Agent::GetInstance()->GetDefaultVrf(),
                     Ip4Address::from_string("2.2.1.0"), 24);
    EXPECT_TRUE(route != NULL);
    if (route == NULL)
        return;

    nh = route->GetActiveNextHop();
    EXPECT_TRUE(nh != NULL);
    if (nh == NULL)
        return;
    EXPECT_TRUE(nh->GetType() == NextHop::RECEIVE);

    route = RouteGet(Agent::GetInstance()->GetDefaultVrf(),
                     Ip4Address::from_string("2.2.2.0"), 24);
    EXPECT_TRUE(route != NULL);
    if (route == NULL)
        return;

    nh = route->GetActiveNextHop();
    EXPECT_TRUE(nh != NULL);
    if (nh == NULL)
        return;
    EXPECT_TRUE(nh->GetType() == NextHop::RECEIVE);
}

static void ValidateVgwInterface(Inet4UnicastRouteEntry *route,
                                 const char *name) {
    const NextHop *nh = route->GetActiveNextHop();
    EXPECT_TRUE(nh != NULL);
    if (nh == NULL)
        return;

    EXPECT_TRUE(nh->GetType() == NextHop::INTERFACE);
    if (nh->GetType() != NextHop::INTERFACE)
        return;

    const Interface *intf = 
        static_cast<const InterfaceNH *>(nh)->GetInterface();
    EXPECT_TRUE(intf != NULL);
    if (intf == NULL)
        return;

    EXPECT_TRUE(intf->type() == Interface::INET);
    if (intf->type() != Interface::INET)
        return;

    const InetInterface *inet_intf;
    inet_intf = static_cast<const InetInterface *>(intf);
    EXPECT_TRUE(inet_intf->sub_type() == InetInterface::SIMPLE_GATEWAY);
    EXPECT_STREQ(intf->name().c_str(), name);
    EXPECT_TRUE(route->GetActivePath()->GetTunnelBmap() ==
                TunnelType::GREType());
    EXPECT_TRUE(static_cast<const InterfaceNH *>(nh)->GetVrf()->GetName() ==
                inet_intf->vrf()->GetName());
}

// The route in public-vn vrf should point to interface-nh for vgw
TEST_F(VgwTest, vn_route_1) {
    Inet4UnicastRouteEntry *route;

    route = RouteGet("default-domain:admin:public:public",
                     Ip4Address::from_string("0.0.0.0"), 0);
    EXPECT_TRUE(route != NULL);
    if (route == NULL)
        return;
    ValidateVgwInterface(route, "vgw");

    route = RouteGet("default-domain:admin:public1:public1",
                     Ip4Address::from_string("0.0.0.0"), 0);
    EXPECT_TRUE(route != NULL);
    if (route == NULL)
        return;
    ValidateVgwInterface(route, "vgw1");

    route = RouteGet("default-domain:admin:public1:public1",
                     Ip4Address::from_string("10.10.10.0"), 24);
    EXPECT_TRUE(route != NULL);
    if (route == NULL)
        return;
    ValidateVgwInterface(route, "vgw1");
}

//Gateway vrf should not be deleted
TEST_F(VgwTest, vrf_delete) {
    //Add IF Nodes
    AddVrf("default-domain:admin:public:public");
    AddVrf("default-domain:admin:public1:public1");
    client->WaitForIdle();

    EXPECT_TRUE(VrfFind("default-domain:admin:public:public"));
    EXPECT_TRUE(VrfFind("default-domain:admin:public1:public1"));

    DelVrf("default-domain:admin:public:public");
    DelVrf("default-domain:admin:public1:public1");
    client->WaitForIdle();

    EXPECT_TRUE(VrfFind("default-domain:admin:public:public"));
    EXPECT_TRUE(VrfFind("default-domain:admin:public1:public1"));
}

TEST_F(VgwTest, RouteResync) {
    Inet4UnicastRouteEntry *route;
    route = RouteGet("default-domain:admin:public:public",
                     Ip4Address::from_string("0.0.0.0"), 0);
    EXPECT_TRUE(route != NULL);
    if (route == NULL)
        return;
    ValidateVgwInterface(route, "vgw");

    AddEncapList("MPLSoUDP", "MPLSoGRE", "VXLAN");
    client->WaitForIdle();
    AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
    client->WaitForIdle();

    route = RouteGet("default-domain:admin:public:public",
                     Ip4Address::from_string("0.0.0.0"), 0);
    EXPECT_TRUE(route != NULL);
    ValidateVgwInterface(route, "vgw");
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = VGwInit( "controller/src/vnsw/agent/vgw/test/cfg.ini",
                      ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
