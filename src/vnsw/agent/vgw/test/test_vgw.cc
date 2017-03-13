/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "vgw/cfg_vgw.h"
#include "vgw/vgw.h"
#include "oper/mpls.h"
#include "pkt/test/test_flow_util.h"

namespace opt = boost::program_options;

void RouterIdDepInit(Agent *agent) {
}

class VgwTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
    }

    virtual void TearDown() {
    }

    opt::options_description desc;
    opt::variables_map var_map;
    Agent *agent_;
    FlowProto *flow_proto_;
};

TEST_F(VgwTest, conf_file_1) {
    AgentParam param(Agent::GetInstance());

    VirtualGatewayConfigTable *table =
        Agent::GetInstance()->params()->vgw_config_table();
    VirtualGatewayConfigTable::Table::iterator it =
        table->table().find(VirtualGatewayConfig("vgw"));
    EXPECT_TRUE(it != table->table().end());

    if (it == table->table().end())
        return;

    EXPECT_STREQ(it->vrf_name().c_str(), "default-domain:admin:public:public");
    EXPECT_STREQ(it->interface_name().c_str(), "vgw");

    VirtualGatewayConfig::Subnet subnet = it->subnets()[0];
    EXPECT_STREQ(subnet.ip_.to_string().c_str(), "1.1.1.1");
    EXPECT_EQ(subnet.plen_, 24);

    it = table->table().find(VirtualGatewayConfig("vgw1"));
    EXPECT_TRUE(it != table->table().end());

    if (it == table->table().end())
        return;

    EXPECT_STREQ(it->vrf_name().c_str(), "default-domain:admin:public1:public1");
    EXPECT_STREQ(it->interface_name().c_str(), "vgw1");
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
        EXPECT_STREQ(subnet.ip_.to_string().c_str(), "0.0.0.0");
        EXPECT_EQ(subnet.plen_, 0);

        subnet = it->routes()[1];
        EXPECT_STREQ(subnet.ip_.to_string().c_str(), "10.10.10.1");
        EXPECT_EQ(subnet.plen_, 24);
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
    InetUnicastRouteEntry *route;
    route = RouteGet(Agent::GetInstance()->fabric_vrf_name(),
                     Ip4Address::from_string("1.1.1.0"), 24);
    EXPECT_TRUE(route != NULL);
    if (route == NULL)
        return;

    const NextHop *nh = route->GetActiveNextHop();
    EXPECT_TRUE(nh != NULL);
    if (nh == NULL)
        return;
    EXPECT_TRUE(nh->GetType() == NextHop::RECEIVE);

    route = RouteGet(Agent::GetInstance()->fabric_vrf_name(),
                     Ip4Address::from_string("2.2.1.0"), 24);
    EXPECT_TRUE(route != NULL);
    if (route == NULL)
        return;

    nh = route->GetActiveNextHop();
    EXPECT_TRUE(nh != NULL);
    if (nh == NULL)
        return;
    EXPECT_TRUE(nh->GetType() == NextHop::RECEIVE);

    route = RouteGet(Agent::GetInstance()->fabric_vrf_name(),
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

static void ValidateVgwInterface(InetUnicastRouteEntry *route,
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
    InetUnicastRouteEntry *route;

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
    InetUnicastRouteEntry *route;
    route = RouteGet("default-domain:admin:public:public",
                     Ip4Address::from_string("0.0.0.0"), 0);
    EXPECT_TRUE(route != NULL);
    if (route == NULL)
        return;
    ValidateVgwInterface(route, "vgw");

    AddEncapList("MPLSoUDP", "MPLSoGRE", "VXLAN");
    client->WaitForIdle();
    route = RouteGet("default-domain:admin:public:public",
                     Ip4Address::from_string("0.0.0.0"), 0);
    EXPECT_EQ(route->GetActivePath()->tunnel_type(), TunnelType::MPLS_GRE);

    AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
    client->WaitForIdle();
    route = RouteGet("default-domain:admin:public:public",
                     Ip4Address::from_string("0.0.0.0"), 0);
    EXPECT_EQ(route->GetActivePath()->tunnel_type(), TunnelType::MPLS_GRE);

    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    client->WaitForIdle();
    route = RouteGet("default-domain:admin:public:public",
                     Ip4Address::from_string("0.0.0.0"), 0);
    EXPECT_EQ(route->GetActivePath()->tunnel_type(), TunnelType::MPLS_GRE);

    route = RouteGet("default-domain:admin:public:public",
                     Ip4Address::from_string("0.0.0.0"), 0);
    EXPECT_TRUE(route != NULL);
    ValidateVgwInterface(route, "vgw");
}

TEST_F(VgwTest, IngressFlow_1) {
    AddVmPort("vnet2", 2, "2.2.2.3", "00:00:02:02:02:03",
              "default-domain:admin:public:public",
              "default-domain:admin:public", 2, "vm2", 2, "instance-ip-2", 2);
    WAIT_FOR(1000, 100, (VmPortFind(2) == true));
    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(2));
    EXPECT_TRUE(vmi != NULL);

    const InetInterface *gw = InetInterfaceGet("vgw");
    EXPECT_TRUE(gw != NULL);
    if (gw == NULL)
        return;
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "2.2.2.2", "100.100.100.100", 1, 0, 0,
                        "default-domain:admin:public:public", vmi->id()),
        },
    };
    CreateFlow(flow, 1);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(vmi->vrf_id(), "2.2.2.2", "100.100.100.100",
                              1, 0, 0, vmi->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);

    DeleteFlow(flow, 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (flow_proto_->FlowCount() == 0));

    DelVmPort("vnet2", 2, "2.2.2.3", "00:00:02:02:02:03",
              "default-domain:admin:public:public",
              "default-domain:admin:public", 2, "vm2", 2, "instance-ip-2", 2);
    client->WaitForIdle();

    EXPECT_FALSE(VmPortFind(2));
    WAIT_FOR(1000, 100, (VmPortFind(2) == false));
}

TEST_F(VgwTest, EgressFlow_1) {
    AddVmPort("vnet2", 2, "2.2.2.3", "00:00:02:02:02:03",
              "default-domain:admin:public:public",
              "default-domain:admin:public", 2, "vm2", 2, "instance-ip-2", 2);
    WAIT_FOR(1000, 100, (VmPortFind(2) == true));
    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(2));
    EXPECT_TRUE(vmi != NULL);

    const InetInterface *gw = InetInterfaceGet("vgw");
    EXPECT_TRUE(gw != NULL);
    if (gw == NULL)
        return;
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "100.100.100.100", "2.2.2.2", 1, 0, 0,
                        "default-domain:admin:public:public", "20.20.20.20",
                        gw->label()),
        },
    };
    CreateFlow(flow, 1);
    client->WaitForIdle();

    MplsLabel *label = GetActiveLabel(gw->label());
    FlowEntry *fe = FlowGet(vmi->vrf_id(), "100.100.100.100", "2.2.2.2",
                              1, 0, 0, label->nexthop()->id());
    EXPECT_TRUE(fe != NULL);

    DeleteFlow(flow, 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (flow_proto_->FlowCount() == 0));

    DelVmPort("vnet2", 2, "2.2.2.3", "00:00:02:02:02:03",
              "default-domain:admin:public:public",
              "default-domain:admin:public", 2, "vm2", 2, "instance-ip-2", 2);
    client->WaitForIdle();

    EXPECT_FALSE(VmPortFind(2));
    WAIT_FOR(1000, 100, (VmPortFind(2) == false));
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = VGwInit( "controller/src/vnsw/agent/vgw/test/cfg.ini",
                      ksync_init);
    int ret = RUN_ALL_TESTS();
    std::vector<VirtualGatewayInfo> vgw_info_list;
    vgw_info_list.push_back(VirtualGatewayInfo("vgw"));
    vgw_info_list.push_back(VirtualGatewayInfo("vgw1"));
    boost::shared_ptr<VirtualGatewayData>
        vgw_data(new VirtualGatewayData(VirtualGatewayData::Delete,
                                        vgw_info_list, 0));
    Agent::GetInstance()->params()->vgw_config_table()->Enqueue(vgw_data);
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
