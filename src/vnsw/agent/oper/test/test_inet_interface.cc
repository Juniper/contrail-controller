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
#include "filter/acl.h"
#include "openstack/instance_service_server.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h> 
#include <ksync/ksync_sock_user.h> 

#define VRF_VHOST "vrf-vhost"
#define VRF_LL "vrf-ll"
#define VRF_GW "vrf-gw"

void RouterIdDepInit(Agent *agent) {
}

class InetInterfaceTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        interface_table_ = agent_->interface_table();
        nh_table_ = agent_->nexthop_table();
        vrf_table_ = agent_->vrf_table();
        intf_count_ = agent_->interface_table()->Size();
        nh_count_ = agent_->nexthop_table()->Size();
        vrf_count_ = agent_->vrf_table()->Size();
        peer_ = agent_->local_peer();

        vrf_table_->CreateVrfReq(VRF_VHOST);
        vrf_table_->CreateVrfReq(VRF_LL);
        vrf_table_->CreateVrfReq(VRF_GW);
        client->WaitForIdle();

        vhost_rt_table_ = static_cast<Inet4UnicastAgentRouteTable *> 
            (vrf_table_->GetInet4UnicastRouteTable(VRF_VHOST));
        ll_rt_table_ = static_cast<Inet4UnicastAgentRouteTable *> 
            (vrf_table_->GetInet4UnicastRouteTable(VRF_LL));
        gw_rt_table_ = static_cast<Inet4UnicastAgentRouteTable *> 
            (vrf_table_->GetInet4UnicastRouteTable(VRF_GW));
    }

    virtual void TearDown() {
        vrf_table_->DeleteVrfReq(VRF_VHOST);
        vrf_table_->DeleteVrfReq(VRF_LL);
        vrf_table_->DeleteVrfReq(VRF_GW);
        client->WaitForIdle();

        WAIT_FOR(100, 1000, (interface_table_->Size() == intf_count_));
        WAIT_FOR(100, 1000, (agent_->vrf_table()->Size() == vrf_count_));
        WAIT_FOR(100, 1000, (agent_->nexthop_table()->Size() == nh_count_));
        WAIT_FOR(100, 1000, (agent_->vm_table()->Size() == 0U));
        WAIT_FOR(100, 1000, (agent_->vn_table()->Size() == 0U));
    }

    int intf_count_;
    int nh_count_;
    int vrf_count_;
    Agent *agent_;
    InterfaceTable *interface_table_;
    NextHopTable *nh_table_;
    Inet4UnicastAgentRouteTable *vhost_rt_table_;
    Inet4UnicastAgentRouteTable *ll_rt_table_;
    Inet4UnicastAgentRouteTable *gw_rt_table_;
    VrfTable *vrf_table_;
    const Peer *peer_;
};

static void DelInterface(InetInterfaceTest *t, const char *ifname) {
    InetInterface::DeleteReq(t->interface_table_, ifname);
}

static void AddInterface(InetInterfaceTest *t, const char *ifname,
                         InetInterface::SubType sub_type, const char *vrf,
                         const char *ip, int plen, const char *gw) {
    InetInterface::CreateReq(t->interface_table_, ifname, sub_type, vrf,
                             Ip4Address::from_string(ip), plen,
                             Ip4Address::from_string(gw),
                             client->param()->eth_port(), "TEST");
}

static void DelInterface(InetInterfaceTest *t, const char *ifname,
                         const char *vrf, const char *gw) {
    InetInterface::DeleteReq(t->interface_table_, ifname);

    if (strcmp(vrf, VRF_VHOST) == 0){
        t->vhost_rt_table_->DeleteReq(t->peer_, vrf,
                                      Ip4Address::from_string(gw), 32, NULL);
    }

    if (strcmp(vrf, VRF_LL) == 0){
        t->ll_rt_table_->DeleteReq(t->peer_, vrf, Ip4Address::from_string(gw),
                                   32, NULL);
    }

    if (strcmp(vrf, VRF_GW) == 0){
        t->gw_rt_table_->DeleteReq(t->peer_, vrf, Ip4Address::from_string(gw),
                                   32, NULL);
    }

}

// Create and delete VHOST port
TEST_F(InetInterfaceTest, vhost_basic_1) {
    AddInterface(this, "vhost1", InetInterface::VHOST, VRF_VHOST,
                 "1.1.1.1", 24, "1.1.1.254");
    client->WaitForIdle();

    NextHop *nh;
    nh = ReceiveNHGet(nh_table_, "vhost1", false);
    EXPECT_TRUE(nh != NULL);

    nh = ReceiveNHGet(nh_table_, "vhost1", true);
    EXPECT_TRUE(nh != NULL);

    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.1"), 32));
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.0"), 24));
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.255"), 32));
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("0.0.0.0"), 0));
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.254"), 32));

    DelInterface(this, "vhost1", VRF_VHOST, "1.1.1.254");
    client->WaitForIdle();
}

TEST_F(InetInterfaceTest, vhost_key_manipulations) {
    AddInterface(this, "vhost1", InetInterface::VHOST, VRF_VHOST,
                 "1.1.1.1", 24, "1.1.1.254");
    client->WaitForIdle();

    ReceiveNH *nh = static_cast<ReceiveNH *>(ReceiveNHGet(nh_table_, "vhost1", 
                                                          false));
    EXPECT_TRUE(nh != NULL);
    DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
    nh->SetKey(key.get());

    DelInterface(this, "vhost1", VRF_VHOST, "1.1.1.254");
    client->WaitForIdle();
}

TEST_F(InetInterfaceTest, vhost_no_ip_1) {
    AddInterface(this, "vhost1", InetInterface::VHOST, VRF_VHOST,
                 "0.0.0.0", 0, "1.1.1.254");
    client->WaitForIdle();

    NextHop *nh;
    nh = ReceiveNHGet(nh_table_, "vhost1", false);
    EXPECT_TRUE(nh != NULL);

    nh = ReceiveNHGet(nh_table_, "vhost1", true);
    EXPECT_TRUE(nh != NULL);

    // Gateway route is added added
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("0.0.0.0"), 0));
    // Resolve route is not added for route since 1.1.1.0/24 route is missing
    EXPECT_FALSE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.254"),
                           32));

    DelInterface(this, "vhost1", VRF_VHOST, "1.1.1.254");
    client->WaitForIdle();
}

// Test interface add without-ip and set ip later
TEST_F(InetInterfaceTest, vhost_change_1) {
    AddInterface(this, "vhost1", InetInterface::VHOST, VRF_VHOST,
                 "0.0.0.0", 0, "1.1.1.254");
    client->WaitForIdle();

    NextHop *nh;
    nh = ReceiveNHGet(nh_table_, "vhost1", false);
    EXPECT_TRUE(nh != NULL);

    nh = ReceiveNHGet(nh_table_, "vhost1", true);
    EXPECT_TRUE(nh != NULL);

    // Gateway route is added added
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("0.0.0.0"), 0));
    // Resolve route is not added for route since 1.1.1.0/24 route is missing
    EXPECT_FALSE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.254"),
                           32));

    AddInterface(this, "vhost1", InetInterface::VHOST, VRF_VHOST,
                 "1.1.1.1", 24, "1.1.1.254");
    client->WaitForIdle();

    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.1"), 32));
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.0"), 24));
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.255"), 32));
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("0.0.0.0"), 0));
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.254"), 32));

    DelInterface(this, "vhost1", VRF_VHOST, "1.1.1.254");
    client->WaitForIdle();
}

TEST_F(InetInterfaceTest, del_add_1) {
    AddInterface(this, "vhost1", InetInterface::VHOST, VRF_VHOST,
                 "0.0.0.0", 0, "1.1.1.254");
    client->WaitForIdle();

    // Add reference to interface so that its not freed on delete
    InterfaceRef ref(InetInterfaceGet("vhost1"));

    DelInterface(this, "vhost1");
    client->WaitForIdle();

    NextHop *nh;
    nh = ReceiveNHGet(nh_table_, "vhost1", false);
    EXPECT_TRUE(nh == NULL);

    nh = ReceiveNHGet(nh_table_, "vhost1", true);
    EXPECT_TRUE(nh == NULL);

    EXPECT_TRUE(InetInterfaceGet("vhost1") == NULL);

    AddInterface(this, "vhost1", InetInterface::VHOST, VRF_VHOST,
                 "1.1.1.1", 24, "1.1.1.254");
    client->WaitForIdle();

    nh = ReceiveNHGet(nh_table_, "vhost1", false);
    EXPECT_TRUE(nh != NULL);

    nh = ReceiveNHGet(nh_table_, "vhost1", true);
    EXPECT_TRUE(nh != NULL);

    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.1"), 32));
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.0"), 24));
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.255"), 32));
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("0.0.0.0"), 0));
    EXPECT_TRUE(RouteFind(VRF_VHOST, Ip4Address::from_string("1.1.1.254"), 32));

    DelInterface(this, "vhost1", VRF_VHOST, "1.1.1.254");
    client->WaitForIdle();
}

// Create and delete LL port
TEST_F(InetInterfaceTest, ll_basic_1) {
    AddInterface(this, "ll1", InetInterface::LINK_LOCAL, VRF_LL,
                 "1.1.1.1", 24, "1.1.1.254");
    client->WaitForIdle();

    NextHop *nh;
    nh = ReceiveNHGet(nh_table_, "ll1", false);
    EXPECT_TRUE(nh != NULL);

    nh = ReceiveNHGet(nh_table_, "ll1", true);
    EXPECT_TRUE(nh != NULL);

    EXPECT_TRUE(RouteFind(VRF_LL, Ip4Address::from_string("1.1.1.1"), 32));
    EXPECT_TRUE(RouteFind(VRF_LL, Ip4Address::from_string("1.1.1.0"), 24));
    EXPECT_TRUE(RouteFind(VRF_LL, Ip4Address::from_string("1.1.1.255"), 32));
    EXPECT_TRUE(RouteFind(VRF_LL, Ip4Address::from_string("0.0.0.0"), 0));
    EXPECT_TRUE(RouteFind(VRF_LL, Ip4Address::from_string("1.1.1.254"), 32));

    DelInterface(this, "ll1", VRF_LL, "1.1.1.254");
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, false, false, false);

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
