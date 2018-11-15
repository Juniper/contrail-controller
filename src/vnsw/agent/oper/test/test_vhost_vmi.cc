/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <sys/socket.h>

#include <net/if.h>

#ifdef __linux__
#include <linux/netlink.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>
#endif

#ifdef __FreeBSD__
#include <sys/sockio.h>
#include <ifaddrs.h>
#endif

#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
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
#include "filter/acl.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h>
#include <ksync/ksync_sock_user.h>
#include <boost/assign/list_of.hpp>

using namespace boost::assign;

#define NULL_VRF ""
#define ZERO_IP "0.0.0.0"

#define DEFAULT_VN "default-domain:default-project:ip-fabric"

IpamInfo ipam_info[] = {
    {"10.1.1.0", 24, "10.1.1.10"},
};

class FabricVmiTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent = Agent::GetInstance();
        client->WaitForIdle();

        AddVn(DEFAULT_VN, 1);
        AddIPAM(DEFAULT_VN, ipam_info, 1);

        std::stringstream str;
        str << "<display-name>" << "vhost0" << "</display-name>";
        AddNode("virtual-machine-interface", "vhost0", 10, str.str().c_str());
        AddLink("virtual-machine-interface", "vhost0",
                "virtual-network", DEFAULT_VN);

        peer_ = CreateBgpPeer(Ip4Address(1), "BGP Peer 1");
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DelIPAM(DEFAULT_VN);
        DelVn(DEFAULT_VN);
        //DelNode("virtual-machine-interface", "vhost0");
        DelLink("virtual-machine-interface", "vhost0",
                "virtual-network", DEFAULT_VN);
        client->WaitForIdle();

        DeleteBgpPeer(peer_);
        client->WaitForIdle();
        WAIT_FOR(100, 1000, (agent->vrf_table()->Size() == 2U));
        WAIT_FOR(100, 1000, (agent->vm_table()->Size() == 0U));
        WAIT_FOR(100, 1000, (agent->vn_table()->Size() == 0U));
    }

    Agent *agent;
    BgpPeer *peer_;
};

TEST_F(FabricVmiTest, CrossConnect) {
    const VmInterface *vm_intf =
        static_cast<const VmInterface *>(VhostGet("vhost0"));
    EXPECT_TRUE(vm_intf->parent()->name() == "vnet0");
    EXPECT_TRUE(vm_intf->vmi_type() == VmInterface::VHOST);
    EXPECT_TRUE(vm_intf->bridging() == false);
    EXPECT_TRUE(vm_intf->proxy_arp_mode() == VmInterface::PROXY_ARP_NONE);
}

TEST_F(FabricVmiTest, VerifyReceiveRoute) {
    Ip4Address ip = Ip4Address::from_string("10.1.1.1");

    EXPECT_TRUE(RouteFind(agent->fabric_policy_vrf_name(), ip, 32));
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_policy_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);

    EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    rt = RouteGet(agent->fabric_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RECEIVE);
}

TEST_F(FabricVmiTest, DefaultRoute) {
    Ip4Address ip = Ip4Address::from_string("0.0.0.0");

    EXPECT_TRUE(RouteFind(agent->fabric_policy_vrf_name(), ip, 0));

    EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(), ip, 0));
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_vrf_name(), ip, 0);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
}

TEST_F(FabricVmiTest, ResolveRoute) {
    Ip4Address ip = Ip4Address::from_string("10.1.1.0");

    EXPECT_FALSE(RouteFind(agent->fabric_policy_vrf_name(), ip, 24));

    EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(), ip, 24));
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_vrf_name(), ip, 24);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE);
}

TEST_F(FabricVmiTest, SG) {
    AddSg("sg1", 1);
    AddLink("virtual-machine-interface", "vhost0", "security-group", "sg1");
    client->WaitForIdle();

    SecurityGroupList sg1;
    sg1.push_back(1);

    Ip4Address ip = Ip4Address::from_string("10.1.1.1");

    EXPECT_TRUE(RouteFind(agent->fabric_policy_vrf_name(), ip, 32));
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_policy_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->GetActivePath()->sg_list() == sg1);

    DelLink("virtual-machine-interface", "vhost0", "security-group", "sg1");
    DelNode("security-group", "sg1");
    client->WaitForIdle();

    EXPECT_TRUE(rt->GetActivePath()->sg_list() != sg1);
}

TEST_F(FabricVmiTest, Tag) {
    AddTag("Tag1", 1, 1);
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vhost0", "tag", "Tag1");
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("10.1.1.1");
    TagList tag;
    tag.push_back(1);

    EXPECT_TRUE(RouteFind(agent->fabric_policy_vrf_name(), ip, 32));
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_policy_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->GetActivePath()->tag_list() == tag);

    AddLink("virtual-machine-interface", "vhost0", "tag", "Tag1");
    DelNode("tag", "Tag1");
    client->WaitForIdle();
}

//Delete the IPAM
TEST_F(FabricVmiTest, NoIpam) {
    Ip4Address ip = Ip4Address::from_string("10.1.1.1");
    DelIPAM(DEFAULT_VN);
    client->WaitForIdle();

    EXPECT_TRUE(RouteFind(agent->fabric_policy_vrf_name(), ip, 32));
    client->WaitForIdle();
}

TEST_F(FabricVmiTest, PolicyChange) {
    VnListType vn_list;
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE,
                            boost::uuids::nil_uuid(), "vhost0");
    LocalVmRoute *local_vm_route =
        new LocalVmRoute(intf_key, 17, VxLanTable::kInvalidvxlan_id,
                         false, vn_list, InterfaceNHFlags::INET4,
                         SecurityGroupList(), TagList(), CommunityList(),
                         PathPreference(), Ip4Address(0), EcmpLoadBalance(),
                         false, false, 0, false, false);

    InetUnicastAgentRouteTable *table = agent->fabric_inet4_unicast_table();
    table->AddLocalVmRouteReq(peer_, agent->fabric_policy_vrf_name(),
                              agent->router_id(), 32,
                              static_cast<LocalVmRoute *>(local_vm_route));
    client->WaitForIdle();

    std::stringstream str;
    str << "<display-name>" << "vhost0" << "</display-name>";
    str << "<virtual-machine-interface-disable-policy>";
    str << "true";
    str << "</virtual-machine-interface-disable-policy>";
    AddNode("virtual-machine-interface", "vhost0", 10, str.str().c_str());

    const AgentRoute *rt = RouteGet(agent->fabric_policy_vrf_name(),
            agent->router_id(), 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);

    table->ResyncRoute(peer_,agent->fabric_policy_vrf_name(),
                       agent->router_id(), 32);
    client->WaitForIdle();

    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    InetUnicastAgentRouteTable::DeleteReq(peer_,
                                          agent->fabric_policy_vrf_name(),
                                          agent->router_id(), 32,
                                          NULL);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    int ret = 0;
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    ret = RUN_ALL_TESTS();
    usleep(100000);
    TestShutdown();
    delete client;
    return ret;
}
