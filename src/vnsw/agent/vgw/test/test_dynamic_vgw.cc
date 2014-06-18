/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <boost/assign/list_of.hpp>

#include <io/test/event_manager_test.h>
#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "vgw/cfg_vgw.h"
#include "vgw/vgw.h"
#include "oper/mpls.h"

#include <async/TAsioAsync.h>
#include <protocol/TBinaryProtocol.h>
#include <gen-cpp/InstanceService.h>
#include <async/TFuture.h>
#include <base/test/task_test_util.h>

using namespace std;
using namespace boost::assign;
using namespace apache::thrift;
using boost::shared_ptr;

extern void InstanceInfoServiceServerInit(Agent *agent);
boost::shared_ptr<InstanceServiceAsyncClient> client_service;
EventManager *client_evm;
ServerThread *client_thread;
bool connection_complete = false;

void RouterIdDepInit(Agent *agent) {
}

class DynamicVgwTest : public ::testing::Test {
public:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

void AddVgwCallback(bool ret) {
    std::cout << "Return value " << ret << std::endl;
}

void AddVgwErrback(const InstanceService_AddVirtualGateway_result& result) {
    std::cout << "Exception caught " << __FUNCTION__ << std::endl;
}

void DeleteVgwCallback(bool ret) {
    std::cout << "DeletePort " << ret << std::endl;
}

void DeleteVgwErrback(const InstanceService_DeleteVirtualGateway_result& result) {
    std::cout << "Exception caught " << __FUNCTION__ << std::endl;
}

void ConnectCallback(bool ret) {
    std::cout << "Connect, " << "Return value " << ret << std::endl;
}

void ConnectErrback(const InstanceService_ConnectForVirtualGateway_result& result) {
    std::cout << "Exception caught " << __FUNCTION__ << std::endl;
}

void connected(boost::shared_ptr<InstanceServiceAsyncClient> inst_client) {
    std::cout << "connected!!!" << std::endl;
    client_service = inst_client;
    connection_complete = true;
}

void ConnectionStart() {
    client_evm = new EventManager();
    client_thread = new ServerThread(client_evm);
    boost::shared_ptr<protocol::TProtocolFactory> 
            protocolFactory(new protocol::TBinaryProtocolFactory());
    boost::shared_ptr<async::TAsioClient> nova_client (
        new async::TAsioClient(
            *(client_evm->io_service()), protocolFactory, protocolFactory));
    client_thread->Start();
    nova_client->connect("localhost", 9090, connected);
    TASK_UTIL_EXPECT_EQ(true, connection_complete);
}

std::string subnet_list[][5] = { { "4.5.6.0", "5.6.7.0", "6.7.8.0", "7.8.9.0", "8.9.10.0" },
                                 { "5.5.6.0", "6.6.7.0", "7.7.8.0", "8.8.9.0", "9.9.10.0" },
                                 { "6.5.6.0", "7.6.7.0", "8.7.8.0", "9.8.9.0", "10.9.10.0" },
                                 { "7.5.6.0", "8.6.7.0", "9.7.8.0", "10.8.9.0", "11.9.10.0" },
                                 { "8.5.6.0", "9.6.7.0", "10.7.8.0", "11.8.9.0", "12.9.10.0" } };
std::string route_list[] = { "14.15.16.0", "15.16.17.0", "16.17.18.0", "17.18.19.0", "18.19.20.0" };

void MakeVgwAddList(std::vector<VirtualGatewayRequest> &list, uint32_t count,
                        uint32_t route_count) {
    for (uint32_t i = 0; i < count; ++i) {
        VirtualGatewayRequest gw;

        std::stringstream str;
        str << "vgw" << i;
        gw.interface_name = str.str();

        std::stringstream inst;
        inst << "default-domain:admin:public" << i << ":public" << i;
        gw.routing_instance = inst.str();

        for (uint32_t j = 0; j < i+1; ++j) {
            Subnet subnet;
            subnet.prefix = subnet_list[i][j];
            subnet.plen = 24;
            gw.subnets.push_back(subnet);
        }

        SubnetList subnet_list;
        for (uint32_t j = 0; j < route_count; ++j) {
            Subnet subnet;
            subnet.prefix = route_list[j];
            subnet.plen = 24;
            subnet_list.push_back(subnet);
        }
        if (route_count)
            gw.__set_routes(subnet_list);

        list.push_back(gw);
    }
}

void MakeVgwDeleteList(std::vector<std::string> &list, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        std::stringstream str;
        str << "vgw" << i;
        list.push_back(str.str());
    }
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
            Inet4UnicastRouteEntry *route;
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
void ValidateVgwRoute(uint32_t count, uint32_t route_count) {
    for (uint32_t i = 0; i < count; ++i) {
        std::stringstream str;
        str << "vgw" << i;
        std::stringstream inst;
        inst << "default-domain:admin:public" << i << ":public" << i;

        if (route_count == 0) {
            Inet4UnicastRouteEntry *route;
            route = RouteGet(inst.str(), Ip4Address::from_string("0.0.0.0"), 0);
            EXPECT_TRUE(route != NULL);
            if (route == NULL)
                continue;
            ValidateVgwInterface(route, str.str().c_str());
            continue;
        }

        for (uint32_t j = 0; j < route_count; ++j) {
            Inet4UnicastRouteEntry *route;
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
            Inet4UnicastRouteEntry *route;
            route = RouteGet(Agent::GetInstance()->fabric_vrf_name(),
                             Ip4Address::from_string(subnet_list[i][j]), 24);
            EXPECT_TRUE(route == NULL);
        }

        if (route_count == 0) {
            Inet4UnicastRouteEntry *route;
            route = RouteGet(inst.str(), Ip4Address::from_string("0.0.0.0"), 0);
            EXPECT_TRUE(route == NULL);
        }

        for (uint32_t j = 0; j < route_count; ++j) {
            Inet4UnicastRouteEntry *route;
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
    std::vector<VirtualGatewayRequest> gateway_list;
    MakeVgwAddList(gateway_list, 1, 0);
    client_service->AddVirtualGateway(gateway_list).setCallback(
        boost::bind(&AddVgwCallback, _1)).setErrback(AddVgwErrback);
    client->WaitForIdle();
    usleep(50000);
    ValidateVirtualGatewayConfigTable(1, 0);
    ValidateInterfaceConfiguration(1);
    ValidateSubnetReceiveRoute(1);
    ValidateVgwRoute(1, 0);

    // Delete 
    std::vector<std::string> interface_list;
    MakeVgwDeleteList(interface_list, 1);
    client_service->DeleteVirtualGateway(interface_list).setCallback(
        boost::bind(&DeleteVgwCallback, _1)).setErrback(DeleteVgwErrback);
    client->WaitForIdle();
    usleep(50000);
    ValidateVgwDelete(1, 0);
}

// modify virtual gateway configuration
TEST_F(DynamicVgwTest, VgwChange) {
    std::vector<VirtualGatewayRequest> gateway_list;
    MakeVgwAddList(gateway_list, 2, 0);
    client_service->AddVirtualGateway(gateway_list).setCallback(
        boost::bind(&AddVgwCallback, _1)).setErrback(AddVgwErrback);
    client->WaitForIdle();
    usleep(50000);
    ValidateVirtualGatewayConfigTable(2, 0);
    ValidateInterfaceConfiguration(2);
    ValidateSubnetReceiveRoute(2);
    ValidateVgwRoute(2, 0);

    // Change
    gateway_list.clear();
    MakeVgwAddList(gateway_list, 4, 2);
    client_service->AddVirtualGateway(gateway_list).setCallback(
        boost::bind(&AddVgwCallback, _1)).setErrback(AddVgwErrback);
    client->WaitForIdle();
    usleep(50000);
    ValidateVirtualGatewayConfigTable(4, 2);
    ValidateInterfaceConfiguration(4);
    ValidateSubnetReceiveRoute(4);
    ValidateVgwRoute(4, 2);

    // Change
    gateway_list.clear();
    MakeVgwAddList(gateway_list, 4, 1);
    client_service->AddVirtualGateway(gateway_list).setCallback(
        boost::bind(&AddVgwCallback, _1)).setErrback(AddVgwErrback);
    client->WaitForIdle();
    usleep(50000);
    ValidateVirtualGatewayConfigTable(4, 1);
    ValidateInterfaceConfiguration(4);
    ValidateSubnetReceiveRoute(4);
    ValidateVgwRoute(4, 1);

    // Change
    gateway_list.clear();
    MakeVgwAddList(gateway_list, 4, 0);
    client_service->AddVirtualGateway(gateway_list).setCallback(
        boost::bind(&AddVgwCallback, _1)).setErrback(AddVgwErrback);
    client->WaitForIdle();
    usleep(50000);
    ValidateVirtualGatewayConfigTable(4, 0);
    ValidateInterfaceConfiguration(4);
    ValidateSubnetReceiveRoute(4);
    ValidateVgwRoute(4, 0);

    // Delete 
    std::vector<std::string> interface_list;
    MakeVgwDeleteList(interface_list, 4);
    client_service->DeleteVirtualGateway(interface_list).setCallback(
        boost::bind(&DeleteVgwCallback, _1)).setErrback(DeleteVgwErrback);
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

    std::vector<VirtualGatewayRequest> gateway_list;
    MakeVgwAddList(gateway_list, 1, 0);
    client_service->AddVirtualGateway(gateway_list).setCallback(
        boost::bind(&AddVgwCallback, _1)).setErrback(AddVgwErrback);
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
    std::vector<std::string> interface_list;
    MakeVgwDeleteList(interface_list, 1);
    client_service->DeleteVirtualGateway(interface_list).setCallback(
        boost::bind(&DeleteVgwCallback, _1)).setErrback(DeleteVgwErrback);
    client->WaitForIdle();
    usleep(50000);
    ValidateVgwDelete(1, 0);
}

// create VGW, add a VM to the same VRF, delete VGW, check that VRF isnt deleted
TEST_F(DynamicVgwTest, ExistingVgwAddVrf) {
    std::vector<VirtualGatewayRequest> gateway_list;
    MakeVgwAddList(gateway_list, 1, 0);
    client_service->AddVirtualGateway(gateway_list).setCallback(
        boost::bind(&AddVgwCallback, _1)).setErrback(AddVgwErrback);
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
    std::vector<std::string> interface_list;
    MakeVgwDeleteList(interface_list, 1);
    client_service->DeleteVirtualGateway(interface_list).setCallback(
        boost::bind(&DeleteVgwCallback, _1)).setErrback(DeleteVgwErrback);
    client->WaitForIdle();
    usleep(50000);
    ValidateVgwDelete(1, 0, false);

    // Delete VM
    DeleteVmportEnv(input, 1, 1, 0, NULL,
                    "default-domain:admin:public0:public0");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("default-domain:admin:public0:public0"));
}

// Connect, Configure GWs, Connect again, reconfigure some GWs
// Check that older GWs are deleted after timeout
TEST_F(DynamicVgwTest, Reconnect) {
    client_service->AuditTimerForVirtualGateway(20);
    client_service->ConnectForVirtualGateway().setCallback(
        boost::bind(&ConnectCallback, _1)).setErrback(ConnectErrback);
    client->WaitForIdle();

    std::vector<VirtualGatewayRequest> gateway_list;
    MakeVgwAddList(gateway_list, 4, 4);
    client_service->AddVirtualGateway(gateway_list).setCallback(
        boost::bind(&AddVgwCallback, _1)).setErrback(AddVgwErrback);
    client->WaitForIdle();
    usleep(50000);
    ValidateVirtualGatewayConfigTable(4, 4);
    ValidateInterfaceConfiguration(4);
    ValidateSubnetReceiveRoute(4);
    ValidateVgwRoute(4, 4);

    client_service->ConnectForVirtualGateway().setCallback(
        boost::bind(&ConnectCallback, _1)).setErrback(ConnectErrback);
    client->WaitForIdle();

    gateway_list.clear();
    MakeVgwAddList(gateway_list, 2, 4);
    client_service->AddVirtualGateway(gateway_list).setCallback(
        boost::bind(&AddVgwCallback, _1)).setErrback(AddVgwErrback);
    client->WaitForIdle();
    usleep(50000);

    usleep(20000);  // wait for timeout

    ValidateVirtualGatewayConfigTable(2, 4);
    ValidateInterfaceConfiguration(2);
    ValidateSubnetReceiveRoute(2);
    ValidateVgwRoute(2, 4);

    VirtualGatewayConfigTable *table = 
        Agent::GetInstance()->params()->vgw_config_table();
    for (uint32_t i = 2; i < 4; ++i) {
        std::stringstream str;
        str << "vgw" << i;

        VirtualGatewayConfigTable::Table::iterator it = 
            table->table().find(VirtualGatewayConfig(str.str()));
        EXPECT_TRUE(it == table->table().end());

        const InetInterface *gw = InetInterfaceGet(str.str().c_str());
        EXPECT_TRUE(gw == NULL);

        for (uint32_t j = 0; j < i+1; ++j) {
            Inet4UnicastRouteEntry *route;
            route = RouteGet(Agent::GetInstance()->fabric_vrf_name(),
                             Ip4Address::from_string(subnet_list[i][j]), 24);
            EXPECT_TRUE(route == NULL);
        }
    }
    EXPECT_FALSE(VrfFind("default-domain:admin:public2:public2"));
    EXPECT_FALSE(VrfFind("default-domain:admin:public3:public3"));

    std::vector<std::string> interface_list;
    MakeVgwDeleteList(interface_list, 2);
    client_service->DeleteVirtualGateway(interface_list).setCallback(
        boost::bind(&DeleteVgwCallback, _1)).setErrback(DeleteVgwErrback);
    client->WaitForIdle();
    usleep(50000);
    ValidateVgwDelete(2, 0);
    client_service->AuditTimerForVirtualGateway(60000);
}

int main(int argc, char **argv) {
    GETUSERARGS();

    // client = TestInit(init_file, ksync_init, true, true);
    client = VGwInit("controller/src/vnsw/agent/test/vnswa_cfg.ini",
                     ksync_init);
    InstanceInfoServiceServerInit(Agent::GetInstance());
    usleep(100000);
    client->WaitForIdle();

    ConnectionStart();

    int ret = RUN_ALL_TESTS();

    client_evm->Shutdown();
    client_thread->Join();
    delete client_evm;
    delete client_thread;

    TestShutdown();
    delete client;
    return ret;
}
