/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
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

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
    {"2.2.2.0", 24, "2.2.2.10"}
};

class FabricVmiTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent = Agent::GetInstance();
        client->WaitForIdle();
        peer_ = CreateBgpPeer(Ip4Address(1), "BGP Peer 1");
        client->WaitForIdle();
        AddIPAM("vn1", ipam_info, 2);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DelIPAM("vn1");
        client->WaitForIdle();
        DeleteBgpPeer(peer_);
        client->WaitForIdle();
        WAIT_FOR(100, 1000, (agent->vrf_table()->Size() == 1U));
        WAIT_FOR(100, 1000, (agent->vm_table()->Size() == 0U));
        WAIT_FOR(100, 1000, (agent->vn_table()->Size() == 0U));
    }

    Agent *agent;
    BgpPeer *peer_;
};

TEST_F(FabricVmiTest, basic_1) {
    AddVrf(agent->fabric_policy_vrf_name().c_str(), 2, false);
    client->WaitForIdle();

    VrfEntry *vrf = VrfGet(agent->fabric_policy_vrf_name().c_str());
    EXPECT_TRUE(vrf->forwarding_vrf() == agent->fabric_vrf());

    DelVrf(agent->fabric_policy_vrf_name().c_str());
    client->WaitForIdle();
}

TEST_F(FabricVmiTest, basic_2) {
    AddVrf(agent->fabric_policy_vrf_name().c_str(), 2, false);
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address server_ip = Ip4Address::from_string("10.1.1.3");

    PathPreference path_preference(1, PathPreference::LOW, false, false);
    TunnelType::TypeBmap bmap = (1 << TunnelType::MPLS_GRE);
    Inet4TunnelRouteAdd(peer_, 
                        agent->fabric_policy_vrf_name().c_str(),
                        ip, 32, server_ip, bmap, 16, "vn1",
                        SecurityGroupList(), TagList(), path_preference);
    client->WaitForIdle();

    //Route should be leaked to fabric VRF
    EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(), ip, 32));

    //Verify that nexthop is ARP nexthop fpr server_ip
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    EXPECT_TRUE(rt->GetActivePath()->gw_ip() == server_ip);

    DeleteRoute(agent->fabric_policy_vrf_name().c_str(), "1.1.1.1", 32,
                peer_);
    client->WaitForIdle();

    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    DelVrf(agent->fabric_policy_vrf_name().c_str());
    client->WaitForIdle();
}

TEST_F(FabricVmiTest, basic_3) {
    struct PortInfo input1[] = {
        {"intf2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
    };
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VrfKey("vrf1"));
    VrfData *data = new VrfData(agent, NULL, VrfData::ConfigVrf,
                                MakeUuid(1), 0, "", 0, false);
    data->forwarding_vrf_name_ = agent->fabric_vrf_name();
    req.data.reset(data);
    agent->vrf_table()->Enqueue(&req);
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    //Route should be leaked to fabric VRF
    EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(2));
    EXPECT_TRUE(vm_intf->forwarding_vrf()->GetName() == 
                agent->fabric_vrf_name());
    const InterfaceNH *intf_nh = 
        dynamic_cast<const InterfaceNH *>(vm_intf->flow_key_nh());
    EXPECT_TRUE(intf_nh->GetVrf()->GetName() == agent->fabric_vrf_name());
#if 0
    //Verify that nexthop is ARP nexthop fpr server_ip
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
#endif
    AddVrf("vrf1");
    client->WaitForIdle();

    EXPECT_TRUE(vm_intf->forwarding_vrf()->GetName() == "vrf1");
    EXPECT_TRUE(intf_nh->GetVrf()->GetName() == "vrf1");

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();

    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    DelVrf(agent->fabric_policy_vrf_name().c_str());
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, false, false, false);
    int ret = RUN_ALL_TESTS();
    usleep(10000);
    client->WaitForIdle();
    usleep(10000);
    TestShutdown();
    delete client;
    return ret;
}
