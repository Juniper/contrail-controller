/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
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
#include "vr_flow.h"

using namespace std;
using namespace boost::assign;

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

void RouterIdDepInit(Agent *agent) {
}

class EcmpNhTest : public ::testing::Test {
public:
    void SetUp() {
        boost::system::error_code ec;
        bgp_peer = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                 "xmpp channel");
        agent_ = Agent::GetInstance();
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();
    }
    void TearDown() {
        WAIT_FOR(1000, 1000, agent_->vrf_table()->Size() == 2);
        DeleteBgpPeer(bgp_peer);
        DelIPAM("vn1");
        client->WaitForIdle();
    }

    Agent *agent_;
    BgpPeer *bgp_peer;
};

TEST_F(EcmpNhTest, DISABLED_EcmpNH_controller) {
    client->WaitForIdle();
    struct PortInfo input1[] = {
        {"vnet10", 10, "1.1.1.1", "00:00:00:01:01:01", 10, 10}
    };

    client->Reset();
    AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
    client->WaitForIdle();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    //Now add remote route with ECMP comp NH
    Ip4Address ip1 = Ip4Address::from_string("9.9.9.1");
    Ip4Address ip2 = Ip4Address::from_string("9.9.9.2");
    TunnelNHKey *nh_key = new TunnelNHKey(agent_->fabric_vrf_name(),
                                          agent_->router_id(),
                                          ip1, false,
                                          TunnelType::MPLS_GRE);
    std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
    TunnelNHKey *nh_key_2 = new TunnelNHKey(agent_->fabric_vrf_name(),
                                            agent_->router_id(),
                                            ip2, false,
                                            TunnelType::MPLS_GRE);
    std::auto_ptr<const NextHopKey> nh_key_ptr_2(nh_key_2);

    ComponentNHKeyPtr component_nh_key(new ComponentNHKey(1000,
                                                          nh_key_ptr));
    ComponentNHKeyPtr component_nh_key_2(new ComponentNHKey(1001,
                                                            nh_key_ptr_2));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(component_nh_key);
    comp_nh_list.push_back(component_nh_key_2);

    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::ECMP, true,
                                        comp_nh_list, "vrf10"));
    nh_req.data.reset(new CompositeNHData());
    Ip4Address prefix = Ip4Address::from_string("18.18.18.0");
    PathPreference rp(100, PathPreference::LOW, false, false);
    SecurityGroupList sg;
    BgpPeer *peer_;
    peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1"),
                          "xmpp channel");
    VnListType vn_list;
    vn_list.insert("vn10");
    ControllerEcmpRoute *data =
        new ControllerEcmpRoute(peer_, vn_list, EcmpLoadBalance(),
                                TagList(), sg, rp,
                                (1 << TunnelType::MPLS_GRE),
                                nh_req, prefix.to_string());

    //ECMP create component NH
    InetUnicastAgentRouteTable::AddRemoteVmRouteReq(peer_, "vrf10",
                                                    prefix, 24, data);
    client->WaitForIdle();
    InetUnicastRouteEntry *rt = RouteGet("vrf10", prefix, 24);
    EXPECT_TRUE(rt != NULL);
    const CompositeNH *cnh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    const TunnelNH *tnh = static_cast<const TunnelNH *>(cnh->GetNH(0));
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);
    tnh = static_cast<const TunnelNH *>(cnh->GetNH(1));
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    AddEncapList("MPLSoUDP", "MPLSoGRE", "VXLAN");
    client->WaitForIdle();
    rt = RouteGet("vrf10", prefix, 24);
    EXPECT_TRUE(rt != NULL);
    cnh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    tnh = static_cast<const TunnelNH *>(cnh->GetNH(0));
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);
    tnh = static_cast<const TunnelNH *>(cnh->GetNH(1));
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    //cleanup
    DeleteRoute("vrf10", "18.18.18.0", 24, peer_);
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    DeleteBgpPeer(peer_);
    client->WaitForIdle();
}

TEST_F(EcmpNhTest, EcmpNH_1) {
    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.1", "00:00:00:02:02:01", 1, 2},
        {"vnet3", 3, "1.1.1.1", "00:00:00:02:02:03", 1, 3},
        {"vnet4", 4, "1.1.1.1", "00:00:00:02:02:04", 1, 4},
        {"vnet5", 5, "1.1.1.1", "00:00:00:02:02:05", 1, 5},
    };

    CreateVmportWithEcmp(input1, 5);
    client->WaitForIdle();

    //Check that route points to composite NH,
    //with 5 members
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    InetUnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
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
    EXPECT_FALSE(comp_nh->PolicyEnabled() == false);

    DeleteVmportEnv(input1, 5, true);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));

    //Expect MPLS label to be not present
    EXPECT_FALSE(FindMplsLabel(mpls_label));
}
//ecmp NH gets created and also verify that it gets deleted
//upon VM deletion.
TEST_F(EcmpNhTest, EcmpNH_2) {
    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    struct PortInfo input2[] = {
        {"vnet2", 2, "1.1.1.1", "00:00:00:02:02:01", 1, 2}
    };

    struct PortInfo input3[] = {
        {"vnet3", 3, "1.1.1.1", "00:00:00:02:02:03", 1, 3}
    };

    struct PortInfo input4[] = {
        {"vnet4", 4, "1.1.1.1", "00:00:00:02:02:04", 1, 4}
    };

    struct PortInfo input5[] = {
        {"vnet5", 5, "1.1.1.1", "00:00:00:02:02:05", 1, 5}
    };

    CreateVmportWithEcmp(input1, 1, 1);
    client->WaitForIdle();
    //First VM added, route points to composite NH
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    InetUnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(nh->PolicyEnabled() == true);
    VmInterface *intf = VmInterfaceGet(input1[0].intf_id);
    EXPECT_TRUE(intf != NULL);
    EXPECT_TRUE(intf->policy_enabled());

    //Second VM added, route should point to composite NH
    CreateVmportWithEcmp(input2, 1);
    client->WaitForIdle();
    nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->PolicyEnabled() == true);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(comp_nh->Get(0)->nh());
    EXPECT_TRUE(intf_nh->PolicyEnabled() == false);
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    CreateVmportWithEcmp(input3, 1);
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 3);

    CreateVmportWithEcmp(input4, 1);
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 4);

    CreateVmportWithEcmp(input5, 1);
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 5);
    EXPECT_TRUE(comp_nh->PolicyEnabled() == true);

    //Verify all the component NH have right label and nexthop
    ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    intf_nh = static_cast<const InterfaceNH *>
                                     ((*component_nh_it)->nh());
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


    //Delete couple of interface and verify composite NH also get
    //deleted
    DeleteVmportEnv(input2, 1, false);
    DeleteVmportEnv(input4, 1, false);
    client->WaitForIdle();

    //Verify all the component NH have right label and nexthop
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->PolicyEnabled() == true);
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

    DeleteVmportEnv(input3, 1, false);
    DeleteVmportEnv(input5, 1, false);
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));

    //Expect MPLS label to be not present
    EXPECT_FALSE(FindMplsLabel(composite_nh_mpls_label));

    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
}

//Create multiple VM with same floating IP and verify
//ecmp NH gets created and also verify that it gets deleted
//upon floating IP disassociation
TEST_F(EcmpNhTest, EcmpNH_3) {
    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:01", 1, 2},
        {"vnet3", 3, "1.1.1.3", "00:00:00:02:02:03", 1, 3},
        {"vnet4", 4, "1.1.1.4", "00:00:00:02:02:04", 1, 4},
        {"vnet5", 5, "1.1.1.5", "00:00:00:02:02:05", 1, 5}
    };

    CreateVmportFIpEnv(input1, 5);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortActive(2));
    EXPECT_TRUE(VmPortActive(3));
    EXPECT_TRUE(VmPortActive(4));
    EXPECT_TRUE(VmPortActive(5));

    //Create one dummy interface vrf2 for floating IP
    struct PortInfo input2[] = {
        {"vnet6", 6, "1.1.1.1", "00:00:00:01:01:01", 2, 6},
    };
    CreateVmportFIpEnv(input2, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(6));

    //Create floating IP pool
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.2.2.2");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");

    //Associate vnet1 with floating IP
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    //First VM added, route points to composite NH
    Ip4Address ip = Ip4Address::from_string("2.2.2.2");
    InetUnicastRouteEntry *rt = RouteGet("default-project:vn2:vn2", ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::INTERFACE);

    //Second VM added with same floating IP, route should point to composite NH
    AddLink("virtual-machine-interface", "vnet2", "floating-ip", "fip1");
    client->WaitForIdle();
    nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);

    AddLink("virtual-machine-interface", "vnet3", "floating-ip", "fip1");
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 3);

    AddLink("virtual-machine-interface", "vnet4", "floating-ip", "fip1");
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 4);

    AddLink("virtual-machine-interface", "vnet5", "floating-ip", "fip1");
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 5);

    //Verify all the component NH have right label and nexthop
    ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
                                     ((*component_nh_it)->nh());
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
    uint32_t composite_mpls_label = rt->GetActiveLabel();
    mpls = GetActiveLabel(composite_mpls_label);
    EXPECT_TRUE(mpls->nexthop() == comp_nh);

    //Delete couple of interface and verify composite NH also get
    //deleted
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());

    //Interface 1 has been deleted, expected the component NH to
    //be NULL
    component_nh_it = comp_nh->begin();
    EXPECT_TRUE(*component_nh_it == NULL);

    DelLink("virtual-machine-interface", "vnet2", "floating-ip", "fip1");
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    //Interface 2 has been deleted, expected the component NH to
    //be NULL
    component_nh_it = comp_nh->begin();
    component_nh_it++;
    EXPECT_TRUE(*component_nh_it == NULL);

    DelLink("virtual-machine-interface", "vnet3", "floating-ip", "fip1");
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    //be NULL
    component_nh_it = comp_nh->begin();
    component_nh_it++;
    component_nh_it++;
    EXPECT_TRUE(*component_nh_it == NULL);

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

    //Delete the vnet4 floating ip. Since only vent5 has floating IP
    //route should point to interface NH
    DelLink("virtual-machine-interface", "vnet4", "floating-ip", "fip1");
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActiveNextHop() == intf_nh);

    //Expect MPLS label to be not present
    EXPECT_FALSE(FindMplsLabel(composite_mpls_label));

    DelLink("virtual-machine-interface", "vnet5", "floating-ip", "fip1");
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network", "default-project:vn2");
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("default-project:vn2:vn2", ip, 32));

    DeleteVmportFIpEnv(input1, 5, true);
    DeleteVmportFIpEnv(input2, 1, true);
    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
    WAIT_FOR(100, 1000, (VrfFind("vrf2") == false));
    client->WaitForIdle();
}

//Create a ECMP NH on a delete marked VRF,
//and make sure NH is not created
TEST_F(EcmpNhTest, EcmpNH_4) {
    AddVrf("vrf2");
    client->WaitForIdle();

    VrfEntryRef vrf1 = VrfGet("vrf2");
    client->WaitForIdle();

    DelVrf("vrf2");
    WAIT_FOR(100, 1000, (VrfFind("vrf2") == false));
    client->WaitForIdle();

    //Enqueue a request to add composite NH
    //since the entry is marked delete, composite NH will not get
    //created
    ComponentNHKeyList comp_nh_list;
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new CompositeNHKey(Composite::ECMP, true, comp_nh_list, "vrf2"));
    req.data.reset(new CompositeNHData());
    agent_->nexthop_table()->Enqueue(&req);

    client->WaitForIdle();

    CompositeNHKey key(Composite::ECMP, true, comp_nh_list, "vrf2");
    EXPECT_FALSE(FindNH(&key));
    vrf1.reset();
}

//Create a remote route first pointing to tunnel NH
//Change the route to point to composite NH with old tunnel NH
//and a new tunnel NH, and make sure
//preexiting NH gets slot 0 in composite NH
TEST_F(EcmpNhTest, EcmpNH_5) {
    AddVrf("vrf2");
    client->WaitForIdle();

    Ip4Address remote_vm_ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    //Add a remote VM route
    Inet4TunnelRouteAdd(bgp_peer, "vrf2", remote_vm_ip, 32, remote_server_ip1,
                        TunnelType::DefaultType(), 30, "vn2",
                        SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();
    //Create component NH list
    //Transition remote VM route to ECMP route
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20,
                                                  agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyList comp_nh_list;
    //Insert new NH first and then existing route NH
    comp_nh_list.push_back(nh_data2);
    comp_nh_list.push_back(nh_data1);

    SecurityGroupList sg_id_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf2", remote_vm_ip, 32,
                       comp_nh_list, -1, "vn2", sg_id_list,
                       TagList(), PathPreference());
    client->WaitForIdle();
    InetUnicastRouteEntry *rt = RouteGet("vrf2", remote_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);

    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);
    EXPECT_TRUE(comp_nh->PolicyEnabled() == false);

    //Verify all the component NH have right label and nexthop
    ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    const TunnelNH *tun_nh1 = static_cast<const TunnelNH *>
        ((*component_nh_it)->nh());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip1);
    EXPECT_TRUE((*component_nh_it)->label() == 30);

    component_nh_it++;
    tun_nh1 = static_cast<const TunnelNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip2);
    EXPECT_TRUE((*component_nh_it)->label() == 20);

    DeleteRoute("vrf2", "1.1.1.1", 32, bgp_peer);
    client->WaitForIdle();
    DelVrf("vrf2");
    WAIT_FOR(100, 1000, (VrfFind("vrf2") == false));
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf2"));
}

//Create a remote route first pointing to tunnel NH
//Change the route to point to composite NH with all new tunnel NH
//make sure preexiting NH doesnt exist in the new component NH list
TEST_F(EcmpNhTest, EcmpNH_6) {
    AddVrf("vrf2");
    client->WaitForIdle();

    Ip4Address remote_vm_ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    Ip4Address remote_server_ip3 = Ip4Address::from_string("10.10.10.102");
    //Add a remote VM route
    Inet4TunnelRouteAdd(bgp_peer, "vrf2",
                        remote_vm_ip,
                        32, remote_server_ip1,
                        TunnelType::AllType(),
                        30, "vn2",
                        SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();
    //Create component NH list
    //Transition remote VM route to ECMP route
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20,
                                                  agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip3,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyList comp_nh_list;
    //Insert new NH first and then existing route NH
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);

    SecurityGroupList sg_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf2", remote_vm_ip, 32,
                       comp_nh_list, -1, "vn2", sg_list,
                       TagList(), PathPreference());
    client->WaitForIdle();
    InetUnicastRouteEntry *rt = RouteGet("vrf2", remote_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);

    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);
    EXPECT_TRUE(comp_nh->PolicyEnabled() == false);

    //Verify all the component NH have right label and nexthop
    ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    const TunnelNH *tun_nh1 = static_cast<const TunnelNH *>
        ((*component_nh_it)->nh());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip2);
    EXPECT_TRUE((*component_nh_it)->label() == 30);

    component_nh_it++;
    tun_nh1 = static_cast<const TunnelNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip3);
    EXPECT_TRUE((*component_nh_it)->label() == 20);

    DeleteRoute("vrf2", "1.1.1.1", 32, bgp_peer);
    client->WaitForIdle();
    DelVrf("vrf2");
    WAIT_FOR(100, 1000, (VrfFind("vrf2") == false));
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf2"));
}

TEST_F(EcmpNhTest, DISABLED_EcmpNH_7) {
    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    struct PortInfo input2[] = {
        {"vnet2", 2, "1.1.1.1", "00:00:00:02:02:01", 1, 2}
    };

    struct PortInfo input3[] = {
        {"vnet3", 3, "1.1.1.1", "00:00:00:02:02:03", 1, 3}
    };

    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();
    //First VM added, route points to composite NH
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    InetUnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::INTERFACE);

    //Second VM added, route should point to composite NH
    CreateVmportWithEcmp(input2, 1);
    client->WaitForIdle();
    nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);
    EXPECT_TRUE(comp_nh->PolicyEnabled() == false);

    DBEntryBase::KeyPtr comp_key = comp_nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(comp_key.release());
    std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(rt->GetActiveLabel(),
                                                  nh_key_ptr));
    Ip4Address remote_server_ip1 = Ip4Address::from_string("11.1.1.1");
    //Leak the route via BGP peer
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);

    SecurityGroupList sg_id_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip, 32,
            comp_nh_list, false, "vn1", sg_id_list,
            TagList(), PathPreference());
    client->WaitForIdle();

    CreateVmportWithEcmp(input3, 1);
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 4);

    //Verify all the component NH have right label and nexthop
    ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
                                     ((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");
    MplsLabel *mpls = GetActiveLabel((*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    component_nh_it++;
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

    //Delete couple of interface and verify composite NH also get
    //deleted
    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();

    //Verify all the component NH have right label and nexthop
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    component_nh_it = comp_nh->begin();
    EXPECT_TRUE(*component_nh_it == NULL);

    component_nh_it++;
    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");
    mpls = GetActiveLabel((*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");

    //Interface vnet3 has been deleted, expect the component NH to be NULL
    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");
    mpls = GetActiveLabel((*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");

    //Create Vmport again
    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 4);

    //Verify all the component NH have right label and nexthop
    component_nh_it = comp_nh->begin();
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");
    mpls = GetActiveLabel((*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    component_nh_it++;
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

    //cleanup
    DeleteVmportEnv(input1, 1, false);
    DeleteVmportEnv(input2, 1, false);
    DeleteVmportEnv(input3, 1, true);
    client->WaitForIdle();
    DeleteRoute("vrf1", "1.1.1.1", 32, bgp_peer);
    client->WaitForIdle();

    EXPECT_FALSE(RouteFind("vrf1", ip, 32));

    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
}

//Add multiple remote routes with same set of composite NH
//make sure they share the composite NH
TEST_F(EcmpNhTest, EcmpNH_8) {
    AddVrf("vrf1");
    client->WaitForIdle();

    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");

    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));

    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);

    Ip4Address ip1 = Ip4Address::from_string("100.1.1.1");
    SecurityGroupList sg_id_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip1, 32,
            comp_nh_list, false, "vn1", sg_id_list,
            TagList(), PathPreference());
    client->WaitForIdle();

    Ip4Address ip2 = Ip4Address::from_string("100.1.1.2");
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip2, 32,
            comp_nh_list, false, "vn1", sg_id_list,
            TagList(), PathPreference());
    client->WaitForIdle();

    InetUnicastRouteEntry *rt1 = RouteGet("vrf1", ip1, 32);
    InetUnicastRouteEntry *rt2 = RouteGet("vrf1", ip2, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt1->GetActiveNextHop()->PolicyEnabled() == false);
    EXPECT_TRUE(rt1->GetActiveNextHop() == rt2->GetActiveNextHop());

    const NextHop *nh = rt1->GetActiveNextHop();
    //Change ip1 route nexthop to be unicast nexthop
    //and ensure ip2 route still points to old composite nexthop
    Inet4TunnelRouteAdd(bgp_peer, "vrf1", ip1, 32, remote_server_ip1,
                        TunnelType::AllType(), 8, "vn1",
                        SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();

    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::TUNNEL);
    EXPECT_TRUE(rt2->GetActiveNextHop() == nh);
    client->WaitForIdle();

    //Delete all the routes and make sure nexthop is also deleted
    DeleteRoute("vrf1", "100.1.1.1", 32, bgp_peer);
    client->WaitForIdle();
    DeleteRoute("vrf1", "100.1.1.2", 32, bgp_peer);
    client->WaitForIdle();

    CompositeNHKey composite_nh_key(Composite::ECMP, true, comp_nh_list, "vrf1");
    EXPECT_FALSE(FindNH(&composite_nh_key));
    DelVrf("vrf1");
}

//Add ECMP composite NH with no member
TEST_F(EcmpNhTest, EcmpNH_9) {
    AddVrf("vrf1");
    client->WaitForIdle();
    ComponentNHKeyList comp_nh_list;
    Ip4Address ip1 = Ip4Address::from_string("100.1.1.1");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.1.1.10");
    SecurityGroupList sg_id_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip1, 32,
            comp_nh_list, false, "vn1", sg_id_list,
            TagList(), PathPreference());
    client->WaitForIdle();

    InetUnicastRouteEntry *rt1 = RouteGet("vrf1", ip1, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt1->GetActiveNextHop()->PolicyEnabled() == false);

    //Change ip1 route nexthop to be unicast nexthop
    Inet4TunnelRouteAdd(bgp_peer, "vrf1", ip1, 32, remote_server_ip1,
                        TunnelType::AllType(), 8, "vn1",
                        SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();

    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::TUNNEL);
    client->WaitForIdle();

    //Delete all the routes and make sure nexthop is also deleted
    DeleteRoute("vrf1", "100.1.1.1", 32, bgp_peer);
    client->WaitForIdle();

    CompositeNHKey composite_nh_key(Composite::ECMP, true, comp_nh_list, "vrf1");
    EXPECT_FALSE(FindNH(&composite_nh_key));
    DelVrf("vrf1");
}

//Add 2 routes with different composite NH
//Modify the routes, to point to same composite NH
//and ensure both routes would share same nexthop
TEST_F(EcmpNhTest, EcmpNH_10) {
    AddVrf("vrf1");
    client->WaitForIdle();

    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    Ip4Address remote_server_ip3 = Ip4Address::from_string("10.10.10.102");

    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data3(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip3,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyList comp_nh_list1;
    comp_nh_list1.push_back(nh_data1);
    comp_nh_list1.push_back(nh_data2);

    Ip4Address ip1 = Ip4Address::from_string("100.1.1.1");
    SecurityGroupList sg_id_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip1, 32,
            comp_nh_list1, false, "vn1", sg_id_list,
            TagList(), PathPreference());
    client->WaitForIdle();

    ComponentNHKeyList comp_nh_list2;
    comp_nh_list2.push_back(nh_data3);
    comp_nh_list2.push_back(nh_data2);

    Ip4Address ip2 = Ip4Address::from_string("100.1.1.2");
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip2, 32,
            comp_nh_list2, false, "vn1", sg_id_list,
            TagList(), PathPreference());
    client->WaitForIdle();

    InetUnicastRouteEntry *rt1 = RouteGet("vrf1", ip1, 32);
    InetUnicastRouteEntry *rt2 = RouteGet("vrf1", ip2, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt1->GetActiveNextHop()->PolicyEnabled() == false);
    EXPECT_TRUE(rt1->GetActiveNextHop() != rt2->GetActiveNextHop());

    const NextHop *nh = rt1->GetActiveNextHop();
    //Change ip2 route, such that ip1 route and ip2 route
    //should share same nexthop
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip2, 32,
                       comp_nh_list1, false, "vn1", sg_id_list,
                       TagList(), PathPreference());
    client->WaitForIdle();

    EXPECT_TRUE(rt1->GetActiveNextHop() == rt2->GetActiveNextHop());
    EXPECT_TRUE(rt2->GetActiveNextHop() == nh);
    EXPECT_TRUE(rt2->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    //Make sure old nexthop is deleted
    CompositeNHKey composite_nh_key2(Composite::ECMP, false, comp_nh_list2, "vrf1");
    EXPECT_FALSE(FindNH(&composite_nh_key2));
    CompositeNHKey composite_nh_key1(Composite::ECMP, false, comp_nh_list1, "vrf1");
    EXPECT_TRUE(GetNH(&composite_nh_key1)->GetRefCount() == 2);

    //Delete all the routes and make sure nexthop is also deleted
    DeleteRoute("vrf1", "100.1.1.1", 32, bgp_peer);
    client->WaitForIdle();
    DeleteRoute("vrf1", "100.1.1.2", 32, bgp_peer);
    client->WaitForIdle();

    EXPECT_FALSE(FindNH(&composite_nh_key1));
    DelVrf("vrf1");
}

//Add 2 routes with different composite NH
//Modify the routes, to point to same composite NH
//and ensure both routes would share same nexthop
TEST_F(EcmpNhTest, EcmpNH_11) {
    AddVrf("vrf1");
    client->WaitForIdle();

    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    Ip4Address remote_server_ip3 = Ip4Address::from_string("10.10.10.102");

    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data3(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip3,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyList comp_nh_list1;
    comp_nh_list1.push_back(nh_data1);
    comp_nh_list1.push_back(nh_data2);

    Ip4Address ip1 = Ip4Address::from_string("100.1.1.1");
    SecurityGroupList sg_id_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip1, 32,
            comp_nh_list1, false, "vn1", sg_id_list,
            TagList(), PathPreference());
    client->WaitForIdle();

    ComponentNHKeyList comp_nh_list2;
    comp_nh_list2.push_back(nh_data1);
    comp_nh_list2.push_back(nh_data2);
    comp_nh_list2.push_back(nh_data3);

    Ip4Address ip2 = Ip4Address::from_string("100.1.1.2");
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip2, 32,
            comp_nh_list2, false, "vn1", sg_id_list,
            TagList(), PathPreference());
    client->WaitForIdle();

    InetUnicastRouteEntry *rt1 = RouteGet("vrf1", ip1, 32);
    InetUnicastRouteEntry *rt2 = RouteGet("vrf1", ip2, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt1->GetActiveNextHop()->PolicyEnabled() == false);
    EXPECT_TRUE(rt1->GetActiveNextHop() != rt2->GetActiveNextHop());
    CompositeNHKey composite_nh_key1(Composite::ECMP, false, comp_nh_list1, "vrf1");
    CompositeNHKey composite_nh_key2(Composite::ECMP, false, comp_nh_list2, "vrf1");
    CompositeNH *composite_nh1 =
        static_cast<CompositeNH *>(GetNH(&composite_nh_key1));
    EXPECT_TRUE(composite_nh1->GetRefCount() == 1);
    EXPECT_TRUE(composite_nh1->ComponentNHCount() == 2);

    CompositeNH *composite_nh2 =
        static_cast<CompositeNH *>(GetNH(&composite_nh_key2));
    EXPECT_TRUE(composite_nh2->GetRefCount() == 1);
    EXPECT_TRUE(composite_nh2->ComponentNHCount() == 3);

    //Change ip1 route, such that ip1 route and ip2 route
    //should share same nexthop
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip1, 32,
                       comp_nh_list2, false, "vn1", sg_id_list,
                       TagList(), PathPreference());
    client->WaitForIdle();

    EXPECT_TRUE(rt1->GetActiveNextHop() == rt2->GetActiveNextHop());
    EXPECT_TRUE(rt2->GetActiveNextHop() == composite_nh2);
    EXPECT_TRUE(rt2->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt2->GetActiveNextHop()->PolicyEnabled() == false);
    //Make sure old nexthop is deleted
    EXPECT_TRUE(composite_nh2->GetRefCount() == 2);
    EXPECT_TRUE(composite_nh2->ComponentNHCount() == 3);

    //Delete all the routes and make sure nexthop is also deleted
    DeleteRoute("vrf1", "100.1.1.1", 32, bgp_peer);
    client->WaitForIdle();
    DeleteRoute("vrf1", "100.1.1.2", 32, bgp_peer);
    client->WaitForIdle();

    EXPECT_FALSE(FindNH(&composite_nh_key1));
    DelVrf("vrf1");
}

//Add a route pointing to tunnel NH
//Change the route to point to ECMP composite NH with no member
TEST_F(EcmpNhTest, EcmpNH_12) {
    AddVrf("vrf1");
    client->WaitForIdle();

    Ip4Address ip1 = Ip4Address::from_string("100.1.1.1");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.1.1.10");
    SecurityGroupList sg_id_list;

    //Change ip1 route nexthop to be unicast nexthop
    Inet4TunnelRouteAdd(bgp_peer, "vrf1", ip1, 32, remote_server_ip1,
                        TunnelType::AllType(), 8, "vn1",
                        SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();

    InetUnicastRouteEntry *rt1 = RouteGet("vrf1", ip1, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::TUNNEL);
    client->WaitForIdle();

    ComponentNHKeyList comp_nh_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip1, 32,
            comp_nh_list, false, "vn1", sg_id_list,
            TagList(), PathPreference());
    client->WaitForIdle();

    rt1 = RouteGet("vrf1", ip1, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt1->GetActiveNextHop()->PolicyEnabled() == false);

    //Delete all the routes and make sure nexthop is also deleted
    DeleteRoute("vrf1", "100.1.1.1", 32, bgp_peer);
    client->WaitForIdle();

    CompositeNHKey composite_nh_key(Composite::ECMP, true, comp_nh_list, "vrf1");
    EXPECT_FALSE(FindNH(&composite_nh_key));
    DelVrf("vrf1");
}

//Verify renewal of composite NH
TEST_F(EcmpNhTest, EcmpNH_13) {
    AddVrf("vrf1");
    client->WaitForIdle();
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");

    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);

    Ip4Address ip1 = Ip4Address::from_string("100.1.1.1");
    SecurityGroupList sg_id_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip1, 32,
            comp_nh_list, false, "vn1", sg_id_list,
            TagList(), PathPreference());
    client->WaitForIdle();

    //Delete composite NH
    CompositeNHKey composite_nh_key(Composite::ECMP, false, comp_nh_list, "vrf1");
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(
            GetNH(&composite_nh_key));
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);
    EXPECT_TRUE(comp_nh->PolicyEnabled() == false);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    NextHopKey *key = new CompositeNHKey(Composite::ECMP, false,
                                         comp_nh_list, "vrf1");
    req.key.reset(key);
    req.data.reset(NULL);
    NextHopTable::GetInstance()->Enqueue(&req);
    client->WaitForIdle();
    EXPECT_TRUE(comp_nh->ActiveComponentNHCount() == 0);
    client->NextHopReset();

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    key = new CompositeNHKey(Composite::ECMP, false,
                             comp_nh_list, "vrf1");
    ((CompositeNHKey *)key)->CreateTunnelNHReq(agent_);
    req.key.reset(key);
    req.data.reset(NULL);
    NextHopTable::GetInstance()->Enqueue(&req);
    client->WaitForIdle();
    client->CompositeNHWait(1);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);

    //Delete all the routes and make sure nexthop is also deleted
    DeleteRoute("vrf1", "100.1.1.1", 32, bgp_peer);
    client->WaitForIdle();

    EXPECT_FALSE(FindNH(&composite_nh_key));
    DelVrf("vrf1");
}

TEST_F(EcmpNhTest, EcmpNH_14) {
    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2}
    };

    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();
    //First VM added, route points to composite NH
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    InetUnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::INTERFACE);

    DBEntryBase::KeyPtr db_nh_key = nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(db_nh_key.release());
    std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(rt->GetActiveLabel(),
                                                  nh_key_ptr));
    Ip4Address remote_server_ip1 = Ip4Address::from_string("11.1.1.1");
    //Leak the route via BGP peer
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);

    SecurityGroupList sg_id_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip, 32,
                       comp_nh_list, false, "vn1", sg_id_list,
                       TagList(), PathPreference());
    client->WaitForIdle();

    //Deactivate vm interface
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    //Transition ecmp to tunnel NH
    Inet4TunnelRouteAdd(bgp_peer, "vrf1", ip, 32, remote_server_ip1,
                        TunnelType::DefaultType(), 30, "vn2",
                        SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();

    //Resync the route
    rt->EnqueueRouteResync();
    client->WaitForIdle();

    DeleteVmportEnv(input1, 2, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1", true));
}

//Create a remote route with ECMP component NH in order A,B and C
//Enqueue change for the same route in different order of composite NH
//verify that nexthop doesnt change
TEST_F(EcmpNhTest, EcmpNH_15) {
    AddVrf("vrf2");
    client->WaitForIdle();

    Ip4Address remote_vm_ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    Ip4Address remote_server_ip3 = Ip4Address::from_string("10.10.10.102");
    //Create component NH list
    //Transition remote VM route to ECMP route
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(15, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20,
                                                  agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data3(new ComponentNHKey(25,
                                                  agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip3,
                                                  false,
                                                  TunnelType::DefaultType()));

    ComponentNHKeyList comp_nh_list;
    //Insert new NH first and then existing route NH
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);
    comp_nh_list.push_back(nh_data3);

    SecurityGroupList sg_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf2", remote_vm_ip, 32,
                       comp_nh_list, -1, "vn2", sg_list,
                       TagList(), PathPreference());
    client->WaitForIdle();
    InetUnicastRouteEntry *rt = RouteGet("vrf2", remote_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);

    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 3);

    //Verify all the component NH have right label and nexthop
    ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    const TunnelNH *tun_nh1 = static_cast<const TunnelNH *>
        ((*component_nh_it)->nh());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip1);
    EXPECT_TRUE((*component_nh_it)->label() == 15);

    component_nh_it++;
    tun_nh1 = static_cast<const TunnelNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip2);
    EXPECT_TRUE((*component_nh_it)->label() == 20);

    component_nh_it++;
    tun_nh1 = static_cast<const TunnelNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip3);
    EXPECT_TRUE((*component_nh_it)->label() == 25);

    //Enqueue route change in different order
    comp_nh_list.clear();
    comp_nh_list.push_back(nh_data2);
    comp_nh_list.push_back(nh_data3);
    comp_nh_list.push_back(nh_data1);

    EcmpTunnelRouteAdd(bgp_peer, "vrf2", remote_vm_ip, 32,
                       comp_nh_list, -1, "vn2", sg_list,
                       TagList(), PathPreference());
    client->WaitForIdle();
    rt = RouteGet("vrf2", remote_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);
    nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);

    comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 3);
    EXPECT_TRUE(comp_nh->PolicyEnabled() == false);

    //Verify all the component NH have right label and nexthop
    component_nh_it = comp_nh->begin();
    tun_nh1 = static_cast<const TunnelNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip1);
    EXPECT_TRUE((*component_nh_it)->label() == 15);

    component_nh_it++;
    tun_nh1 = static_cast<const TunnelNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip2);
    EXPECT_TRUE((*component_nh_it)->label() == 20);

    component_nh_it++;
    tun_nh1 = static_cast<const TunnelNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip3);
    EXPECT_TRUE((*component_nh_it)->label() == 25);

    comp_nh_list.clear();
    comp_nh_list.push_back(nh_data3);
    comp_nh_list.push_back(nh_data2);
    comp_nh_list.push_back(nh_data1);

    EcmpTunnelRouteAdd(bgp_peer, "vrf2", remote_vm_ip, 32,
                       comp_nh_list, -1, "vn2", sg_list,
                       TagList(), PathPreference());
    client->WaitForIdle();
    rt = RouteGet("vrf2", remote_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);
    nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);

    comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 3);
    EXPECT_TRUE(comp_nh->PolicyEnabled() == false);

    //Verify all the component NH have right label and nexthop
    component_nh_it = comp_nh->begin();
    tun_nh1 = static_cast<const TunnelNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip1);
    EXPECT_TRUE((*component_nh_it)->label() == 15);

    component_nh_it++;
    tun_nh1 = static_cast<const TunnelNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip2);
    EXPECT_TRUE((*component_nh_it)->label() == 20);

    component_nh_it++;
    tun_nh1 = static_cast<const TunnelNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip3);
    EXPECT_TRUE((*component_nh_it)->label() == 25);

    //cleanup
    DeleteRoute("vrf2", "1.1.1.1", 32, bgp_peer);
    client->WaitForIdle();
    //agent_->fabric_inet4_unicast_table()->DeleteReq(NULL, "vrf2",
    //                                                remote_vm_ip, 32, NULL);
    DelVrf("vrf2");
    WAIT_FOR(100, 1000, (VrfFind("vrf2") == false));
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf2"));
}

//Create a composite NH with one interface NH which is not present
//Add the interface, trigger route change and verify that component NH
//list get populated
TEST_F(EcmpNhTest, EcmpNH_16) {
    MacAddress mac1 = MacAddress::FromString("00:00:00:01:01:01");
    AddVrf("vrf2");
    client->WaitForIdle();
    Ip4Address remote_vm_ip = Ip4Address::from_string("1.1.1.1");
    //Transition remote VM route to ECMP route
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(15, MakeUuid(1),
                                                  InterfaceNHFlags::INET4,
                                                  mac1));
    ComponentNHKeyList comp_nh_list;
    //Insert new NH first and then existing route NH
    comp_nh_list.push_back(nh_data1);
    SecurityGroupList sg_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf2", remote_vm_ip, 32,
                       comp_nh_list, -1, "vn2", sg_list,
                       TagList(), PathPreference());
    client->WaitForIdle();

    //Nexthop is not found, hence component NH count is 0
    InetUnicastRouteEntry *rt = RouteGet("vrf2", remote_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);

    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 1);
    //Verify all the component NH have right label and nexthop
    ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    EXPECT_TRUE(*component_nh_it == NULL);

    //Add interface
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    client->NextHopReset();
    EcmpTunnelRouteAdd(bgp_peer, "vrf2", remote_vm_ip, 32,
            comp_nh_list, -1, "vn2", sg_list,
            TagList(), PathPreference());
    client->WaitForIdle();

    EXPECT_TRUE(client->CompositeNHWait(1));
    //Nexthop is not found, hence component NH count is 0
    rt = RouteGet("vrf2", remote_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);
    nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 1);
    component_nh_it = comp_nh->begin();
    EXPECT_TRUE(*component_nh_it != NULL);
    EXPECT_TRUE((*component_nh_it)->nh()->GetType() == NextHop::INTERFACE);

    DeleteVmportEnv(input, 1, true);
    DeleteRoute("vrf2", "1.1.1.1", 32, bgp_peer);
    client->WaitForIdle();
    DelVrf("vrf2");
    WAIT_FOR(100, 1000, (VrfFind("vrf2") == false));
    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf2"));
}

//Add a interface NH with policy
//Add a  BGP peer route with one interface NH and one tunnel NH
//make sure interface NH gets added without policy
TEST_F(EcmpNhTest, EcmpNH_17) {
    //Add interface
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    CreateVmportEnv(input, 1, 1);
    client->WaitForIdle();

    VnListType vn_list;
    vn_list.insert("vn1");
    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    agent_->fabric_inet4_unicast_table()->
            AddLocalVmRouteReq(agent_->local_peer(), "vrf1", ip, 32,
            MakeUuid(1), vn_list, intf->label(),
            SecurityGroupList(), TagList(),
            CommunityList(), false, PathPreference(),
            Ip4Address(0), EcmpLoadBalance(), false, false, false);
    client->WaitForIdle();

    InetUnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();

    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    //Create component NH list
    //Transition remote VM route to ECMP route
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(15, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
    std::auto_ptr<const NextHopKey> nh_akey(nh_key);
    nh_key->SetPolicy(false);
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(intf->label(), nh_akey));

    ComponentNHKeyList comp_nh_list;
    //Insert new NH first and then existing route NH
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);

    SecurityGroupList sg_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip, 32,
                       comp_nh_list, false, "vn1", sg_list,
                       TagList(), PathPreference());
    client->WaitForIdle();

    rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->PolicyEnabled() == false);
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(comp_nh->Get(1)->nh());
    EXPECT_TRUE(intf_nh->PolicyEnabled() == false);
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    //cleanup
    agent_->fabric_inet4_unicast_table()->DeleteReq(agent_->local_peer(), "vrf1",
                                                    ip, 32, NULL);
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DeleteRoute("vrf1", "1.1.1.1", 32, bgp_peer);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));
}
//Add multiple remote routes with same set of composite NH
//make sure they share the composite NH
//Add ecmp load balance fields to route entry.
//and check that more common feilds picked by  composite NEXTHop
TEST_F(EcmpNhTest, EcmpNH_18) {
    AddVrf("vrf1");
    client->WaitForIdle();

    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");

    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));

    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);

    Ip4Address ip1 = Ip4Address::from_string("100.1.1.1");
    SecurityGroupList sg_id_list;
    EcmpLoadBalance ecmp_load_balance;
    ecmp_load_balance.ResetAll();
    ecmp_load_balance.set_source_ip();
    ecmp_load_balance.set_destination_ip();
    ecmp_load_balance.set_source_port();

    autogen::EcmpHashingIncludeFields ecmp;
    ecmp.Clear();
    ecmp.hashing_configured= true;
    ecmp.source_ip = true;
    ecmp.destination_ip = true;
    ecmp.source_port = true;
    ecmp.destination_port = false;
    ecmp.ip_protocol = false;

    EcmpLoadBalance ecmp_load_balance2;
    ecmp_load_balance2.ResetAll();
    ecmp_load_balance2.UpdateFields(ecmp);
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip1, 32,
            comp_nh_list, false, "vn1", sg_id_list,
            TagList(), PathPreference(), ecmp_load_balance2);
    client->WaitForIdle();

    Ip4Address ip2 = Ip4Address::from_string("100.1.1.2");
    ecmp_load_balance.ResetAll();
    ecmp_load_balance.set_source_ip();
    ecmp_load_balance.set_destination_ip();
    ecmp_load_balance.set_ip_protocol();
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip2, 32,
            comp_nh_list, false, "vn1", sg_id_list,
            TagList(), PathPreference(), ecmp_load_balance);
    client->WaitForIdle();

    InetUnicastRouteEntry *rt1 = RouteGet("vrf1", ip1, 32);
    InetUnicastRouteEntry *rt2 = RouteGet("vrf1", ip2, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt1->GetActiveNextHop()->PolicyEnabled() == false);
    EXPECT_TRUE(rt1->GetActiveNextHop() == rt2->GetActiveNextHop());
    const CompositeNH *cnh = static_cast<const CompositeNH *>(rt1->GetActiveNextHop());

    EXPECT_TRUE(cnh->EcmpHashFieldInUse() ==
            (1<<EcmpLoadBalance::SOURCE_IP | 1<<EcmpLoadBalance::DESTINATION_IP));
    //Delete all the routes and make sure nexthop is also deleted
    DeleteRoute("vrf1", "100.1.1.1", 32, bgp_peer);
    client->WaitForIdle();
    DeleteRoute("vrf1", "100.1.1.2", 32, bgp_peer);
    client->WaitForIdle();

    CompositeNHKey composite_nh_key(Composite::ECMP, true, comp_nh_list, "vrf1");
    EXPECT_FALSE(FindNH(&composite_nh_key));
    WAIT_FOR(100, 1000, (FindNH(&composite_nh_key) ==false));
    DelVrf("vrf1");
}

// Same as EcmpNH_18 with UpdateFields call
TEST_F(EcmpNhTest, EcmpNH_UpdateFields) {
    AddVrf("vrf1");
    client->WaitForIdle();

    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");

    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));

    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);

    Ip4Address ip1 = Ip4Address::from_string("100.1.1.1");
    SecurityGroupList sg_id_list;

    autogen::EcmpHashingIncludeFields ecmp;
    ecmp.Clear();
    ecmp.hashing_configured= true;
    ecmp.source_ip = true;
    ecmp.destination_ip = true;
    ecmp.source_port = true;
    ecmp.destination_port = false;
    ecmp.ip_protocol = false;

    EcmpLoadBalance ecmp_load_balance;
    ecmp_load_balance.ResetAll();
    ecmp_load_balance.UpdateFields(ecmp);
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip1, 32,
            comp_nh_list, false, "vn1", sg_id_list,
            TagList(), PathPreference(), ecmp_load_balance);
    client->WaitForIdle();

    Ip4Address ip2 = Ip4Address::from_string("100.1.1.2");

    ecmp_load_balance.ResetAll();
    ecmp.Clear();
    ecmp.hashing_configured= true;
    ecmp.source_ip = true;
    ecmp.destination_ip = true;
    ecmp.source_port = false;
    ecmp.destination_port = false;
    ecmp.ip_protocol = true;
    ecmp_load_balance.UpdateFields(ecmp);

    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip2, 32,
            comp_nh_list, false, "vn1", sg_id_list,
            TagList(), PathPreference(), ecmp_load_balance);
    client->WaitForIdle();

    InetUnicastRouteEntry *rt1 = RouteGet("vrf1", ip1, 32);
    InetUnicastRouteEntry *rt2 = RouteGet("vrf1", ip2, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt1->GetActiveNextHop()->PolicyEnabled() == false);
    EXPECT_TRUE(rt1->GetActiveNextHop() == rt2->GetActiveNextHop());
    const CompositeNH *cnh = static_cast<const CompositeNH *>(rt1->GetActiveNextHop());

    EXPECT_TRUE(cnh->EcmpHashFieldInUse() ==
            (1<<EcmpLoadBalance::SOURCE_IP | 1<<EcmpLoadBalance::DESTINATION_IP));
    //Delete all the routes and make sure nexthop is also deleted
    DeleteRoute("vrf1", "100.1.1.1", 32, bgp_peer);
    client->WaitForIdle();
    DeleteRoute("vrf1", "100.1.1.2", 32, bgp_peer);
    client->WaitForIdle();

    CompositeNHKey composite_nh_key(Composite::ECMP, true, comp_nh_list, "vrf1");
    EXPECT_FALSE(FindNH(&composite_nh_key));
    WAIT_FOR(100, 1000, (FindNH(&composite_nh_key) ==false));
    DelVrf("vrf1");
}

//Add multiple remote routes with same set of composite NH
//make sure they share the composite NH
//Add ecmp load balance feilds to route entry.
//and check that more common feilds picked by  composite NEXTHop
//and delete the route entry and check that recompute happens
TEST_F(EcmpNhTest, EcmpNH_19) {
    AddVrf("vrf1");
    client->WaitForIdle();

    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");

    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));

    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);

    Ip4Address ip1 = Ip4Address::from_string("100.1.1.1");
    SecurityGroupList sg_id_list;
    EcmpLoadBalance ecmp_load_balance;
    ecmp_load_balance.ResetAll();
    ecmp_load_balance.set_source_ip();
    ecmp_load_balance.set_destination_ip();
    ecmp_load_balance.set_ip_protocol();
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip1, 32,
            comp_nh_list, false, "vn1", sg_id_list,
            TagList(), PathPreference(), ecmp_load_balance);
    client->WaitForIdle();

    Ip4Address ip2 = Ip4Address::from_string("100.1.1.2");
    ecmp_load_balance.ResetAll();
    ecmp_load_balance.set_source_ip();
    ecmp_load_balance.set_destination_ip();
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip2, 32,
            comp_nh_list, false, "vn1", sg_id_list,
            TagList(), PathPreference(), ecmp_load_balance);
    client->WaitForIdle();
    Ip4Address ip3 = Ip4Address::from_string("100.1.1.3");
    ecmp_load_balance.ResetAll();
    ecmp_load_balance.set_source_ip();
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip3, 32,
            comp_nh_list, false, "vn1", sg_id_list,
            TagList(), PathPreference(), ecmp_load_balance);
    client->WaitForIdle();
    InetUnicastRouteEntry *rt1 = RouteGet("vrf1", ip1, 32);
    InetUnicastRouteEntry *rt2 = RouteGet("vrf1", ip2, 32);
    InetUnicastRouteEntry *rt3 = RouteGet("vrf1", ip3, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt1->GetActiveNextHop()->PolicyEnabled() == false);
    EXPECT_TRUE(rt1->GetActiveNextHop() == rt2->GetActiveNextHop());
    EXPECT_TRUE(rt1->GetActiveNextHop() == rt3->GetActiveNextHop());
    const CompositeNH *cnh = static_cast<const CompositeNH *>(rt1->GetActiveNextHop());
    EXPECT_TRUE(cnh->EcmpHashFieldInUse() == (1<<EcmpLoadBalance::SOURCE_IP));
    client->WaitForIdle();
    DeleteRoute("vrf1", "100.1.1.3", 32, bgp_peer);
    client->WaitForIdle();
    const CompositeNH *cnh1 = static_cast<const CompositeNH *>(rt1->GetActiveNextHop());
    EXPECT_TRUE(cnh1->EcmpHashFieldInUse() ==
            (1<<EcmpLoadBalance::SOURCE_IP | 1<<EcmpLoadBalance::DESTINATION_IP));
    //Delete all the routes and make sure nexthop is also deleted
    DeleteRoute("vrf1", "100.1.1.2", 32, bgp_peer);
    client->WaitForIdle();
    const CompositeNH *cnh2 = static_cast<const CompositeNH *>(rt1->GetActiveNextHop());
    EXPECT_TRUE(cnh2->EcmpHashFieldInUse() ==
            (1<<EcmpLoadBalance::IP_PROTOCOL | 1<<EcmpLoadBalance::SOURCE_IP |
             1<<EcmpLoadBalance::DESTINATION_IP));

    DeleteRoute("vrf1", "100.1.1.1", 32, bgp_peer);
    client->WaitForIdle();

    CompositeNHKey composite_nh_key(Composite::ECMP, true, comp_nh_list, "vrf1");
    EXPECT_FALSE(FindNH(&composite_nh_key));
    DelVrf("vrf1");
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
