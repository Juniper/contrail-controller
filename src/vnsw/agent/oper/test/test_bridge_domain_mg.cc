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

#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h>
#include <ksync/ksync_sock_user.h>
#include <boost/assign/list_of.hpp>
#include "oper/bridge_domain.h"

using namespace boost::assign;

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 1}
};

class BridgeDomainMGTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent = Agent::GetInstance();
        CreateVmportEnv(input, 2);
        client->WaitForIdle();

        AddBridgeDomain("bridge1", 1, 1, false);
        client->WaitForIdle();

        AddLink("virtual-network", "vn1", "bridge-domain", "bridge1");
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DeleteVmportEnv(input, 2, true);
        client->WaitForIdle();

        DelLink("virtual-network", "vn1", "bridge-domain", "bridge1");
        DelNode("bridge-domain", "bridge1");
        client->WaitForIdle();

        EXPECT_TRUE(agent->bridge_domain_table()->Size() == 0);
    }

    void CreateBridgeDomain(const char *intf_name, uint32_t id) {
        AddVmportBridgeDomain(intf_name, id);
        AddLink("virtual-machine-interface-bridge-domain", intf_name,
                "bridge-domain", "bridge1",
                "virtual-machine-interface-bridge-domain");
        AddLink("virtual-machine-interface-bridge-domain", intf_name,
                "virtual-machine-interface", intf_name,
                "virtual-machine-interface-bridge-domain");
        client->WaitForIdle();
    }

    void DeleteBridgeDomain(const char *intf_name) {
        DelLink("virtual-machine-interface-bridge-domain", intf_name,
                "bridge-domain", "bridge1");
        DelLink("virtual-machine-interface-bridge-domain", intf_name,
                "virtual-machine-interface", intf_name);
        DelLink("virtual-network", "vn1", "bridge-domain", "bridge1");
        DelNode("virtual-machine-interface-bridge-domain", intf_name);
        client->WaitForIdle();
    }

protected:
    Agent *agent;
};

TEST_F(BridgeDomainMGTest, Test1) {
    CreateBridgeDomain(input[0].name, 1);
    client->WaitForIdle();

    EXPECT_TRUE(L2RouteFind("vrf1:1", MacAddress::BroadcastMac()));
    const VmInterface *vm_intf =
        static_cast<const VmInterface *>(VmPortGet(1));

    BridgeRouteEntry *l2_rt =
        L2RouteGet("vrf1:1", MacAddress("FF:FF:FF:FF:FF:FF"));
    NextHop *l2_nh = const_cast<NextHop *>(l2_rt->GetActiveNextHop());
    const CompositeNH *cnh = dynamic_cast<const CompositeNH *>(l2_nh);
    cnh = dynamic_cast<const CompositeNH *>(cnh->GetNH(0));

    EXPECT_TRUE(cnh->composite_nh_type() == Composite::L2INTERFACE);
    EXPECT_TRUE(cnh->ComponentNHCount() == 1);

    const InterfaceNH *intf_nh =
        static_cast<const InterfaceNH *>(cnh->GetNH(0));
    EXPECT_TRUE(intf_nh->GetInterface() == vm_intf);

    DeleteBridgeDomain(input[0].name);
    client->WaitForIdle();
}

TEST_F(BridgeDomainMGTest, Test2) {
    CreateBridgeDomain(input[0].name, 1);
    client->WaitForIdle();

    CreateBridgeDomain(input[1].name, 2);
    client->WaitForIdle();

    const VmInterface *vm_intf1 =
        static_cast<const VmInterface *>(VmPortGet(1));
    const VmInterface *vm_intf2 =
        static_cast<const VmInterface *>(VmPortGet(2));

    EXPECT_TRUE(L2RouteFind("vrf1:1", MacAddress::BroadcastMac()));

    BridgeRouteEntry *l2_rt =
        L2RouteGet("vrf1:1", MacAddress("FF:FF:FF:FF:FF:FF"));
    NextHop *l2_nh = const_cast<NextHop *>(l2_rt->GetActiveNextHop());
    const CompositeNH *cnh = dynamic_cast<const CompositeNH *>(l2_nh);
    cnh = dynamic_cast<const CompositeNH *>(cnh->GetNH(0));

    EXPECT_TRUE(cnh->composite_nh_type() == Composite::L2INTERFACE);
    EXPECT_TRUE(cnh->ComponentNHCount() == 2);

    const InterfaceNH *intf_nh =
        static_cast<const InterfaceNH *>(cnh->GetNH(0));
    EXPECT_TRUE(intf_nh->GetInterface() == vm_intf1);
    intf_nh = static_cast<const InterfaceNH *>(cnh->GetNH(1));
    EXPECT_TRUE(intf_nh->GetInterface() == vm_intf2);

    DeleteBridgeDomain(input[0].name);
    DeleteBridgeDomain(input[1].name);
    client->WaitForIdle();
}

TEST_F(BridgeDomainMGTest, Test3) {
    //Add a peer and enqueue path add in multicast route.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    agent->mpls_table()->ReserveMulticastLabel(4000, 5000, 0);
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent->
                                       oper_db()->multicast());

    CreateBridgeDomain(input[0].name, 1);
    CreateBridgeDomain(input[1].name, 2);
    client->WaitForIdle();

    Ip4Address sip(0);
    Ip4Address broadcast(0xFFFFFFFF);

    //Modify olist for pbb VRF and verify it gets
    //updated for cmac vrf
    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(nil_uuid(), 10,
                                     IpAddress::from_string("8.8.8.8").to_v4(),
                                     TunnelType::MplsType()));
    mc_handler->ModifyFabricMembers(agent->multicast_tree_builder_peer(),
                                    "vrf1", broadcast, sip, 4100, olist, 1);
    client->WaitForIdle();

    BridgeRouteEntry *l2_rt =
        L2RouteGet("vrf1:1", MacAddress("FF:FF:FF:FF:FF:FF"));
    NextHop *l2_nh = const_cast<NextHop *>(l2_rt->GetActiveNextHop());
    const CompositeNH *cnh = dynamic_cast<const CompositeNH *>(l2_nh);
    const CompositeNH *fcnh = dynamic_cast<const CompositeNH *>(cnh->GetNH(0));
    const CompositeNH *icnh = dynamic_cast<const CompositeNH *>(cnh->GetNH(1));

    EXPECT_TRUE(fcnh->ComponentNHCount() == 1);
    EXPECT_TRUE(fcnh->GetNH(0)->GetType() == NextHop::TUNNEL);

    EXPECT_TRUE(icnh->composite_nh_type() == Composite::L2INTERFACE);
    EXPECT_TRUE(icnh->ComponentNHCount() == 2);

    DeleteBridgeDomain(input[0].name);
    DeleteBridgeDomain(input[1].name);
    DeleteBgpPeer(bgp_peer_ptr);
    client->WaitForIdle();
}

TEST_F(BridgeDomainMGTest, Test4) {
    std::stringstream str;
    //Add a peer and enqueue path add in multicast route.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    agent->mpls_table()->ReserveMulticastLabel(4000, 5000, 0);
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent->
                                       oper_db()->multicast());

    Ip4Address sip(0);
    Ip4Address broadcast(0xFFFFFFFF);
    //Modify olist for pbb VRF and verify it gets
    //updated for cmac vrf
    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(nil_uuid(), 10,
                                     IpAddress::from_string("8.8.8.8").to_v4(),
                                     TunnelType::MplsType()));
    mc_handler->ModifyFabricMembers(agent->multicast_tree_builder_peer(),
                                    "vrf1", broadcast, sip, 4100, olist, 1);
    client->WaitForIdle();

    CreateBridgeDomain(input[0].name, 1);
    CreateBridgeDomain(input[1].name, 2);
    client->WaitForIdle();

    BridgeRouteEntry *l2_rt =
        L2RouteGet("vrf1:1", MacAddress("FF:FF:FF:FF:FF:FF"));
    NextHop *l2_nh = const_cast<NextHop *>(l2_rt->GetActiveNextHop());
    const CompositeNH *cnh = dynamic_cast<const CompositeNH *>(l2_nh);
    const CompositeNH *fcnh = dynamic_cast<const CompositeNH *>(cnh->GetNH(0));
    const CompositeNH *icnh = dynamic_cast<const CompositeNH *>(cnh->GetNH(1));

    EXPECT_TRUE(fcnh->ComponentNHCount() == 1);
    EXPECT_TRUE(fcnh->GetNH(0)->GetType() == NextHop::TUNNEL);

    EXPECT_TRUE(icnh->composite_nh_type() == Composite::L2INTERFACE);
    EXPECT_TRUE(icnh->ComponentNHCount() == 2);

    str.clear();
    str << "<pbb-etree-enable>"<< "true" << "</pbb-etree-enable>";
    AddNode("virtual-network", "vn1", 1, str.str().c_str());
    client->WaitForIdle();

    l2_nh = const_cast<NextHop *>(l2_rt->GetActiveNextHop());
    cnh = dynamic_cast<const CompositeNH *>(l2_nh);
    icnh = dynamic_cast<const CompositeNH *>(cnh->GetNH(0));
    EXPECT_TRUE(icnh->composite_nh_type() == Composite::L2INTERFACE);
    EXPECT_TRUE(icnh->ComponentNHCount() == 2);

    DeleteBridgeDomain(input[0].name);
    DeleteBridgeDomain(input[1].name);
    DeleteBgpPeer(bgp_peer_ptr);
    client->WaitForIdle();
}

TEST_F(BridgeDomainMGTest, Test5) {
    std::stringstream str;
    //Add a peer and enqueue path add in multicast route.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    agent->mpls_table()->ReserveMulticastLabel(4000, 5000, 0);
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent->
                                       oper_db()->multicast());

    CreateBridgeDomain(input[0].name, 1);
    client->WaitForIdle();

    //Modify olist for pbb VRF and verify it gets
    //updated for cmac vrf
    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(nil_uuid(), 10,
                                     IpAddress::from_string("8.8.8.8").to_v4(),
                                     TunnelType::MplsType()));
    mc_handler->ModifyEvpnMembers(bgp_peer_ptr,
                                  "vrf1", olist, 1, 1);
    client->WaitForIdle();

    BridgeRouteEntry *l2_rt =
        L2RouteGet("vrf1:1", MacAddress("FF:FF:FF:FF:FF:FF"));
    NextHop *l2_nh = const_cast<NextHop *>(l2_rt->GetActiveNextHop());
    const CompositeNH *cnh = dynamic_cast<const CompositeNH *>(l2_nh);
    const CompositeNH *fcnh = dynamic_cast<const CompositeNH *>(cnh->GetNH(0));

    EXPECT_TRUE(fcnh->ComponentNHCount() == 1);
    EXPECT_TRUE(fcnh->GetNH(0)->GetType() == NextHop::TUNNEL);

    str.clear();
    str << "<pbb-etree-enable>"<< "true" << "</pbb-etree-enable>";
    AddNode("virtual-network", "vn1", 1, str.str().c_str());
    client->WaitForIdle();

    l2_nh = const_cast<NextHop *>(l2_rt->GetActiveNextHop());
    cnh = dynamic_cast<const CompositeNH *>(l2_nh);
    fcnh = dynamic_cast<const CompositeNH *>(cnh->GetNH(0));

    EXPECT_TRUE(fcnh->ComponentNHCount() == 1);
    EXPECT_TRUE(fcnh->GetNH(0)->GetType() == NextHop::TUNNEL);

    DeleteBridgeDomain(input[0].name);
    DeleteBgpPeer(bgp_peer_ptr);
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
