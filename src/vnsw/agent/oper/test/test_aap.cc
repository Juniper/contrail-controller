/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/socket.h>
#include <linux/netlink.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>

#include "testing/gunit.h"

#include <boost/uuid/string_generator.hpp>

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "oper/path_preference.h"
#include "filter/acl.h"
#include "openstack/instance_service_server.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h>
#include <ksync/ksync_sock_user.h>
#include <boost/assign/list_of.hpp>

using namespace boost::assign;

#define NULL_VRF ""
#define ZERO_IP "0.0.0.0"

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
    {"intf1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
};

class TestAap : public ::testing::Test {
public:
    TestAap() {
        peer_ = CreateBgpPeer(Ip4Address(1), "BGP Peer 1");
    }

    ~TestAap() {
        DeleteBgpPeer(peer_);
    }
    void AddAap(std::string intf_name, int intf_id,
            std::vector<Ip4Address> aap_list) {
        std::ostringstream buf;
        buf << "<virtual-machine-interface-allowed-address-pairs>";
        std::vector<Ip4Address>::iterator it = aap_list.begin();
        while (it != aap_list.end()) {
            buf << "<allowed-address-pair>";
            buf << "<ip>";
            buf << "<ip-prefix>" << it->to_string()<<"</ip-prefix>";
            buf << "<ip-prefix-len>"<< 32 << "</ip-prefix-len>";
            buf << "</ip>";
            buf << "<mac><mac-address>" << "00:00:00:00:00:00"
                << "</mac-address></mac>";
            buf << "<flag>" << "act-stby" << "</flag>";
            buf << "</allowed-address-pair>";
            it++;
        }
        buf << "</virtual-machine-interface-allowed-address-pairs>";
        char cbuf[10000];
        strcpy(cbuf, buf.str().c_str());
        AddNode("virtual-machine-interface", intf_name.c_str(),
                intf_id, cbuf);
        client->WaitForIdle();
    }

    virtual void SetUp() {
        CreateVmportEnv(input, 1);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
    }

    virtual void TearDown() {
        DeleteVmportEnv(input, 1, true);
        client->WaitForIdle();
        EXPECT_FALSE(VmPortFindRetDel(1));
        client->WaitForIdle();
    }
protected:
    Peer *peer_;
};

//Add and delete allowed address pair route
TEST_F(TestAap, AddDel_1) {
    Ip4Address ip = Ip4Address::from_string("10.10.10.10");
    std::vector<Ip4Address> v;
    v.push_back(ip);

    AddAap("intf1", 1, v);
    EXPECT_TRUE(RouteFind("vrf1", ip, 32));
    v.clear();
    AddAap("intf1", 1, v);
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));
}

TEST_F(TestAap, AddDel_2) {
    Ip4Address ip = Ip4Address::from_string("10.10.10.10");
    std::vector<Ip4Address> v;
    v.push_back(ip);

    AddAap("intf1", 1, v);
    EXPECT_TRUE(RouteFind("vrf1", ip, 32));

    DelLink("virtual-machine-interface-routing-instance", "intf1",
            "routing-instance", "vrf1");
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));

    AddLink("virtual-machine-interface-routing-instance", "intf1",
            "routing-instance", "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("vrf1", ip, 32));
}

TEST_F(TestAap, Update) {
    Ip4Address ip1 = Ip4Address::from_string("10.10.10.10");
    Ip4Address ip2 = Ip4Address::from_string("11.10.10.10");
    std::vector<Ip4Address> v;
    v.push_back(ip1);

    AddAap("intf1", 1, v);
    EXPECT_TRUE(RouteFind("vrf1", ip1, 32));

    v.push_back(ip2);
    AddAap("intf1", 1, v);
    EXPECT_TRUE(RouteFind("vrf1", ip1, 32));
    EXPECT_TRUE(RouteFind("vrf1", ip2, 32));

    v.clear();
    AddAap("intf1", 1, v);
    EXPECT_FALSE(RouteFind("vrf1", ip1, 32));
    EXPECT_FALSE(RouteFind("vrf1", ip2, 32));
}

//Just add a local path, verify that sequence no gets initialized to 0
TEST_F(TestAap, StateMachine_1) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    EXPECT_TRUE(RouteFind("vrf1", ip, 32));

    VmInterface *vm_intf = VmInterfaceGet(1);
    Inet4UnicastRouteEntry *rt =
        RouteGet("vrf1", ip, 32);
    const AgentPath *path = rt->GetActivePath();
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);
}

//Add a remote path woth same preference and verify that local path
//moves to wait for traffic state
TEST_F(TestAap, StateMachine_2) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address server_ip = Ip4Address::from_string("10.1.1.3");

    PathPreference path_preference(1, PathPreference::LOW, false, false);
    TunnelType::TypeBmap bmap = (1 << TunnelType::MPLS_GRE);
    Inet4TunnelRouteAdd(peer_, "vrf1", ip, 32, server_ip, bmap,
                        16, "vn1", SecurityGroupList(), path_preference);
    client->WaitForIdle();

    VmInterface *vm_intf = VmInterfaceGet(1);
    Inet4UnicastRouteEntry *rt =
        RouteGet("vrf1", ip, 32);
    const AgentPath *path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);
}

//Add a remote path with same preference and verify that local path
//moves to wait for traffic state
TEST_F(TestAap, StateMachine_3) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address server_ip = Ip4Address::from_string("10.1.1.3");

    PathPreference path_preference(1, PathPreference::LOW, false, false);
    TunnelType::TypeBmap bmap = (1 << TunnelType::MPLS_GRE);
    Inet4TunnelRouteAdd(peer_, "vrf1", ip, 32, server_ip, bmap,
                        16, "vn1", SecurityGroupList(), path_preference);
    client->WaitForIdle();

    VmInterface *vm_intf = VmInterfaceGet(1);
    Inet4UnicastRouteEntry *rt =
        RouteGet("vrf1", ip, 32);
    const AgentPath *path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 32, vm_intf->id(), vm_intf->vrf()->vrf_id());
    client->WaitForIdle();
    EXPECT_TRUE(path->path_preference().sequence() == 2);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);
}

//Verify that dependent static route gets high preference,
//when interface native IP sees traffic
TEST_F(TestAap, StateMachine_4) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    //Add a static route
   struct TestIp4Prefix static_route[] = {
       { Ip4Address::from_string("24.1.1.0"), 24},
       { Ip4Address::from_string("16.1.1.0"), 16},
   };

   AddInterfaceRouteTable("static_route", 1, static_route, 2);
   AddLink("virtual-machine-interface", "intf1",
           "interface-route-table", "static_route");
   Ip4Address sip = Ip4Address::from_string("24.1.1.0");
   client->WaitForIdle();

   VmInterface *vm_intf = VmInterfaceGet(1);
   Inet4UnicastRouteEntry *rt =
       RouteGet("vrf1", sip, 24);
   const AgentPath *path = rt->FindPath(vm_intf->peer());
   EXPECT_TRUE(path->path_preference().sequence() == 0);
   EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
   EXPECT_TRUE(path->path_preference().ecmp() == false);
   EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

   Agent::GetInstance()->oper_db()->route_preference_module()->
       EnqueueTrafficSeen(ip, 32, vm_intf->id(), vm_intf->vrf()->vrf_id());
   client->WaitForIdle();
   EXPECT_TRUE(path->path_preference().sequence() == 1);
   EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
   EXPECT_TRUE(path->path_preference().ecmp() == false);
   EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);
}

//Upon transition of instance IP address from active-backup to active-active
//Verify that path preference becomes high when interface is in active-active
//mode
TEST_F(TestAap, StateMachine_5) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    AddActiveActiveInstanceIp("instance1", 1, "1.1.1.1");
    client->WaitForIdle();

    VmInterface *vm_intf = VmInterfaceGet(1);
    Inet4UnicastRouteEntry *rt =
        RouteGet("vrf1", ip, 32);
    const AgentPath *path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == true);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);

    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 32, vm_intf->id(), vm_intf->vrf()->vrf_id());
    client->WaitForIdle();
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == true);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);
}

//Upon transition of instance IP address from active-backup to active-active
//Verify that path preference of dependent static router, becomes high
//when interface is in active-active mode
TEST_F(TestAap, StateMachine_6) {
    //Add a static route
    struct TestIp4Prefix static_route[] = {
        { Ip4Address::from_string("24.1.1.0"), 24},
        { Ip4Address::from_string("16.1.1.0"), 16},
    };

    AddInterfaceRouteTable("static_route", 1, static_route, 2);
    AddLink("virtual-machine-interface", "intf1",
            "interface-route-table", "static_route");
    Ip4Address sip = Ip4Address::from_string("24.1.1.0");
    client->WaitForIdle();
    VmInterface *vm_intf = VmInterfaceGet(1);
    Inet4UnicastRouteEntry *rt =
        RouteGet("vrf1", sip, 24);
    const AgentPath *path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    AddActiveActiveInstanceIp("instance1", 1, "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == true);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);

    AddInstanceIp("instance1", 1, "1.1.1.1");
    client->WaitForIdle();
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);
}

//Verify that a static route subnet route gets activated, when one of the
//ip in the subnet gets traffic
TEST_F(TestAap, StateMachine_7) {
    Ip4Address ip = Ip4Address::from_string("24.1.1.1");
    //Add a static route
    struct TestIp4Prefix static_route[] = {
        { Ip4Address::from_string("24.1.1.0"), 24},
        { Ip4Address::from_string("16.1.1.0"), 16},
    };

    AddInterfaceRouteTable("static_route", 1, static_route, 2);
    AddLink("virtual-machine-interface", "intf1",
            "interface-route-table", "static_route");
    Ip4Address sip = Ip4Address::from_string("24.1.1.0");
    client->WaitForIdle();
    VmInterface *vm_intf = VmInterfaceGet(1);
    Inet4UnicastRouteEntry *rt =
        RouteGet("vrf1", sip, 24);
    const AgentPath *path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 32, vm_intf->id(), vm_intf->vrf()->vrf_id());
    client->WaitForIdle();
    EXPECT_TRUE(path->path_preference().sequence() == 1);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);

    //Send a dummy update, and verify nothing changes
    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 32, vm_intf->id(), vm_intf->vrf()->vrf_id());
    client->WaitForIdle();
    EXPECT_TRUE(path->path_preference().sequence() == 1);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);

    //Verify that interface native IP is still inactive
    Ip4Address native_ip = Ip4Address::from_string("1.1.1.1");
    rt = RouteGet("vrf1", native_ip, 32);
    path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);
}

//Create a interface with IP address 24.1.1.1(intf2)
//Fake traffic for that ip from intf1(static route 24.1.1.0/24)
//and verify that 24.1.1. doesnt get activated
TEST_F(TestAap, StateMachine_8) {
    struct PortInfo input1[] = {
        {"intf2", 2, "24.1.1.1", "00:00:00:01:01:01", 1, 2},
    };
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("24.1.1.1");
    //Add a static route
    struct TestIp4Prefix static_route[] = {
        { Ip4Address::from_string("24.1.1.0"), 24},
        { Ip4Address::from_string("16.1.1.0"), 16},
    };

    AddInterfaceRouteTable("static_route", 1, static_route, 2);
    AddLink("virtual-machine-interface", "intf1",
            "interface-route-table", "static_route");
    Ip4Address sip = Ip4Address::from_string("24.1.1.0");
    client->WaitForIdle();
    VmInterface *vm_intf = VmInterfaceGet(1);
    Inet4UnicastRouteEntry *rt =
        RouteGet("vrf1", sip, 24);
    const AgentPath *path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    //Fake traffic on intf1 for 24.1.1.1
    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 32, vm_intf->id(), vm_intf->vrf()->vrf_id());
    client->WaitForIdle();
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    return ret;
}
