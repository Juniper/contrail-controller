/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include "oper/vn.h"
#include "oper/tunnel_nh.h"

#include <boost/assign/list_of.hpp>

#define BUF_SIZE (12 * 1024)

using namespace std;
using namespace boost::assign;

void RouterIdDepInit(Agent *agent) {
}

class MulticastTest : public ::testing::Test {
public:
    MulticastTest() : agent_(Agent::GetInstance()) {}
    virtual void SetUp() {
        peer_ = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    }

    virtual void TearDown() {
        WAIT_FOR(1000, 10000, (agent_->vn_table()->Size() == 0));
        WAIT_FOR(1000, 10000, (agent_->vrf_table()->Size() == 2));
        DeleteBgpPeer(peer_);
    }
    Agent *agent_;
    Peer *peer_;
};

// Basic BUM route creation.
// Happens when VMs are added to a VRF.
// Two VMs added to VRF resulting in broadcast route being added
// with the two VMs as the members of the nexthop.
TEST_F(MulticastTest, Mcast_vm_add_basic) {
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.3", "00:00:11:01:01:03", 1, 1},
        {"vnet2", 2, "11.1.1.4", "00:00:11:01:01:04", 1, 1},
    };

    CreateVmportEnv(input, 2, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));

    MacAddress mac = MacAddress::kBroadcastMac;
    BridgeRouteEntry *rt = L2RouteGet("vrf1", mac);
    EXPECT_TRUE((rt != NULL));

    const NextHop *nh;
    const CompositeNH *cnh, *cnh1;
    const ComponentNH *component_nh;

    nh = L2RouteToNextHop("vrf1", mac);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh->ActiveComponentNHCount());

    component_nh = cnh->Get(0);
    nh = component_nh->nh();
    EXPECT_TRUE((nh != NULL));
    cnh1 = dynamic_cast<const CompositeNH *>(nh);
    // 2 indicates number of VMs in the VN/VRF.
    EXPECT_EQ(2, cnh1->ActiveComponentNHCount());

    DeleteVmportEnv(input, 2, 1, 0);
    client->WaitForIdle();
}

// BUM route creation with neighbor compute.
// Happens when VMs are added to a VRF.
// Two VMs added to VRF resulting in broadcast route being added
// with the two VMs as the members of the nexthop.
// Also, a dummy compute is added to the Nexthop - 8.8.8.8.
// Fabric composite nexthop contains a tunnel nexthop to 8.8.8.8.
TEST_F(MulticastTest, Mcast_fabric_nexthop_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.3", "00:00:11:01:01:03", 1, 1},
        {"vnet2", 2, "11.1.1.4", "00:00:11:01:01:04", 1, 1},
    };

    CreateVmportEnv(input, 2, 0);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));
    WAIT_FOR(1000, 1000, (VmPortActive(input, 1) == true));

    MacAddress mac = MacAddress::kBroadcastMac;
    BridgeRouteEntry *rt = L2RouteGet("vrf1", mac);
    EXPECT_TRUE((rt != NULL));

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 2000,
                            IpAddress::from_string("8.8.8.8").to_v4(),
                            TunnelType::AllType()));
    agent_->oper_db()->multicast()->
        ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                            IpAddress::from_string("255.255.255.255").to_v4(),
                            IpAddress::from_string("0.0.0.0").to_v4(),
                            1112, olist_map,
                            agent_->controller()->multicast_sequence_number());
    client->WaitForIdle();

    const NextHop *nh;
    const CompositeNH *cnh, *cnh1;
    const ComponentNH *component_nh;

    nh = L2RouteToNextHop("vrf1", mac);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    component_nh = cnh->Get(0);
    nh = component_nh->nh();
    EXPECT_TRUE((nh != NULL));
    cnh1 = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh1->ActiveComponentNHCount());

    component_nh = cnh->Get(1);
    nh = component_nh->nh();
    EXPECT_TRUE((nh != NULL));
    cnh1 = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh1->ActiveComponentNHCount());

    DeleteVmportEnv(input, 2, 1, 0);
    client->WaitForIdle();
}

// BUM route creation with neighbor compute.
// Happens when VMs are added to a VRF.
// Two VMs added to VRF resulting in broadcast route being added
// with the two VMs as the members of the nexthop.
// Also, dummy computes is added to the Nexthop - 8.8.8.8 and 8.8.8.9.
// Fabric composite nexthop contains tunnel nexthops to 8.8.8.8 and 8.8.8.9.
// second half of test consist of having only one tunnel as an update to the
// route.
TEST_F(MulticastTest, Mcast_fabric_nexthop_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.3", "00:00:11:01:01:03", 1, 1},
        {"vnet2", 2, "11.1.1.4", "00:00:11:01:01:04", 1, 1},
    };

    CreateVmportEnv(input, 2, 0);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));
    WAIT_FOR(1000, 1000, (VmPortActive(input, 1) == true));

    MacAddress mac = MacAddress::kBroadcastMac;
    BridgeRouteEntry *rt = L2RouteGet("vrf1", mac);
    EXPECT_TRUE((rt != NULL));

    TunnelOlist olist_map;
    const NextHop *nh;
    const CompositeNH *cnh, *cnh1;
    const ComponentNH *component_nh;

    olist_map.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 2000,
                            IpAddress::from_string("8.8.8.8").to_v4(),
                            TunnelType::AllType()));
    olist_map.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 2000,
                            IpAddress::from_string("8.8.8.9").to_v4(),
                            TunnelType::AllType()));
    agent_->oper_db()->multicast()->
        ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                            IpAddress::from_string("255.255.255.255").to_v4(),
                            IpAddress::from_string("0.0.0.0").to_v4(),
                            1112, olist_map,
                            agent_->controller()->multicast_sequence_number());
    client->WaitForIdle();

    nh = L2RouteToNextHop("vrf1", mac);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    component_nh = cnh->Get(0);
    nh = component_nh->nh();
    EXPECT_TRUE((nh != NULL));
    cnh1 = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh1->ActiveComponentNHCount());

    component_nh = cnh->Get(1);
    nh = component_nh->nh();
    EXPECT_TRUE((nh != NULL));
    cnh1 = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh1->ActiveComponentNHCount());

    olist_map.clear();
    olist_map.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 2000,
                            IpAddress::from_string("8.8.8.8").to_v4(),
                            TunnelType::AllType()));
    agent_->oper_db()->multicast()->
        ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                            IpAddress::from_string("255.255.255.255").to_v4(),
                            IpAddress::from_string("0.0.0.0").to_v4(),
                            1112, olist_map,
                            agent_->controller()->multicast_sequence_number());
    client->WaitForIdle();

    nh = L2RouteToNextHop("vrf1", mac);

    cnh = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh->ActiveComponentNHCount());

    component_nh = cnh->Get(0);
    nh = component_nh->nh();
    EXPECT_TRUE((nh != NULL));
    cnh1 = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(1, cnh1->ActiveComponentNHCount());

    component_nh = cnh->Get(1);
    nh = component_nh->nh();
    EXPECT_TRUE((nh != NULL));
    cnh1 = dynamic_cast<const CompositeNH *>(nh);
    EXPECT_EQ(2, cnh1->ActiveComponentNHCount());

    DeleteVmportEnv(input, 2, 1, 0);
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
