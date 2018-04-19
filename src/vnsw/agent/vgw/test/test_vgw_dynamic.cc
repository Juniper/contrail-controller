/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <stdio.h>
#include <stdlib.h>
#include <boost/assign/list_of.hpp>

#include <io/test/event_manager_test.h>
#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "vgw/cfg_vgw.h"
#include "vgw/vgw.h"
#include "oper/mpls.h"
#include "port_ipc/port_ipc_handler.h"

#include <base/test/task_test_util.h>

using namespace std;
using namespace boost::assign;
using boost::shared_ptr;

void RouterIdDepInit(Agent *agent) {
}

class DynamicVgwTest : public ::testing::Test {
public:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

std::string subnet_list[][5] = { { "4.5.6.0", "5.6.7.0", "6.7.8.0", "7.8.9.0", "8.9.10.0" },
                                 { "5.5.6.0", "6.6.7.0", "7.7.8.0", "8.8.9.0", "9.9.10.0" },
                                 { "6.5.6.0", "7.6.7.0", "8.7.8.0", "9.8.9.0", "10.9.10.0" },
                                 { "7.5.6.0", "8.6.7.0", "9.7.8.0", "10.8.9.0", "11.9.10.0" },
                                 { "8.5.6.0", "9.6.7.0", "10.7.8.0", "11.8.9.0", "12.9.10.0" } };
std::string route_list[] = { "14.15.16.0", "15.16.17.0", "16.17.18.0", "17.18.19.0", "18.19.20.0" };

std::string MakeVgwAddList(uint32_t count, uint32_t route_count) {
    std::stringstream str;
    str << "[";
    bool started = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (started) {
            str << ", ";
        }
        started = true;
        str << "{";
        str << "\"interface\":\"vgw"<< i <<"\", \"routing-instance\":\"";
        str << "default-domain:admin:public" << i << ":public" << i
            << "\", \"subnets\":[";

        bool first = true;
        for (uint32_t j = 0; j < i+1; ++j) {
            if (!first) {
                str << ",";
            }
            first = false;
            str << "{\"ip-address\": \"" << subnet_list[i][j] << "\", \"prefix-len\": " << 24 << "}";
        }
        str << "], \"routes\":[";

        first = true;
        for (uint32_t j = 0; j < route_count; ++j) {
            if (!first) {
                str << ",";
            }
            first = false;
            str << "{\"ip-address\": \"" << route_list[j] << "\", \"prefix-len\": " << 24 << "}";
        }
        str << "]}";
    }
    str << "]";
    return str.str();
}

std::string MakeVgwDeleteList(uint32_t count) {
    std::stringstream str;
    str << "[";
    bool first = true;
    for (uint32_t i = 0; i < count; ++i) {
        if (!first) {
            str << ",";
        }
        first = false;
        str << "{\"interface\":\"vgw"<< i <<"\"}";
    }
    str << "]";
    return str.str();
}

void ValidateVirtualGatewayConfigTable(uint32_t count, uint32_t route_count) {
    VirtualGatewayConfigTable *table =
        Agent::GetInstance()->params()->vgw_config_table();

    for (uint32_t i = 0; i < count; ++i) {
        std::stringstream str;
        str << "vgw" << i;

        VirtualGatewayConfigTable::Table::iterator it =
            table->table().find(VirtualGatewayConfig(str.str()));
        EXPECT_TRUE(it != table->table().end());

        if (it == table->table().end())
            continue;

        std::stringstream inst;
        inst << "default-domain:admin:public" << i << ":public" << i;

        EXPECT_STREQ(it->vrf_name().c_str(), inst.str().c_str());
        EXPECT_STREQ(it->interface_name().c_str(), str.str().c_str());

        EXPECT_EQ(it->subnets().size(), i+1);
        for (uint32_t j = 0; j < i+1; ++j) {
            VirtualGatewayConfig::Subnet subnet = it->subnets()[j];
            EXPECT_STREQ(subnet.ip_.to_string().c_str(), subnet_list[i][j].c_str());
            EXPECT_EQ(subnet.plen_, 24);
        }

        EXPECT_EQ(it->routes().size(), route_count);
        for (uint32_t j = 0; j < route_count; ++j) {
            VirtualGatewayConfig::Subnet subnet = it->routes()[j];
            EXPECT_STREQ(subnet.ip_.to_string().c_str(), route_list[j].c_str());
            EXPECT_EQ(subnet.plen_, 24);
        }
    }
}

void ValidateInterfaceConfiguration(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        std::stringstream str;
        str << "vgw" << i;

        const InetInterface *gw = InetInterfaceGet(str.str().c_str());
        EXPECT_TRUE(gw != NULL);
        if (gw == NULL)
            continue;

        EXPECT_TRUE(gw->sub_type() == InetInterface::SIMPLE_GATEWAY);
        EXPECT_TRUE(gw->ipv4_active());
        EXPECT_NE(gw->label(), 0xFFFFFFFF);
    }
}

// The route for public-vn in fabric-vrf shold point to receive NH
void ValidateSubnetReceiveRoute(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        for (uint32_t j = 0; j < i+1; ++j) {
            InetUnicastRouteEntry *route;
            route = RouteGet(Agent::GetInstance()->fabric_vrf_name(),
                             Ip4Address::from_string(subnet_list[i][j]), 24);
            EXPECT_TRUE(route != NULL);
            if (route == NULL)
                continue;

            const NextHop *nh = route->GetActiveNextHop();
            EXPECT_TRUE(nh != NULL);
            if (nh == NULL)
                return;
            EXPECT_TRUE(nh->GetType() == NextHop::RECEIVE);
        }
    }
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
                TunnelType::MplsType());
    EXPECT_TRUE(static_cast<const InterfaceNH *>(nh)->GetVrf()->GetName() ==
                inet_intf->vrf()->GetName());
}

// The route in public-vn vrf should point to interface-nh for vgw
void ValidateVgwRoute(uint32_t count, uint32_t route_count) {
    for (uint32_t i = 0; i < count; ++i) {
        std::stringstream str;
        str << "vgw" << i;
        std::stringstream inst;
        inst << "default-domain:admin:public" << i << ":public" << i;

        if (route_count == 0) {
            InetUnicastRouteEntry *route;
            route = RouteGet(inst.str(), Ip4Address::from_string("0.0.0.0"), 0);
            EXPECT_TRUE(route != NULL);
            if (route == NULL)
                continue;
            ValidateVgwInterface(route, str.str().c_str());
            continue;
        }

        for (uint32_t j = 0; j < route_count; ++j) {
            InetUnicastRouteEntry *route;
            route = RouteGet(inst.str(), Ip4Address::from_string(route_list[j]), 24);
            EXPECT_TRUE(route != NULL);
            if (route == NULL)
                continue;
            ValidateVgwInterface(route, str.str().c_str());
        }
    }
}

void ValidateVgwDelete(uint32_t count, uint32_t route_count,
                       bool vrf_delete = true) {
    VirtualGatewayConfigTable *table =
        Agent::GetInstance()->params()->vgw_config_table();

    for (uint32_t i = 0; i < count; ++i) {
        std::stringstream str;
        str << "vgw" << i;
        std::stringstream inst;
        inst << "default-domain:admin:public" << i << ":public" << i;

        VirtualGatewayConfigTable::Table::iterator it =
            table->table().find(VirtualGatewayConfig(str.str()));
        EXPECT_TRUE(it == table->table().end());

        const InetInterface *gw = InetInterfaceGet(str.str().c_str());
        EXPECT_TRUE(gw == NULL);

        for (uint32_t j = 0; j < i+1; ++j) {
            InetUnicastRouteEntry *route;
            route = RouteGet(Agent::GetInstance()->fabric_vrf_name(),
                             Ip4Address::from_string(subnet_list[i][j]), 24);
            EXPECT_TRUE(route == NULL);
        }

        if (route_count == 0) {
            InetUnicastRouteEntry *route;
            route = RouteGet(inst.str(), Ip4Address::from_string("0.0.0.0"), 0);
            EXPECT_TRUE(route == NULL);
        }

        for (uint32_t j = 0; j < route_count; ++j) {
            InetUnicastRouteEntry *route;
            route = RouteGet(inst.str(), Ip4Address::from_string(route_list[j]), 24);
            EXPECT_TRUE(route == NULL);
        }
        if (vrf_delete)
            EXPECT_FALSE(VrfFind(inst.str().c_str()));
        else
            EXPECT_TRUE(VrfFind(inst.str().c_str()));
    }
}

// Add and delete virtual gateway
TEST_F(DynamicVgwTest, VgwAddDelete) {
    PortIpcHandler pih(Agent::GetInstance(), "dummy");
    string json = MakeVgwAddList(1, 0);
    string err_msg;
    pih.AddVgwFromJson(json, err_msg);
    client->WaitForIdle();
    usleep(50000);
    ValidateVirtualGatewayConfigTable(1, 0);
    ValidateInterfaceConfiguration(1);
    ValidateSubnetReceiveRoute(1);
    ValidateVgwRoute(1, 0);

    // Delete
    json = MakeVgwDeleteList(1);
    pih.DelVgwFromJson(json, err_msg);
    client->WaitForIdle();
    usleep(50000);
    ValidateVgwDelete(1, 0);
}

// modify virtual gateway configuration
TEST_F(DynamicVgwTest, VgwChange) {
    PortIpcHandler pih(Agent::GetInstance(), "dummy");
    string json = MakeVgwAddList(2, 0);
    string err_msg;
    pih.AddVgwFromJson(json, err_msg);
    client->WaitForIdle();
    usleep(50000);
    ValidateVirtualGatewayConfigTable(2, 0);
    ValidateInterfaceConfiguration(2);
    ValidateSubnetReceiveRoute(2);
    ValidateVgwRoute(2, 0);

    // Change
    json = MakeVgwAddList(4, 2);
    pih.AddVgwFromJson(json, err_msg);
    client->WaitForIdle();
    usleep(50000);
    ValidateVirtualGatewayConfigTable(4, 2);
    ValidateInterfaceConfiguration(4);
    ValidateSubnetReceiveRoute(4);
    ValidateVgwRoute(4, 2);

    // Change
    json = MakeVgwAddList(4, 1);
    pih.AddVgwFromJson(json, err_msg);
    client->WaitForIdle();
    usleep(50000);
    ValidateVirtualGatewayConfigTable(4, 1);
    ValidateInterfaceConfiguration(4);
    ValidateSubnetReceiveRoute(4);
    ValidateVgwRoute(4, 1);

    // Change
    json = MakeVgwAddList(4, 0);
    pih.AddVgwFromJson(json, err_msg);
    client->WaitForIdle();
    usleep(50000);
    ValidateVirtualGatewayConfigTable(4, 0);
    ValidateInterfaceConfiguration(4);
    ValidateSubnetReceiveRoute(4);
    ValidateVgwRoute(4, 0);

    // Delete
    json = MakeVgwDeleteList(4);
    pih.DelVgwFromJson(json, err_msg);
    client->WaitForIdle();
    usleep(50000);
    ValidateVgwDelete(4, 0);
}

// create a VM, add VGW to the same VRF, delete VM, check that VRF isnt deleted
TEST_F(DynamicVgwTest, ExistingVrfAddVgw) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    CreateVmportEnv(input, 1, 0, NULL, "default-domain:admin:public0:public0");
    client->WaitForIdle();
    client->Reset();

    PortIpcHandler pih(Agent::GetInstance(), "dummy");
    string json = MakeVgwAddList(1, 0);
    string err_msg;
    pih.AddVgwFromJson(json, err_msg);
    client->WaitForIdle();
    usleep(50000);
    ValidateVirtualGatewayConfigTable(1, 0);
    ValidateInterfaceConfiguration(1);
    ValidateSubnetReceiveRoute(1);
    ValidateVgwRoute(1, 0);

    // Delete VM
    DeleteVmportEnv(input, 1, 1, 0, NULL,
                    "default-domain:admin:public0:public0");
    client->WaitForIdle();
    EXPECT_TRUE(VrfFind("default-domain:admin:public0:public0"));
    ValidateVirtualGatewayConfigTable(1, 0);
    ValidateInterfaceConfiguration(1);
    ValidateSubnetReceiveRoute(1);
    ValidateVgwRoute(1, 0);

    // Delete VGW
    json = MakeVgwDeleteList(4);
    pih.DelVgwFromJson(json, err_msg);
    client->WaitForIdle();
    usleep(50000);
    ValidateVgwDelete(1, 0);
}

// create VGW, add a VM to the same VRF, delete VGW, check that VRF isnt deleted
TEST_F(DynamicVgwTest, ExistingVgwAddVrf) {
    PortIpcHandler pih(Agent::GetInstance(), "dummy");
    string json = MakeVgwAddList(1, 0);
    string err_msg;
    pih.AddVgwFromJson(json, err_msg);
    client->WaitForIdle();
    usleep(50000);
    ValidateVirtualGatewayConfigTable(1, 0);
    ValidateInterfaceConfiguration(1);
    ValidateSubnetReceiveRoute(1);
    ValidateVgwRoute(1, 0);

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    CreateVmportEnv(input, 1, 0, NULL, "default-domain:admin:public0:public0");
    client->WaitForIdle();
    client->Reset();

    // Delete VGW
    json = MakeVgwDeleteList(4);
    pih.DelVgwFromJson(json, err_msg);
    client->WaitForIdle();
    usleep(50000);
    ValidateVgwDelete(1, 0, false);

    // Delete VM
    DeleteVmportEnv(input, 1, 1, 0, NULL,
                    "default-domain:admin:public0:public0");
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VrfFind("default-domain:admin:public0:public0")
                         == false));
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = VGwInit("controller/src/vnsw/agent/test/vnswa_cfg.ini",
                     ksync_init);

    int ret = RUN_ALL_TESTS();

    TestShutdown();
    delete client;
    return ret;
}
