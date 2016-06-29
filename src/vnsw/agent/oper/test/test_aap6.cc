/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"

#include "testing/gunit.h"

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
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "oper/path_preference.h"
#include "filter/acl.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h>
#include <ksync/ksync_sock_user.h>
#include <boost/assign/list_of.hpp>

using namespace boost::assign;

MacAddress zero_mac;

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
    {"intf1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1, "fd10::2"},
};
IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
    {"fd10::", 96, "fd10::1"},
};

class TestAap6 : public ::testing::Test {
public:
    TestAap6() {
        peer_ = CreateBgpPeer(Ip4Address(1), "BGP Peer 1");
    }

    ~TestAap6() {
        DeleteBgpPeer(peer_);
    }
    uint32_t Ip2PrefixLen(IpAddress addr) {
        uint32_t plen = 0;
        if (addr.is_v4()) {
            plen = 32;
        } else if (addr.is_v6()) {
            plen = 128;
        }
        return plen;

    }
    void AddAap(std::string intf_name, int intf_id,
            std::vector<IpAddress> aap_list) {
        std::ostringstream buf;
        buf << "<virtual-machine-interface-allowed-address-pairs>";
        std::vector<IpAddress>::iterator it = aap_list.begin();
        while (it != aap_list.end()) {
            uint32_t plen = Ip2PrefixLen(*it);
            buf << "<allowed-address-pair>";
            buf << "<ip>";
            buf << "<ip-prefix>" << it->to_string()<<"</ip-prefix>";
            buf << "<ip-prefix-len>"<< plen << "</ip-prefix-len>";
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

    void AddAap(std::string intf_name, int intf_id, IpAddress ip,
                const std::string &mac) {
        std::ostringstream buf;
        uint32_t plen = Ip2PrefixLen(ip);
        buf << "<virtual-machine-interface-allowed-address-pairs>";
        buf << "<allowed-address-pair>";
        buf << "<ip>";
        buf << "<ip-prefix>" << ip.to_string() <<"</ip-prefix>";
        buf << "<ip-prefix-len>"<< plen << "</ip-prefix-len>";
        buf << "</ip>";
        buf << "<mac>" << mac << "</mac>";
        buf << "<flag>" << "act-stby" << "</flag>";
        buf << "</allowed-address-pair>";
        buf << "</virtual-machine-interface-allowed-address-pairs>";
        char cbuf[10000];
        strcpy(cbuf, buf.str().c_str());
        AddNode("virtual-machine-interface", intf_name.c_str(),
                intf_id, cbuf);
        client->WaitForIdle();
    }

    void AddEcmpAap(std::string intf_name, int intf_id, IpAddress ip) {
        std::ostringstream buf;
        uint32_t plen = Ip2PrefixLen(ip);
        buf << "<virtual-machine-interface-allowed-address-pairs>";
        buf << "<allowed-address-pair>";
        buf << "<ip>";
        buf << "<ip-prefix>" << ip.to_string() <<"</ip-prefix>";
        buf << "<ip-prefix-len>"<< plen << "</ip-prefix-len>";
        buf << "</ip>";
        buf << "<mac><mac-address>" << "00:00:00:00:00:00"
            << "</mac-address></mac>";
        buf << "<address-mode>" << "active-active" << "</address-mode>";
        buf << "</allowed-address-pair>";
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
        AddIPAM("vn1", ipam_info, 2);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
    }

    virtual void TearDown() {
        DeleteVmportEnv(input, 1, 1, 0, NULL, NULL, true, true);
        client->WaitForIdle();
        DelIPAM("vn1");
        client->WaitForIdle();
        EXPECT_FALSE(VmPortFindRetDel(1));
        EXPECT_FALSE(VrfFind("vrf1", true));
        client->WaitForIdle();
    }
protected:
    Peer *peer_;
};

//Add and delete allowed address pair route
TEST_F(TestAap6, AddDel_1) {
    Ip6Address ip = Ip6Address::from_string("fd10::10");
    std::vector<IpAddress> v;
    v.push_back(ip);

    AddAap("intf1", 1, v);
    EXPECT_TRUE(RouteFindV6("vrf1", ip, 128));
    v.clear();
    AddAap("intf1", 1, v);
    EXPECT_FALSE(RouteFindV6("vrf1", ip, 128));
}

TEST_F(TestAap6, AddDel_2) {
    Ip6Address ip = Ip6Address::from_string("fd10::10");
    std::vector<IpAddress> v;
    v.push_back(ip);

    AddAap("intf1", 1, v);
    EXPECT_TRUE(RouteFindV6("vrf1", ip, 128));

    DelLink("virtual-machine-interface-routing-instance", "intf1",
            "routing-instance", "vrf1");
    client->WaitForIdle();
    EXPECT_FALSE(RouteFindV6("vrf1", ip, 128));

    AddLink("virtual-machine-interface-routing-instance", "intf1",
            "routing-instance", "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(RouteFindV6("vrf1", ip, 128));
}

TEST_F(TestAap6, Update) {
    Ip6Address ip1 = Ip6Address::from_string("fd10::10");
    Ip6Address ip2 = Ip6Address::from_string("fd11::10");
    std::vector<IpAddress> v;
    v.push_back(ip1);

    AddAap("intf1", 1, v);
    EXPECT_TRUE(RouteFindV6("vrf1", ip1, 128));

    v.push_back(ip2);
    AddAap("intf1", 1, v);
    EXPECT_TRUE(RouteFindV6("vrf1", ip1, 128));
    EXPECT_TRUE(RouteFindV6("vrf1", ip2, 128));

    v.clear();
    AddAap("intf1", 1, v);
    EXPECT_FALSE(RouteFindV6("vrf1", ip1, 128));
    EXPECT_FALSE(RouteFindV6("vrf1", ip2, 128));
}

//Check if subnet gateway for allowed address pair route gets set properly
TEST_F(TestAap6, SubnetGw) {
    Ip6Address ip1 = Ip6Address::from_string("fd10::10");
    std::vector<IpAddress> v;
    v.push_back(ip1);

    AddAap("intf1", 1, v);
    EXPECT_TRUE(RouteFindV6("vrf1", ip1, 128));

    IpamInfo ipam_info[] = {
        {"fd10::", 120, "fd10::200", true},
    };
    AddIPAM("vn1", ipam_info, 1, NULL, "vdns1");
    client->WaitForIdle();

    Ip6Address subnet_service_ip = Ip6Address::from_string("fd10::200");
    InetUnicastRouteEntry *rt = RouteGetV6("vrf1", ip1, 128);
    EXPECT_TRUE(rt->GetActivePath()->subnet_service_ip() == subnet_service_ip);

    DelIPAM("vn1", "vdns1");
    client->WaitForIdle();

    v.clear();
    AddAap("intf1", 1, v);
    EXPECT_FALSE(RouteFindV6("vrf1", ip1, 128));
}

//When both IP and mac are given in config, verify that routes get added to
//both V6 table and EVPN table.
TEST_F(TestAap6, EvpnRoute) {
    Ip6Address ip = Ip6Address::from_string("fd10::10");
    MacAddress mac("0a:0b:0c:0d:0e:0f");

    VmInterface *vm_intf = static_cast<VmInterface *>(VmPortGet(1));
    AddAap("intf1", 1, ip, mac.ToString());
    EXPECT_TRUE(RouteFindV6("vrf1", ip, 128));
    EXPECT_TRUE(EvpnRouteGet("vrf1", mac, ip, 0));
    EXPECT_TRUE(vm_intf->allowed_address_pair_list().list_.size() == 1);

    AddAap("intf1", 1, Ip6Address(), zero_mac.ToString());
    EXPECT_FALSE(RouteFindV6("vrf1", ip, 128));
    EXPECT_FALSE(EvpnRouteGet("vrf1", mac, ip, 0));
    EXPECT_TRUE(vm_intf->allowed_address_pair_list().list_.size() == 0);
}

//Just add a local path, verify that sequence no gets initialized to 0
TEST_F(TestAap6, StateMachine_1) {
    Ip6Address ip = Ip6Address::from_string("fd10::10");
    std::vector<IpAddress> v;
    v.push_back(ip);

    AddAap("intf1", 1, v);
    EXPECT_TRUE(RouteFindV6("vrf1", ip, 128));

    InetUnicastRouteEntry *rt = RouteGetV6("vrf1", ip, 128);
    const AgentPath *path = rt->GetActivePath();
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    //cleanup
    v.clear();
    AddAap("intf1", 1, v);
    EXPECT_FALSE(RouteFindV6("vrf1", ip, 128));
}

//Add a remote path with same preference and verify that local path
//moves to wait for traffic state
TEST_F(TestAap6, StateMachine_2) {
    Ip6Address ip = Ip6Address::from_string("fd10::10");
    std::vector<IpAddress> v;
    v.push_back(ip);

    AddAap("intf1", 1, v);
    EXPECT_TRUE(RouteFindV6("vrf1", ip, 128));

    //On addition of AAP config, verify that the initial state of the path for
    //VMI peer is set to the following
    //--Preference as LOW, sequence as 0 and wait_for_traffic as TRUE.
    VmInterface *vm_intf = VmInterfaceGet(1);
    InetUnicastRouteEntry *rt = RouteGetV6("vrf1", ip, 128);
    const AgentPath *path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    //Enqueue traffic seen
    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 128, vm_intf->id(), vm_intf->vrf()->vrf_id(),
                           vm_intf->vm_mac());
    client->WaitForIdle();

    //After seeing traffic, verify that path for VMI peer is updated as follows
    //--Preference as HIGH, sequence as 1 and wait_for_traffic as FALSE.
    EXPECT_TRUE(path->path_preference().sequence() == 1);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);

    //cleanup
    v.clear();
    AddAap("intf1", 1, v);
    EXPECT_FALSE(RouteFindV6("vrf1", ip, 128));
}

TEST_F(TestAap6, StateMachine_3) {
    Ip6Address ip = Ip6Address::from_string("fd10::10");
    MacAddress mac("0a:0b:0c:0d:0e:0f");

    AddAap("intf1", 1, ip, mac.ToString());
    EXPECT_TRUE(RouteFindV6("vrf1", ip, 128));
    EXPECT_TRUE(EvpnRouteGet("vrf1", mac, ip, 0));

    //Add a remote path with same preference and higher sequence number
    Ip4Address server_ip = Ip4Address::from_string("10.1.1.3");
    PathPreference path_preference(1, PathPreference::LOW, false, false);
    TunnelType::TypeBmap bmap = (1 << TunnelType::MPLS_GRE);
    Inet6TunnelRouteAdd(peer_, "vrf1", ip, 128, server_ip, bmap,
                        16, "vn1", SecurityGroupList(), path_preference);
    client->WaitForIdle();

    VmInterface *vm_intf = VmInterfaceGet(1);
    InetUnicastRouteEntry *rt = RouteGetV6("vrf1", ip, 128);
    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", mac, ip, 0);

    //Verify that paths for unicast and evpn routes for VMI peer are set to
    //default values
    const AgentPath *path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    path = evpn_rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    //Enqueue traffic
    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 128, vm_intf->id(), vm_intf->vrf()->vrf_id(),
                           mac);
    client->WaitForIdle();

    //Verify that paths for unicast and evpn routes for VMI peer are updated
    //after traffic is seen.
    path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 2);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);

    path = evpn_rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 1);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);

    //cleanup
    AddAap("intf1", 1, Ip6Address(), zero_mac.ToString());

    //Remove the remote route added.
    InetUnicastAgentRouteTable *rt_table =
        Agent::GetInstance()->vrf_table()->GetInet6UnicastRouteTable("vrf1");
    rt_table->DeleteReq(peer_, "vrf1", ip, 128,
                        new ControllerVmRoute(static_cast<BgpPeer *>(peer_)));
    client->WaitForIdle();
    WAIT_FOR(1000, 1, (RouteFindV6("vrf1", ip, 128) == false));
    EXPECT_FALSE(EvpnRouteGet("vrf1", mac, ip, 0));
}

//Verify that dependent static route gets high preference, when traffic is seen
//on interface native IP. (UT for path_preference module. Not an AAP UT)
TEST_F(TestAap6, StateMachine_4) {
    Ip6Address ip = Ip6Address::from_string("fd10::2");
    //Add a static route
    struct TestIp6Prefix static_route[] = {
        { Ip6Address::from_string("fd11::2"), 120}
    };

    AddInterfaceRouteTableV6("static_route", 1, static_route, 1);
    AddLink("virtual-machine-interface", "intf1",
            "interface-route-table", "static_route");
    client->WaitForIdle();

    //Verify static IP route and its path preference attributes
    VmInterface *vm_intf = VmInterfaceGet(1);
    InetUnicastRouteEntry *rt = RouteGetV6("vrf1", static_route[0].addr_,
                                           static_route[0].plen_);
    EXPECT_TRUE(rt != NULL);
    const AgentPath *path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    //Enqueue traffic on native IP
    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 128, vm_intf->id(), vm_intf->vrf()->vrf_id(),
                           vm_intf->vm_mac());
    client->WaitForIdle();

    //Verify that static IP route's path_preference attributes are updated on
    //seeing traffic on interface's native IP
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);

    //Cleanup
    DelLink("virtual-machine-interface", "intf1",
            "interface-route-table", "static_route");
    client->WaitForIdle();
    rt = RouteGetV6("vrf1", static_route[0].addr_, static_route[0].plen_);
    EXPECT_TRUE(rt == NULL);
}

//Upon transition of instance IP address from active-backup to active-active,
//Verify that path preference becomes high (This does not have any AAP config)
TEST_F(TestAap6, StateMachine_5) {
    Ip6Address ip = Ip6Address::from_string("fd10::2");

    VmInterface *vm_intf = VmInterfaceGet(1);
    InetUnicastRouteEntry *rt = RouteGetV6("vrf1", ip, 128);
    const AgentPath *path = rt->FindPath(vm_intf->peer());
    //Verify path attributes for native-IP
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    AddActiveActiveInstanceIp("instance1", 1, "fd10::2");
    client->WaitForIdle();
    //After instance-ip is configured for "active-active" mode, verify that
    //path preference attributes are updated (ecmp = true, pref = HIGH,
    //wait_for_traffic=false)
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == true);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);

    //Enqueue traffic seen
    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 128, vm_intf->id(), vm_intf->vrf()->vrf_id(),
                           vm_intf->vm_mac());
    client->WaitForIdle();

    //Verify that there no changes in path attributes, because wait_for_traffic
    //is false.
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == true);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);
}

//Upon transition of instance IP address from active-backup to active-active,
//verify that path preference of dependent static routes, becomes high
TEST_F(TestAap6, StateMachine_6) {
    //Add a static route
    struct TestIp6Prefix static_route[] = {
        { Ip6Address::from_string("fd11::2"), 120}
    };

    AddInterfaceRouteTableV6("static_route", 1, static_route, 1);
    AddLink("virtual-machine-interface", "intf1",
            "interface-route-table", "static_route");
    client->WaitForIdle();

    //Verify static IP route and its path preference attributes
    VmInterface *vm_intf = VmInterfaceGet(1);
    InetUnicastRouteEntry *rt = RouteGetV6("vrf1", static_route[0].addr_,
                                           static_route[0].plen_);
    EXPECT_TRUE(rt != NULL);
    const AgentPath *path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    AddActiveActiveInstanceIp("instance1", 1, "fd10::2");
    client->WaitForIdle();
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == true);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);

    AddInstanceIp("instance1", 1, "fd10::2");
    client->WaitForIdle();
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    //Cleanup
    DelLink("virtual-machine-interface", "intf1",
            "interface-route-table", "static_route");
    client->WaitForIdle();
    rt = RouteGetV6("vrf1", static_route[0].addr_, static_route[0].plen_);
    EXPECT_TRUE(rt == NULL);
}

//Verify that static preference is populated
TEST_F(TestAap6, StateMachine_10) {
    Ip6Address ip = Ip6Address::from_string("fd10::2");
    EXPECT_TRUE(RouteFindV6("vrf1", ip, 128));

    InetUnicastRouteEntry *rt = RouteGetV6("vrf1", ip, 128);
    const AgentPath *path = rt->GetActivePath();
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);
    EXPECT_TRUE(path->path_preference().static_preference() == false);

    AddStaticPreference("intf1", 1, 200);
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);
    EXPECT_TRUE(path->path_preference().static_preference() == true);

    //Delete static interface property
    AddNode("virtual-machine-interface", "intf1", 1, "");
    client->WaitForIdle();
    EXPECT_TRUE(path->path_preference().static_preference() == false);
}

//Verify that preference value change is reflected with
//static preference change
TEST_F(TestAap6, StaticMachine_11) {
    AddStaticPreference("intf1", 1, 100);
    Ip6Address ip = Ip6Address::from_string("fd10::2");
    EXPECT_TRUE(RouteFindV6("vrf1", ip, 128));

    InetUnicastRouteEntry *rt = RouteGetV6("vrf1", ip, 128);
    const AgentPath *path = rt->GetActivePath();
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);
    EXPECT_TRUE(path->path_preference().static_preference() == true);

    AddStaticPreference("intf1", 1, 200);
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);
    EXPECT_TRUE(path->path_preference().static_preference() == true);

    AddStaticPreference("intf1", 1, 100);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);

    //Delete static interface property
    AddNode("virtual-machine-interface", "intf1", 1, "");
    client->WaitForIdle();
    EXPECT_TRUE(path->path_preference().static_preference() == false);
}

//Verify that static preference is not populated
//when preference value is set to 0
TEST_F(TestAap6, StateMachine_12) {
    AddStaticPreference("intf1", 1, 0);
    Ip6Address ip = Ip6Address::from_string("fd10::2");
    EXPECT_TRUE(RouteFindV6("vrf1", ip, 128));

    InetUnicastRouteEntry *rt = RouteGetV6("vrf1", ip, 128);
    const AgentPath *path = rt->GetActivePath();
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);
    EXPECT_TRUE(path->path_preference().static_preference() == false);
}

//When traffic is seen on native IP ensure that preference is updated even for
//AAP IP (when address mode is active-active)
TEST_F(TestAap6, StateMachine_16) {
    //Add AAP-IP with active-active mode
    Ip6Address aap_ip = Ip6Address::from_string("fd10::10");
    AddEcmpAap("intf1", 1, aap_ip);
    EXPECT_TRUE(RouteFindV6("vrf1", aap_ip, 128));

    //Verify that ecmp is set to true on AAP IP route's path, when active-active
    //mode is configured.
    VmInterface *vm_intf = VmInterfaceGet(1);
    InetUnicastRouteEntry *rt = RouteGetV6("vrf1", aap_ip, 128);
    const AgentPath *path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::LOW);
    EXPECT_TRUE(path->path_preference().ecmp() == true);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == true);

    //Enqueue traffic seen for native IP
    Ip6Address ip = Ip6Address::from_string("fd10::2");
    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 128, vm_intf->id(), vm_intf->vrf()->vrf_id(),
                           vm_intf->vm_mac());
    client->WaitForIdle();

    //Verify preference for AAP IP path is increased and wait_for_traffic is
    //set to false
    EXPECT_TRUE(path->path_preference().sequence() == 0);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == true);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);

    //Verify that preference and wait_for_traffic fields for native IP are also
    //updated
    rt = RouteGetV6("vrf1", ip, 128);
    path = rt->FindPath(vm_intf->peer());
    EXPECT_TRUE(path->path_preference().sequence() == 1);
    EXPECT_TRUE(path->path_preference().preference() == PathPreference::HIGH);
    EXPECT_TRUE(path->path_preference().ecmp() == false);
    EXPECT_TRUE(path->path_preference().wait_for_traffic() == false);

    //cleanup
    AddAap("intf1", 1, Ip6Address(), zero_mac.ToString());
    WAIT_FOR(1000, 1, (RouteFindV6("vrf1", aap_ip, 128) == false));
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    return ret;
}
