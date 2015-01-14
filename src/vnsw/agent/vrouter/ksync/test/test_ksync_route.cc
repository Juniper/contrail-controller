/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "vrouter/ksync/route_ksync.h"

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
};

class TestKSyncRoute : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
        EXPECT_TRUE(VmPortActive(2));
        vnet1_ = static_cast<VmInterface *>(VmPortGet(1));
        vnet2_ = static_cast<VmInterface *>(VmPortGet(2));

        vrf1_obj_ = agent_->ksync()->vrf_ksync_obj();
        vrf_listener_id_ = vrf1_obj_->vrf_listener_id();

        VrfTable *table = static_cast<VrfTable *>(agent_->vrf_table());
        VrfKSyncObject::VrfState *state;

        vrf1_ = vnet1_->vrf();
        vrf1_uc_table_ = static_cast<InetUnicastAgentRouteTable *>
            (vrf1_->GetInet4UnicastRouteTable());
        state = static_cast<VrfKSyncObject::VrfState *>
            (vrf1_->GetState(table, vrf_listener_id_));
        vrf1_rt_obj_ = state->inet4_uc_route_table_;

        VrfEntry *fabric_vrf =
            table->FindVrfFromName(agent_->fabric_vrf_name());
        fabric_uc_table_ = static_cast<InetUnicastAgentRouteTable *>
            (fabric_vrf->GetInet4UnicastRouteTable());
        state = static_cast<VrfKSyncObject::VrfState *>
            (fabric_vrf->GetState(table, vrf_listener_id_));
        fabric_rt_obj_ = state->inet4_uc_route_table_;
    }

    virtual void TearDown() {
        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle();
        WAIT_FOR(1000, 100, (VmPortFindRetDel(1) == false));
        WAIT_FOR(1000, 100, (VmPortFindRetDel(2) == false));
    }

    void AddRemoteRoute(const IpAddress &addr, int plen) {
        SecurityGroupList sg_list;
        PathPreference path_pref;
        ControllerVmRoute *data = NULL;
        data = ControllerVmRoute::MakeControllerVmRoute
            (NULL, agent_->fabric_vrf_name(), agent_->router_id(),
             "vrf1", Ip4Address::from_string("10.10.10.2"), TunnelType::GREType(),
             100, "vn1", sg_list, path_pref);
        vrf1_uc_table_->AddRemoteVmRouteReq(NULL, "vrf1", addr, 32, data);
        client->WaitForIdle();
    }

    Agent *agent_;
    VnswInterfaceListener *vnswif_;
    VmInterface *vnet1_;
    VmInterface *vnet2_;
    DBTableBase::ListenerId vrf_listener_id_;
    VrfEntry *vrf1_;
    VrfKSyncObject *vrf1_obj_;
    InetUnicastAgentRouteTable *vrf1_uc_table_;
    InetUnicastAgentRouteTable *fabric_uc_table_;
    RouteKSyncObject *vrf1_rt_obj_;
    RouteKSyncObject *fabric_rt_obj_;
};

// proxy_arp_ and flood_ flags for interface-route
TEST_F(TestKSyncRoute, vm_interface_route_1) {
    InetUnicastRouteEntry *rt = vrf1_uc_table_->FindLPM(vnet1_->ip_addr());
    EXPECT_TRUE(rt != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf1_rt_obj_, rt));
    EXPECT_TRUE(vrf1_obj_->RouteNeedsMacBinding(rt));

    ksync->BuildRouteFlags(rt, vnet1_->mac());
    EXPECT_TRUE(ksync->proxy_arp());
    EXPECT_FALSE(ksync->flood());
}

// proxy_arp_ and flood_ flags for interface-route when MAC not stitched
TEST_F(TestKSyncRoute, vm_interface_route_2) {
    InetUnicastRouteEntry *rt = vrf1_uc_table_->FindLPM(vnet1_->ip_addr());
    EXPECT_TRUE(rt != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf1_rt_obj_, rt));
    EXPECT_TRUE(vrf1_obj_->RouteNeedsMacBinding(rt));

    ksync->BuildRouteFlags(rt, MacAddress());
    EXPECT_FALSE(ksync->proxy_arp());
    EXPECT_TRUE(ksync->flood());
}

// proxy_arp_ and flood_ flags for remote route
TEST_F(TestKSyncRoute, remote_route_1) {
    IpAddress addr = IpAddress(Ip4Address::from_string("1.1.1.100"));
    AddRemoteRoute(addr, 32);

    InetUnicastRouteEntry *rt = vrf1_uc_table_->FindLPM(addr);
    EXPECT_TRUE(rt != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf1_rt_obj_, rt));
    EXPECT_TRUE(vrf1_obj_->RouteNeedsMacBinding(rt));

    ksync->BuildRouteFlags(rt, vnet1_->mac());
    EXPECT_TRUE(ksync->proxy_arp());
    EXPECT_FALSE(ksync->flood());

    vrf1_uc_table_->DeleteReq(NULL, "vrf1", addr, 32, NULL);
    client->WaitForIdle();
}

// proxy_arp_ and flood_ flags for remote-route when MAC not stitched
TEST_F(TestKSyncRoute, remote_route_2) {
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
    };
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    IpAddress addr = IpAddress(Ip4Address::from_string("1.1.1.100"));
    AddRemoteRoute(addr, 32);

    InetUnicastRouteEntry *rt = vrf1_uc_table_->FindLPM(addr);
    EXPECT_TRUE(rt != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf1_rt_obj_, rt));
    EXPECT_TRUE(vrf1_obj_->RouteNeedsMacBinding(rt));

    ksync->BuildRouteFlags(rt, MacAddress());
    EXPECT_FALSE(ksync->proxy_arp());
    EXPECT_TRUE(ksync->flood());

    vrf1_uc_table_->DeleteReq(NULL, "vrf1", addr, 32, NULL);
    DelIPAM("vn1");
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
