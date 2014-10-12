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
        peer_ = new BgpPeer(IpAddress::from_string("127.0.0.1").to_v4(),
                            "dummy", NULL, 1);
    }

    virtual void TearDown() {
        WAIT_FOR(1000, 10000, (agent_->vn_table()->Size() == 0));
        WAIT_FOR(1000, 10000, (agent_->vrf_table()->Size() == 1));
        delete peer_;
    }
    Agent *agent_;
    Peer *peer_;
};

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

void DoMulticastSandesh(int vrf_id) {
    Inet4McRouteReq *mc_list_req = new Inet4McRouteReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    mc_list_req->set_vrf_index(vrf_id);
    mc_list_req->HandleRequest();
    client->WaitForIdle();
    mc_list_req->Release();
    client->WaitForIdle();

    NhListReq *nh_req = new NhListReq();
    std::vector<int> nh_result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, nh_result));
    nh_req->HandleRequest();
    client->WaitForIdle();
    nh_req->Release();
    client->WaitForIdle();
}

void WaitForRouteDelete(Ip4Address grp, std::string vrf_name, bool mcast)
{
    client->WaitForIdle();
    if (mcast)
        WAIT_FOR(1000, 1000, (MCRouteGet(vrf_name, grp) == NULL));
    else
        WAIT_FOR(1000, 1000, (RouteGet(vrf_name, grp, 32) == NULL));
}

TEST_F(MulticastTest, Mcast_basic) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    //Sandesh request for unknown vrf
    DoMulticastSandesh(2);
    client->WaitForIdle();

    client->Reset();
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
	WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));
    WAIT_FOR(1000, 1000, (MCRouteFind("vrf1", "255.255.255.255") == true));
    client->Reset();

    //Sandesh request
    DoMulticastSandesh(1);
    //key manipulation
    Inet4MulticastRouteEntry *mc_rt = 
        MCRouteGet("vrf1", IpAddress::from_string("255.255.255.255").to_v4());
    EXPECT_TRUE(mc_rt != NULL);
    Inet4MulticastRouteKey key("vrf1",
                               IpAddress::from_string("255.255.255.255").to_v4(),
                               IpAddress::from_string("1.1.1.1").to_v4());
    mc_rt->SetKey(&key);
    EXPECT_TRUE(mc_rt->src_ip_addr() == IpAddress::from_string("1.1.1.1").to_v4());

    //Restore old src ip
    Inet4MulticastRouteKey old_key("vrf1",
                               IpAddress::from_string("255.255.255.255").to_v4(),
                               IpAddress::from_string("0.0.0.0").to_v4());
    mc_rt->SetKey(&old_key);
    EXPECT_TRUE(mc_rt->src_ip_addr() == IpAddress::from_string("0.0.0.0").to_v4());

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (MCRouteFind("vrf1", "255.255.255.255") != true));
    WAIT_FOR(1000, 1000, (VrfFind("vrf1") == false));
}

TEST_F(MulticastTest, McastSubnet_1) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 2},
        {"vnet3", 3, "3.3.3.1", "00:00:00:03:03:03", 1, 3},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
        {"2.2.2.0", 24, "2.2.2.200", true},
        {"3.3.3.0", 16, "3.3.30.200", true},
    };

    IpamInfo ipam_updated_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
        {"2.2.2.0", 24, "2.2.2.200", true},
        {"3.3.3.0", 16, "3.3.30.200", true},
        {"4.0.0.0", 8, "4.4.40.200", true},
    };

    IpamInfo ipam_updated_info_2[] = {
        {"2.2.2.0", 24, "2.2.2.200", true},
        {"3.3.3.0", 16, "3.3.30.200", true},
    };
    char buf[BUF_SIZE];
    int len = 0;

    client->Reset();
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 3, 0);
    client->WaitForIdle();

    client->Reset();

	InetUnicastRouteEntry *rt;
    NextHop *nh;
    CompositeNH *cnh;
    MulticastGroupObject *mcobj;

    AddIPAM("vn1", ipam_info, 3);
    client->WaitForIdle();

	EXPECT_TRUE(VmPortActive(input, 0));
	EXPECT_TRUE(VmPortActive(input, 1));
	EXPECT_TRUE(VmPortActive(input, 2));

    WAIT_FOR(1000, 1000, RouteFind("vrf1", "1.1.1.255", 32));
    EXPECT_TRUE(RouteFind("vrf1", "1.1.1.255", 32));
    EXPECT_TRUE(RouteFind("vrf1", "3.3.255.255", 32));

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000, 
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("1.1.1.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("255.255.255.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1112, olist_map);
    AddArp("8.8.8.8", "00:00:08:08:08:08", agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    //Verify sandesh
    DoMulticastSandesh(1);

    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    rt = RouteGet("vrf1", addr, 32);
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    cnh = static_cast<CompositeNH *>(nh);
    EXPECT_TRUE(cnh->GetType() == NextHop::COMPOSITE);
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1",
                                              addr);
	MplsLabel *mpls = 
	    agent_->mpls_table()->FindMplsLabel(1111);
	ASSERT_TRUE(mpls == NULL);
    ASSERT_TRUE((mcobj->GetLocalOlist()).size() == 2);

    TunnelOlist olist_map1;
    olist_map1.push_back(OlistTunnelEntry(7777, 
                                          IpAddress::from_string("8.8.8.8").to_v4(),
                                          TunnelType::AllType()));
    olist_map1.push_back(OlistTunnelEntry(9999, 
                                          IpAddress::from_string("8.8.8.8").to_v4(),
                                          TunnelType::AllType()));
    olist_map1.push_back(OlistTunnelEntry(8888, 
                                          IpAddress::from_string("8.8.8.8").to_v4(),
                                          TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("3.3.255.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          2222, olist_map1);

    addr = Ip4Address::from_string("3.3.255.255");
    rt = RouteGet("vrf1", addr, 32);
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    cnh = static_cast<CompositeNH *>(nh);
    ASSERT_TRUE(nh != NULL);
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
	mpls = agent_->mpls_table()->FindMplsLabel(2222);
	ASSERT_TRUE(mpls == NULL);
    ASSERT_TRUE((mcobj->GetLocalOlist()).size() == 1);

    client->WaitForIdle();
    TunnelOlist olist_map2;
    olist_map2.push_back(OlistTunnelEntry(8888, 
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    olist_map2.push_back(OlistTunnelEntry(5555, 
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("3.3.255.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          2222, olist_map2);
    client->WaitForIdle();
    DoMulticastSandesh(1);

    addr = Ip4Address::from_string("3.3.255.255");
    rt = RouteGet("vrf1", addr, 32);
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
	mpls = agent_->mpls_table()->FindMplsLabel(2222);
	ASSERT_TRUE(mpls == NULL);
    ASSERT_TRUE((mcobj->GetLocalOlist()).size() == 1);

    memset(buf, 0, BUF_SIZE);
    AddXmlHdr(buf, len);
    AddNodeString(buf, len, "virtual-network-network-ipam", 
                  "default-network-ipam,vn1", ipam_updated_info, 4);
    AddXmlTail(buf, len);
    ApplyXmlString(buf);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, RouteGet("vrf1", IpAddress::from_string("4.255.255.255").to_v4(), 32));
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    DelArp("8.8.8.8", "00:00:08:08:08:08", 
           agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);
    client->WaitForIdle();

    memset(buf, 0, BUF_SIZE);
    AddXmlHdr(buf, len);
    AddNodeString(buf, len, "virtual-network-network-ipam",
                  "default-network-ipam,vn1", ipam_updated_info_2, 2);
    AddXmlTail(buf, len);
    ApplyXmlString(buf);
    client->WaitForIdle();

    WaitForRouteDelete(IpAddress::from_string("1.1.1.255").to_v4(), "vrf1",
                       false);
    WaitForRouteDelete(IpAddress::from_string("4.255.255.255").to_v4(), "vrf1",
                       false);

    client->Reset();
    DelIPAM("vn1"); 
    client->WaitForIdle();


    client->Reset();
    DeleteVmportEnv(input, 3, 1, 0);
    client->WaitForIdle();

    WaitForRouteDelete(IpAddress::from_string("1.1.1.255").to_v4(), "vrf1",
                       false);
    WaitForRouteDelete(IpAddress::from_string("4.255.255.255").to_v4(), "vrf1",
                       false);
    WaitForRouteDelete(IpAddress::from_string("2.2.2.255").to_v4(), "vrf1",
                       false);
    WaitForRouteDelete(IpAddress::from_string("3.3.255.255").to_v4(), "vrf1",
                       false);
    WaitForRouteDelete(IpAddress::from_string("255.255.255.255").to_v4(), "vrf1",
                       true);
    client->WaitForIdle();
}

TEST_F(MulticastTest, L2Broadcast_1) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 2},
        {"vnet3", 3, "3.3.3.1", "00:00:00:03:03:03", 1, 3},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
        {"2.2.2.0", 24, "2.2.2.200", true},
        {"3.3.3.0", 16, "3.3.30.200", true},
    };

    client->Reset();
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 3, 0);
    client->WaitForIdle();

    client->Reset();
    AddIPAM("vn1", ipam_info, 3);
    client->WaitForIdle();

    client->Reset();

    NextHop *nh;
    CompositeNH *cnh;
    MulticastGroupObject *mcobj;

	EXPECT_TRUE(VmPortActive(input, 0));
	EXPECT_TRUE(VmPortActive(input, 1));
	EXPECT_TRUE(VmPortActive(input, 2));

    WAIT_FOR(1000, 1000, MCRouteFind("vrf1", "255.255.255.255"));
    EXPECT_TRUE(L2RouteFind("vrf1", MacAddress::BroadcastMac()));

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000,
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("255.255.255.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    AddArp("8.8.8.8", "00:00:08:08:08:08",
           agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    Ip4Address addr = Ip4Address::from_string("255.255.255.255");
    Inet4MulticastRouteEntry *rt =
        MCRouteGet("vrf1", "255.255.255.255");
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    cnh = static_cast<CompositeNH *>(nh);
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
    WAIT_FOR(1000, 1000, FindMplsLabel(MplsLabel::MCAST_NH, 1111));
	MplsLabel *mpls = GetActiveLabel(MplsLabel::MCAST_NH, 1111);
    ASSERT_TRUE((mcobj->GetLocalOlist()).size() == 3);
    EXPECT_TRUE(cnh->ComponentNHCount() == 2);
    EXPECT_TRUE(cnh->composite_nh_type() == Composite::L3COMP);
    DoMulticastSandesh(1);

    Layer2RouteEntry *l2_rt = L2RouteGet("vrf1", MacAddress("FF:FF:FF:FF:FF:FF"));
    NextHop *l2_nh = const_cast<NextHop *>(l2_rt->GetActiveNextHop());
    CompositeNH *l2_cnh = static_cast<CompositeNH *>(l2_nh);
    EXPECT_TRUE(l2_cnh->ComponentNHCount() == 2);
    EXPECT_TRUE(l2_cnh->composite_nh_type() == Composite::L2COMP);

    EXPECT_TRUE(cnh->GetNH(0) == l2_cnh->GetNH(0));
    for (uint8_t i = 1; i < cnh->ComponentNHCount(); i++) {
        const InterfaceNH *l3_comp_nh = 
            static_cast<const InterfaceNH *>(cnh->Get(i)->nh());
        const InterfaceNH *l2_comp_nh = 
            static_cast<const InterfaceNH *>(l2_cnh->Get(i)->nh());
        EXPECT_TRUE(l3_comp_nh->GetFlags() == 
                    (InterfaceNHFlags::MULTICAST |
                     InterfaceNHFlags::INET4));
        EXPECT_TRUE(l2_comp_nh->GetFlags() == InterfaceNHFlags::LAYER2);
        EXPECT_TRUE(l3_comp_nh->GetIfUuid() == l2_comp_nh->GetIfUuid());
    }

    const CompositeNH *fabric_cnh =
        static_cast<const CompositeNH *>(cnh->Get(0)->nh());
    EXPECT_TRUE(fabric_cnh->composite_nh_type() == Composite::FABRIC);
    EXPECT_TRUE(fabric_cnh->ComponentNHCount() == 1);
    EXPECT_TRUE(fabric_cnh->Get(0)->label() == 2000);

    const CompositeNH *l3_intf_cnh = static_cast<const CompositeNH *>(cnh->GetNH(1));
    EXPECT_TRUE(l3_intf_cnh->composite_nh_type() == Composite::L3INTERFACE);
    EXPECT_TRUE(l3_intf_cnh->ComponentNHCount() == 3);

    const CompositeNH *l2_intf_cnh = static_cast<const CompositeNH *>(l2_cnh->GetNH(2));
    EXPECT_TRUE(l2_intf_cnh->composite_nh_type() == Composite::L2INTERFACE);
    EXPECT_TRUE(l2_intf_cnh->ComponentNHCount() == 3);

    for (uint8_t i = 0; i < l3_intf_cnh->ComponentNHCount(); i++) {
        const InterfaceNH *l3_intf_nh =
            static_cast<const InterfaceNH *>(l3_intf_cnh->GetNH(i));
        const InterfaceNH *l2_intf_nh =
            static_cast<const InterfaceNH *>(l2_intf_cnh->GetNH(i));
        EXPECT_TRUE(l3_intf_nh->GetFlags() ==
                    (InterfaceNHFlags::MULTICAST | InterfaceNHFlags::INET4));
        EXPECT_TRUE(l2_intf_nh->GetFlags() == InterfaceNHFlags::LAYER2);
        EXPECT_TRUE(l3_intf_nh->GetIfUuid() == l2_intf_nh->GetIfUuid());
    }

    const CompositeNH *mpls_cnh = 
        static_cast<const CompositeNH *>(mpls->nexthop());
    EXPECT_TRUE(mpls_cnh->composite_nh_type() == Composite::MULTIPROTO);
    EXPECT_TRUE(mpls_cnh->ComponentNHCount() == 2);
    EXPECT_TRUE(mpls_cnh->Get(0)->nh() == cnh);
    EXPECT_TRUE(mpls_cnh->Get(1)->nh() == l2_cnh);
    DoMulticastSandesh(1);
    DoMulticastSandesh(2);
    DoMulticastSandesh(3);

    DelArp("8.8.8.8", "00:00:08:08:08:08", 
           agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    client->WaitForIdle();
    IntfCfgDel(input, 1);
    client->WaitForIdle();
    IntfCfgDel(input, 2);
    client->WaitForIdle();

    client->Reset();
    DelIPAM("vn1"); 
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 3, 1, 0);
    client->WaitForIdle();

    WaitForRouteDelete(IpAddress::from_string("255.255.255.255").to_v4(), "vrf1",
                       true);
    client->WaitForIdle();
}

TEST_F(MulticastTest, McastSubnet_DeleteRouteOnVRFDeleteofVN) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    client->Reset();
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    client->Reset();

    InetUnicastRouteEntry *rt;
    const NextHop *nh;
    const CompositeNH *cnh;
    MulticastGroupObject *mcobj;

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

	EXPECT_TRUE(VmPortActive(input, 0));

    WAIT_FOR(1000, 1000, RouteFind("vrf1", "1.1.1.255", 32));
    EXPECT_TRUE(RouteFind("vrf1", "1.1.1.255", 32));
    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    rt = RouteGet("vrf1", addr, 32);
    nh = rt->GetActiveNextHop();
    cnh = static_cast<const CompositeNH *>(nh);
    WAIT_FOR(1000, 1000, (cnh->ComponentNHCount() != 0));

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000,
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("1.1.1.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    AddArp("8.8.8.8", "00:00:08:08:08:08", agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    rt = RouteGet("vrf1", addr, 32);
    nh = rt->GetActiveNextHop();
    cnh = static_cast<const CompositeNH *>(nh);
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
	MplsLabel *mpls = 
	    agent_->mpls_table()->FindMplsLabel(1111);
	ASSERT_TRUE(mpls == NULL);
    ASSERT_TRUE((mcobj->GetLocalOlist()).size() == 1);

    DelArp("8.8.8.8", "00:00:08:08:08:08", 
           agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, !RouteGet("vrf1",
                                   IpAddress::from_string("1.1.1.255").to_v4(),
                                   32));
    client->WaitForIdle();
    WaitForRouteDelete(IpAddress::from_string("255.255.255.255").to_v4(), "vrf1",
                       true);
}

TEST_F(MulticastTest, McastSubnet_DeleteRouteOnIPAMDeleteofVN) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    client->Reset();
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    client->Reset();

	InetUnicastRouteEntry *rt;
    NextHop *nh;
    CompositeNH *cnh;
    MulticastGroupObject *mcobj;

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

	WAIT_FOR(1000, 1000, (VmPortActive(input, 0)));
    WAIT_FOR(1000, 1000, RouteFind("vrf1", "1.1.1.255", 32));
    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000, 
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("1.1.1.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    AddArp("8.8.8.8", "00:00:08:08:08:08", agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    rt = RouteGet("vrf1", addr, 32);
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    cnh = static_cast<CompositeNH *>(nh);
    EXPECT_TRUE(cnh->GetType() == NextHop::COMPOSITE);
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
	MplsLabel *mpls = 
	    agent_->mpls_table()->FindMplsLabel(1111);
	ASSERT_TRUE(mpls == NULL);
    ASSERT_TRUE((mcobj->GetLocalOlist()).size() == 1);

    DelLink("virtual-network", "vn1", "virtual-network-network-ipam", 
            "default-network-ipam,vn1");
    client->WaitForIdle();

    DelArp("8.8.8.8", "00:00:08:08:08:08", 
           agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, !RouteGet("vrf1", IpAddress::from_string("1.1.1.255").to_v4(), 32));
    client->WaitForIdle();
}

TEST_F(MulticastTest, McastSubnet_DeleteCompNHThenModifyFabricList) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    client->Reset();
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    client->Reset();

	InetUnicastRouteEntry *rt;
    NextHop *nh;
    //CompositeNH *cnh;
    //MulticastGroupObject *mcobj;

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

	EXPECT_TRUE(VmPortActive(input, 0));

    WAIT_FOR(1000, 1000, RouteFind("vrf1", "1.1.1.255", 32));
    EXPECT_TRUE(RouteFind("vrf1", "1.1.1.255", 32));

    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    rt = RouteGet("vrf1", addr, 32);
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    EXPECT_TRUE(nh != NULL);
    
    DBRequest req;
    NextHopKey *key = static_cast<NextHopKey *>(nh->GetDBRequestKey().release());
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(key);
    req.data.reset(NULL);
    agent_->nexthop_table()->Enqueue(&req);

    MulticastGroupObject *mcobj;
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1",
                                              IpAddress::from_string("1.1.1.255").to_v4());
    EXPECT_FALSE(mcobj == NULL);
    mcobj->Deleted(true);

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000, 
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("1.1.1.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    AddArp("8.8.8.8", "00:00:08:08:08:08", agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    client->Reset();
    DelArp("8.8.8.8", "00:00:08:08:08:08", 
           agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    DelIPAM("vn1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, !RouteGet("vrf1", IpAddress::from_string("1.1.1.255").to_v4(), 32));
    client->WaitForIdle();
}

TEST_F(MulticastTest, McastSubnet_SubnetIPAMAddDel) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    IpamInfo ipam_info[] = {
        {"10.1.1.0", 30, "10.1.1.2", true},
        {"11.1.1.0", 30, "11.1.1.2", true},
    };

    IpamInfo ipam_info_2[] = {
        {"10.1.1.0", 30, "10.1.1.2", true},
        {"10.1.1.10", 29, "10.1.1.14", true},
        {"11.1.1.0", 30, "11.1.1.2", true},
    };

    client->Reset();
	EXPECT_TRUE(VmPortActive(input, 0));

    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, RouteFind("vrf1", "11.1.1.3", 32));
    WAIT_FOR(1000, 1000, RouteFind("vrf1", "10.1.1.2", 32));

    AddIPAM("vn1", ipam_info_2, 3);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, RouteFind("vrf1", "10.1.1.14", 32));
    EXPECT_TRUE(RouteFind("vrf1", "11.1.1.3", 32));
    EXPECT_TRUE(RouteFind("vrf1", "10.1.1.2", 32));

    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, !RouteFind("vrf1", "10.1.1.14", 32));
    EXPECT_TRUE(RouteFind("vrf1", "11.1.1.3", 32));
    EXPECT_TRUE(RouteFind("vrf1", "10.1.1.2", 32));

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, !RouteFind("vrf1", "11.1.1.3", 32));
    client->WaitForIdle();
}

#if 0
TEST_F(MulticastTest, McastSubnet_interfaceadd_after_ipam_delete) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "11.1.1.3", "00:00:00:03:03:03", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"11.1.1.0", 24, "11.1.1.200", true},
    };

    const char *vrf_name = "domain:vn1:vrf1";

    AddVn("vn1", 1);
    AddVrf(vrf_name, 2);
    AddIPAM("vn1", ipam_info, 3);
    AddLink("virtual-network", "vn1", "routing-instance", vrf_name);
    client->WaitForIdle();
    client->Reset();

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
	EXPECT_TRUE(VmPortActive(input, 0));

    WAIT_FOR(1000, 1000, RouteFind("vrf1", "11.1.1.255", 32));

    DelIPAM("vn1");
    client->Reset();
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, !RouteFind("vrf1", "11.1.1.255", 32));

    CreateVmportEnv(input, 2, 0);
    client->WaitForIdle();
	EXPECT_TRUE(VmPortActive(input, 1));

    CompositeNHKey key("vrf1", IpAddress::from_string("11.1.1.255").to_v4(),
                       IpAddress::from_string("0.0.0.0").to_v4(), false);
    EXPECT_FALSE(FindNH(&key));

    DelLink("virtual-network", "vn1", "routing-instance", vrf_name);
    DelVrf(vrf_name);
    client->Reset();

    DeleteVmportEnv(input, 2, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, !RouteFind("vrf1", "11.1.1.3", 32));
}
#endif

TEST_F(MulticastTest, McastSubnet_VN2MultipleVRFtest_ignore_unknown_vrf) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"10.1.1.0", 30, "10.1.1.2", true},
        {"10.1.1.10", 29, "10.1.1.14", true},
        {"11.1.1.0", 30, "11.1.1.2", true},
    };

    const char *vrf_name = "domain:vn1:vrf1";

    AddVn("vn1", 1);
    AddVrf(vrf_name, 2);
    AddIPAM("vn1", ipam_info, 3);
    AddLink("virtual-network", "vn1", "routing-instance", vrf_name);
    client->WaitForIdle();
    client->Reset();

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    client->Reset();
	EXPECT_TRUE(VmPortActive(input, 0));

    WAIT_FOR(1000, 1000, RouteFind("vrf1", "11.1.1.3", 32));
    EXPECT_TRUE(RouteFind("vrf1", "11.1.1.3", 32));
    EXPECT_TRUE(RouteFind("vrf1", "10.1.1.2", 32));

    DelIPAM("vn1");
    client->Reset();
    client->WaitForIdle();
    DelLink("virtual-network", "vn1", "routing-instance", vrf_name);
    DelVrf(vrf_name);
    client->Reset();

    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, !RouteFind("vrf1", "11.1.1.3", 32));
    EXPECT_FALSE(RouteFind("vrf1", "10.1.1.2", 32));

    client->WaitForIdle();
}

TEST_F(MulticastTest, subnet_bcast_ipv4_vn_delete) {
    //Send control node message on subnet bcast after family has changed to L2
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:11", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"10.1.1.0", 24, "10.1.1.100", true},
        {"11.1.1.0", 24, "11.1.1.100", true},
    };

    EXPECT_FALSE(VrfFind("vrf1"));
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

	WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));

    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (RouteFind("vrf1", "11.1.1.255", 32)));

    //Change the vn forwarding mode to l2-only
    MulticastGroupObject *mcobj = MulticastHandler::GetInstance()->
        FindGroupObject("vrf1", IpAddress::from_string("11.1.1.255").to_v4());
    EXPECT_TRUE(mcobj->layer2_forwarding() == false);
    //Del VN
    VnDelReq(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (!RouteFind("vrf1", "11.1.1.255", 32)));

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000,
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::MplsType()));
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("11.1.1.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    client->WaitForIdle();

    //Restore and cleanup
    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VrfFind("vrf1") == false));
}

TEST_F(MulticastTest, subnet_bcast_ipv4_vn_ipam_change) {
    //Send control node message on subnet bcast after family has changed to L2
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:11", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"10.1.1.0", 24, "10.1.1.100", true},
        {"11.1.1.0", 24, "11.1.1.100", true},
    };

    IpamInfo ipam_info_2[] = {
        {"10.1.1.0", 24, "10.1.1.100", true},
        {"12.1.1.0", 24, "12.1.1.100", true},
    };

    EXPECT_FALSE(VrfFind("vrf1"));
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

	WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));

    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (RouteFind("vrf1", "11.1.1.255", 32)));

    //Change the vn forwarding mode to l2-only
    MulticastGroupObject *mcobj = MulticastHandler::GetInstance()->
        FindGroupObject("vrf1", IpAddress::from_string("11.1.1.255").to_v4());
    EXPECT_TRUE(mcobj->layer2_forwarding() == false);
    //Change IPAM
    AddIPAM("vn1", ipam_info_2, 2);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (!RouteFind("vrf1", "11.1.1.255", 32)));

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000,
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::MplsType()));
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("11.1.1.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    client->WaitForIdle();

    //Restore and cleanup
    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VrfFind("vrf1") == false));
}

TEST_F(MulticastTest, subnet_bcast_ipv4_vn_vrf_link_delete) {
    //Send control node message on subnet bcast after family has changed to L2
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:11", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"10.1.1.0", 24, "10.1.1.100", true},
        {"11.1.1.0", 24, "11.1.1.100", true},
    };

    EXPECT_FALSE(VrfFind("vrf1"));
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

	WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));

    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (RouteFind("vrf1", "11.1.1.255", 32)));

    //Change the vn forwarding mode to l2-only
    MulticastGroupObject *mcobj = MulticastHandler::GetInstance()->
        FindGroupObject("vrf1", IpAddress::from_string("11.1.1.255").to_v4());
    EXPECT_TRUE(mcobj->layer2_forwarding() == false);
    //VRF delete for VN
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (!RouteFind("vrf1", "11.1.1.255", 32)));

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000,
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::MplsType()));
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("11.1.1.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    client->WaitForIdle();

    //Restore and cleanup
    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VrfFind("vrf1") == false));
}

TEST_F(MulticastTest, subnet_bcast_add_l2l3vn_and_l2vn) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:11", 1, 1},
    };

    struct PortInfo input_2[] = {
        {"vnet2", 2, "11.1.1.3", "00:00:00:01:01:11", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"10.1.1.0", 24, "10.1.1.100", true},
        {"11.1.1.0", 24, "11.1.1.100", true},
    };

    EXPECT_FALSE(VrfFind("vrf1"));
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

	WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));

    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (RouteFind("vrf1", "11.1.1.255", 32)));

    MulticastGroupObject *mcobj = MulticastHandler::GetInstance()->
        FindGroupObject("vrf1", IpAddress::from_string("11.1.1.255").to_v4());
    EXPECT_TRUE(mcobj->layer2_forwarding() == false);
    client->WaitForIdle();

    AddL2Vn("vn2", 2);
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("vrf1", "11.1.1.255", 32));

    AddVrf("vrf2");
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("vrf1", "11.1.1.255", 32));

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000,
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::MplsType()));
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("11.1.1.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    client->WaitForIdle();

    //Restore and cleanup
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
    DelVn("vn2");
    DelVrf("vrf2");
    WAIT_FOR(1000, 1000, (VrfFind("vrf1") == false));
}

TEST_F(MulticastTest, evpn_flood_l2l3_mode) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 2},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
    };

    client->Reset();
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 2, 0);
    client->WaitForIdle();

    client->Reset();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    client->Reset();

    NextHop *nh;
    CompositeNH *cnh;
    MulticastGroupObject *mcobj;

	WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));
	WAIT_FOR(1000, 1000, (VmPortActive(input, 1) == true));

    WAIT_FOR(1000, 1000, MCRouteFind("vrf1", "255.255.255.255"));
    EXPECT_TRUE(L2RouteFind("vrf1", MacAddress::BroadcastMac()));

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000,
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("255.255.255.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    client->WaitForIdle();

    AddArp("8.8.8.8", "00:00:08:08:08:08",
           Agent::GetInstance()->fabric_interface_name().c_str());
    client->WaitForIdle();

    TunnelOlist evpn_olist_map;
    evpn_olist_map.push_back(OlistTunnelEntry(1000,
                                              IpAddress::from_string("8.8.8.8").to_v4(),
                                              TunnelType::MplsType()));
    MulticastHandler::ModifyEvpnMembers(peer_, "vrf1",
                                        evpn_olist_map, 0);
    client->WaitForIdle();

    MulticastHandler::ModifyEvpnMembers(peer_, "vrf1",
                                        evpn_olist_map, 0);
    client->WaitForIdle();

    Inet4MulticastRouteEntry *rt =
        MCRouteGet("vrf1", "255.255.255.255");
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    cnh = static_cast<CompositeNH *>(nh);
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1",
                     IpAddress::from_string("255.255.255.255").to_v4());
    uint32_t evpn_flood_label = mcobj->evpn_mpls_label();
    ASSERT_TRUE(evpn_flood_label != 0);
    ASSERT_TRUE((mcobj->GetLocalOlist()).size() == 2);

    EXPECT_TRUE(cnh->ComponentNHCount() == 2);
    EXPECT_TRUE(cnh->composite_nh_type() == Composite::L3COMP);

    DoMulticastSandesh(1);

    Layer2RouteEntry *l2_rt = L2RouteGet("vrf1", MacAddress::BroadcastMac());
    NextHop *l2_nh = const_cast<NextHop *>(l2_rt->GetActiveNextHop());
    CompositeNH *l2_cnh = static_cast<CompositeNH *>(l2_nh);
    EXPECT_TRUE(l2_cnh->ComponentNHCount() == 3);
    EXPECT_TRUE(l2_cnh->composite_nh_type() == Composite::L2COMP);

    EXPECT_TRUE(cnh->GetNH(0) == l2_cnh->GetNH(0));
    const CompositeNH *fabric_cnh = static_cast<const CompositeNH *>(cnh->GetNH(0));
    EXPECT_TRUE(fabric_cnh->composite_nh_type() == Composite::FABRIC);
    EXPECT_TRUE(fabric_cnh->ComponentNHCount() == 1);
    EXPECT_TRUE(fabric_cnh->Get(0)->label() == 2000);

    const CompositeNH *evpn_cnh = static_cast<const CompositeNH *>(l2_cnh->GetNH(1));
    EXPECT_TRUE(evpn_cnh->composite_nh_type() == Composite::EVPN);
    EXPECT_TRUE(evpn_cnh->ComponentNHCount() == 1);
    EXPECT_TRUE(evpn_cnh->Get(0)->label() == 1000);
    const TunnelNH *evpn_tunnel_nh =
        static_cast<const TunnelNH *>(evpn_cnh->GetNH(0));
    EXPECT_TRUE(evpn_tunnel_nh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);
    EXPECT_TRUE(evpn_tunnel_nh->GetDip()->to_string() ==
                IpAddress::from_string("8.8.8.8").to_v4().to_string());

    const CompositeNH *l3_intf_cnh = static_cast<const CompositeNH *>(cnh->GetNH(1));
    EXPECT_TRUE(l3_intf_cnh->composite_nh_type() == Composite::L3INTERFACE);
    EXPECT_TRUE(l3_intf_cnh->ComponentNHCount() == 2);

    const CompositeNH *l2_intf_cnh = static_cast<const CompositeNH *>(l2_cnh->GetNH(2));
    EXPECT_TRUE(l2_intf_cnh->composite_nh_type() == Composite::L2INTERFACE);
    EXPECT_TRUE(l2_intf_cnh->ComponentNHCount() == 2);

    for (uint8_t i = 0; i < l3_intf_cnh->ComponentNHCount(); i++) {
        const InterfaceNH *l3_intf_nh = 
            static_cast<const InterfaceNH *>(l3_intf_cnh->GetNH(i));
        const InterfaceNH *l2_intf_nh = 
            static_cast<const InterfaceNH *>(l2_intf_cnh->GetNH(i));
        EXPECT_TRUE(l3_intf_nh->GetFlags() == 
                    (InterfaceNHFlags::MULTICAST | InterfaceNHFlags::INET4));
        EXPECT_TRUE(l2_intf_nh->GetFlags() == InterfaceNHFlags::LAYER2);
        EXPECT_TRUE(l3_intf_nh->GetIfUuid() == l2_intf_nh->GetIfUuid());
    }

    //Verify EVPN mpls label NH points to l2_intf_cnh above
    MplsLabel *evpn_label = Agent::GetInstance()->mpls_table()->
        FindMplsLabel(evpn_flood_label);
    const NextHop *evpn_label_nh = evpn_label->nexthop();
    ASSERT_TRUE(evpn_label_nh == l2_cnh);

    DoMulticastSandesh(1);
    DoMulticastSandesh(2);
    DoMulticastSandesh(3);

    IntfCfgDel(input, 0);
    client->WaitForIdle();
    IntfCfgDel(input, 1);
    client->WaitForIdle();

    client->Reset();
    DelIPAM("vn1"); 
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 2, 1, 0);
    client->WaitForIdle();

    DelArp("8.8.8.8", "00:00:08:08:08:08", 
           agent_->fabric_interface_name().c_str());
    client->WaitForIdle();
    WaitForRouteDelete(IpAddress::from_string("255.255.255.255").to_v4(),
                       "vrf1", true);
    client->WaitForIdle();
}

TEST_F(MulticastTest, evpn_flood_l2_mode) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    AddL2Vn("vn1", 1);
    AddVrf("vrf1");
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    client->Reset();
    CreateL2VmportEnv(input, 1);
    client->WaitForIdle();

	WAIT_FOR(1000, 1000, (VmPortL2Active(input, 0) == true));
    WAIT_FOR(1000, 1000, L2RouteFind("vrf1", MacAddress::BroadcastMac()));
    EXPECT_FALSE(MCRouteFind("vrf1", "255.255.255.255"));

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000,
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers(agent_->multicast_tree_builder_peer(), "vrf1",
                                          IpAddress::from_string("255.255.255.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    client->WaitForIdle();

    AddArp("8.8.8.8", "00:00:08:08:08:08",
           Agent::GetInstance()->fabric_interface_name().c_str());
    client->WaitForIdle();

    TunnelOlist evpn_olist_map;
    evpn_olist_map.push_back(OlistTunnelEntry(1000,
                                              IpAddress::from_string("8.8.8.8").to_v4(),
                                              TunnelType::MplsType()));
    //Do it for non existent object, to catch any crashes
    MulticastHandler::ModifyEvpnMembers(peer_, "vrf1",
                                        evpn_olist_map, 0);
    client->WaitForIdle();

    MulticastHandler::ModifyEvpnMembers(peer_, "vrf1",
                                        evpn_olist_map, 0);
    client->WaitForIdle();

    MulticastGroupObject *mcobj =
        MulticastHandler::GetInstance()->FindFloodGroupObject("vrf1");

    uint32_t evpn_flood_label = mcobj->evpn_mpls_label();
    ASSERT_TRUE(evpn_flood_label != 0);
    ASSERT_TRUE((mcobj->GetLocalOlist()).size() == 1);

    Layer2RouteEntry *l2_rt = L2RouteGet("vrf1", MacAddress::BroadcastMac());
    NextHop *l2_nh = const_cast<NextHop *>(l2_rt->GetActiveNextHop());
    CompositeNH *l2_cnh = static_cast<CompositeNH *>(l2_nh);
    EXPECT_TRUE(l2_cnh->ComponentNHCount() == 3);
    EXPECT_TRUE(l2_cnh->composite_nh_type() == Composite::L2COMP);

    const CompositeNH *fabric_cnh = static_cast<const CompositeNH *>(l2_cnh->GetNH(0));
    EXPECT_TRUE(fabric_cnh->composite_nh_type() == Composite::FABRIC);
    EXPECT_TRUE(fabric_cnh->ComponentNHCount() == 1);
    EXPECT_TRUE(fabric_cnh->Get(0)->label() == 2000);

    const CompositeNH *evpn_cnh = static_cast<const CompositeNH *>(l2_cnh->GetNH(1));
    EXPECT_TRUE(evpn_cnh->composite_nh_type() == Composite::EVPN);
    EXPECT_TRUE(evpn_cnh->ComponentNHCount() == 1);
    EXPECT_TRUE(evpn_cnh->Get(0)->label() == 1000);
    const TunnelNH *evpn_tunnel_nh =
        static_cast<const TunnelNH *>(evpn_cnh->GetNH(0));
    EXPECT_TRUE(evpn_tunnel_nh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);
    EXPECT_TRUE(evpn_tunnel_nh->GetDip()->to_string() ==
                IpAddress::from_string("8.8.8.8").to_v4().to_string());

    const CompositeNH *l2_intf_cnh = static_cast<const CompositeNH *>(l2_cnh->GetNH(2));
    EXPECT_TRUE(l2_intf_cnh->composite_nh_type() == Composite::L2INTERFACE);
    EXPECT_TRUE(l2_intf_cnh->ComponentNHCount() == 1);

    //Verify EVPN mpls label NH points to l2_intf_cnh above
    MplsLabel *evpn_label = Agent::GetInstance()->mpls_table()->
        FindMplsLabel(evpn_flood_label);
    const NextHop *evpn_label_nh = evpn_label->nexthop();
    ASSERT_TRUE(evpn_label_nh == l2_cnh);

    DoMulticastSandesh(1);
    DoMulticastSandesh(2);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    DelArp("8.8.8.8", "00:00:08:08:08:08", 
           agent_->fabric_interface_name().c_str());
    client->WaitForIdle();
    WaitForRouteDelete(IpAddress::from_string("255.255.255.255").to_v4(),
                       "vrf1", true);
    EXPECT_FALSE(VmPortFind(input, 0));
    client->WaitForIdle();
}

TEST_F(MulticastTest, change_in_gateway_of_subnet_noop) {
    //Send control node message on subnet bcast after family has changed to L2
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:11", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"11.1.1.0", 24, "11.1.1.200", true},
    };

    IpamInfo ipam_info_2[] = {
        {"11.1.1.0", 24, "11.1.1.100", true},
    };

    EXPECT_FALSE(VrfFind("vrf1"));
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

	WAIT_FOR(1000, 1000, (VmPortActive(input, 0) == true));

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (RouteFind("vrf1", "11.1.1.255", 32)));
    InetUnicastRouteEntry *route_1 =
        RouteGet("vrf1", Ip4Address::from_string("11.1.1.255"), 32);

    //Change IPAM
    AddIPAM("vn1", ipam_info_2, 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (RouteFind("vrf1", "11.1.1.255", 32)));
    InetUnicastRouteEntry *route_2 =
        RouteGet("vrf1", Ip4Address::from_string("11.1.1.255"), 32);

    EXPECT_TRUE(route_1 == route_2);
    //Restore and cleanup
    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VrfFind("vrf1") == false));
}

TEST_F(MulticastTest, McastSubnet_VN2MultipleVRFtest_negative) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"10.1.1.0", 30, "10.1.1.2", true},
        {"10.1.1.10", 29, "10.1.1.14", true},
        {"11.1.1.0", 30, "11.1.1.2", true},
    };

    const char *vrf_name = "vn1:vn1";

    AddVn("vn1", 1);
    AddVrf(vrf_name, 2);
    AddIPAM("vn1", ipam_info, 3);
    AddLink("virtual-network", "vn1", "routing-instance", vrf_name);
    client->WaitForIdle();
    client->Reset();

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 1, 0);
	WAIT_FOR(1000, 1000, VmPortActive(input, 0));
    WAIT_FOR(1000, 1000, RouteFind("vrf1", "11.1.1.3", 32));
    WAIT_FOR(1000, 1000, !RouteFind("vrf1", "10.1.1.2", 32));

    //Should delete all routes from vrf1
    DelIPAM("vn1");
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, 0, 0);
	WAIT_FOR(1000, 1000, !VmPortActive(input, 0));

    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(agent_->vn_table()->Size() == 1);

    AddIPAM("vn1", ipam_info, 3);
    client->WaitForIdle();
    DelLink("virtual-network", "vn1", "routing-instance", vrf_name);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();

    InetUnicastAgentRouteTable::DeleteReq(Agent::GetInstance()->
                                           local_vm_peer(), "vrf1",
                                           IpAddress::from_string("11.1.1.3").to_v4(),
                                           32, NULL);
    InetUnicastAgentRouteTable::DeleteReq(Agent::GetInstance()->
                                           local_vm_peer(), vrf_name,
                                           IpAddress::from_string("11.1.1.3").to_v4(),
                                           32, NULL);
    DelVrf(vrf_name);
    DelVrf("vrf1");
    DelVn("vn1");
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
