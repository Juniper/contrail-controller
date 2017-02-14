/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <boost/assign/list_of.hpp>

#include <cfg/cfg_init.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/mirror_table.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "vr_types.h"

using namespace std;
using namespace boost::assign;

std::string analyzer = "TestAnalyzer";

//TestClient *client;

void RouterIdDepInit(Agent *agent) {
}

class EcmpV6Test : public ::testing::Test {
public:
    void SetUp() {
        boost::system::error_code ec;
        bgp_peer = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                 "xmpp channel");
        agent_ = Agent::GetInstance();
    }
    void TearDown() {
        WAIT_FOR(1000, 1000, agent_->vrf_table()->Size() == 1);
        DeleteBgpPeer(bgp_peer);
    }

    Agent *agent_;
    BgpPeer *bgp_peer;
};

TEST_F(EcmpV6Test, EcmpNH_1) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1, "fd11::3"},
        {"vnet2", 2, "1.1.1.1", "00:00:00:02:02:02", 1, 2, "fd11::3"},
        {"vnet3", 3, "1.1.1.1", "00:00:00:02:02:03", 1, 3, "fd11::3"},
        {"vnet4", 4, "1.1.1.1", "00:00:00:02:02:04", 1, 4, "fd11::3"},
        {"vnet5", 5, "1.1.1.1", "00:00:00:02:02:05", 1, 5, "fd11::3"},
    };
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"},
        {"fd11::", 96, "fd11::1"},
    };

    CreateV6VmportWithEcmp(input, 5, 0);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();

    boost::system::error_code ec;
    Ip6Address addr = Ip6Address::from_string(input[0].ip6addr, ec);
    InetUnicastRouteEntry* rt = RouteGetV6("vrf1", addr, 128);
    EXPECT_TRUE(rt != NULL);

    //Verify that the route points to composite NH and the composite NH has
    //right number of component NHs
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 5);

    //Get the MPLS label corresponding to this path and verify
    //that mpls label also has 5 component NH
    uint32_t mpls_label = rt->GetActiveLabel();
    EXPECT_TRUE(FindMplsLabel(mpls_label));
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 5);

    //Cleanup
    DeleteVmportEnv(input, 5, 1, 0, NULL, NULL, true, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();

    EXPECT_FALSE(RouteFindV6("vrf1", addr, 128));
    //Expect MPLS label to be not present
    EXPECT_FALSE(FindMplsLabel(mpls_label));
}

//Create multiple VM with same virtual IP and verify
//ecmp NH gets created and also verify that it gets deleted
//upon VM deletion.
TEST_F(EcmpV6Test, EcmpNH_2) {
    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1, "fd11::3"},
    };

    struct PortInfo input2[] = {
        {"vnet2", 2, "1.1.1.1", "00:00:00:02:02:02", 1, 2, "fd11::3"},
    };

    struct PortInfo input3[] = {
        {"vnet3", 3, "1.1.1.1", "00:00:00:02:02:03", 1, 3, "fd11::3"},
    };

    struct PortInfo input4[] = {
        {"vnet4", 4, "1.1.1.1", "00:00:00:02:02:04", 1, 4, "fd11::3"},
    };

    struct PortInfo input5[] = {
        {"vnet5", 5, "1.1.1.1", "00:00:00:02:02:05", 1, 5, "fd11::3"},
    };
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"},
        {"fd11::", 96, "fd11::1"},
    };

    //CreateVmportWithEcmp(input1, 1, 1);
    CreateV6VmportWithEcmp(input1, 1, 0);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();

    //First VM added, verify route points to Interface NH
    boost::system::error_code ec;
    Ip6Address addr = Ip6Address::from_string(input1[0].ip6addr, ec);
    InetUnicastRouteEntry* rt = RouteGetV6("vrf1", addr, 128);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::INTERFACE);
    //EXPECT_TRUE(nh->PolicyEnabled() == true);

    //Second VM added, route should point to composite NH
    CreateV6VmportWithEcmp(input2, 1, 0);
    client->WaitForIdle();
    nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(comp_nh->Get(0)->nh());
    EXPECT_TRUE(intf_nh->PolicyEnabled() == false);
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    CreateV6VmportWithEcmp(input3, 1, 0);
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 3);

    CreateV6VmportWithEcmp(input4, 1, 0);
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 4);

    CreateV6VmportWithEcmp(input5, 1, 0);
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 5);

    //Verify all the component NH have right label and nexthop
    ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");
    MplsLabel *mpls = GetActiveLabel((*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");
    component_nh_it++;

    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");
    mpls = GetActiveLabel((*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");
    component_nh_it++;

    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");
    mpls = GetActiveLabel((*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");
    component_nh_it++;

    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");
    mpls = GetActiveLabel((*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");
    component_nh_it++;

    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");
    mpls = GetActiveLabel((*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");

    //Verify that mpls label allocated for ECMP route, points 
    //to the same composite NH
    uint32_t composite_nh_mpls_label = rt->GetActiveLabel();
    mpls = GetActiveLabel(composite_nh_mpls_label);
    EXPECT_TRUE(mpls->nexthop() == comp_nh);

    //Delete couple of interface and verify composite NH also get deleted
    DeleteVmportEnv(input2, 1, 0, 0, NULL, NULL, true, true);
    DeleteVmportEnv(input4, 1, 0, 0, NULL, NULL, true, true);
    client->WaitForIdle();

    //Verify all the component NH have right label and nexthop
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    component_nh_it = comp_nh->begin();
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");
    mpls = GetActiveLabel((*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    //Interface 2 and 4 have been deleted, expected the component NH to
    //be NULL
    component_nh_it++;
    EXPECT_TRUE(*component_nh_it == NULL);

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");
    mpls = GetActiveLabel((*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");

    //Interface vnet4 has been deleted, expect the component NH to be NULL
    component_nh_it++;
    EXPECT_TRUE(*component_nh_it == NULL);

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");
    mpls = GetActiveLabel((*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");

    DeleteVmportEnv(input3, 1, 0, 0, NULL, NULL, true, true);
    DeleteVmportEnv(input5, 1, 0, 0, NULL, NULL, true, true);
    DeleteVmportEnv(input1, 1, 1, 0, NULL, NULL, true, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(RouteFindV6("vrf1", addr, 128));
    
    //Expect MPLS label to be not present
    EXPECT_FALSE(FindMplsLabel(composite_nh_mpls_label));

    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
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
