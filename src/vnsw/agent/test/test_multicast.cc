/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "vr_types.h"

#include <boost/assign/list_of.hpp>

#define BUF_SIZE (12 * 1024)
using namespace std;
using namespace boost::assign;

void RouterIdDepInit() {
}

class CfgTest : public ::testing::Test {
};

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

void WaitForCompositeNHDelete(Ip4Address grp, std::string vrf_name)
{
    CompositeNHKey key(vrf_name, grp,
                       IpAddress::from_string("0.0.0.0").to_v4(), false);
    NextHop *nh;
    do {
        nh = static_cast<NextHop *>(Agent::GetNextHopTable()->FindActiveEntry(&key));
        usleep(1000);
        client->WaitForIdle();
    } while ((nh != NULL) && (nh->IsDeleted() != true));
}

TEST_F(CfgTest, McastSubnet_1) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 2},
        {"vnet3", 3, "3.3.3.1", "00:00:00:03:03:03", 1, 3},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
        {"2.2.2.0", 24, "2.2.2.200"},
        {"3.3.3.0", 16, "3.3.30.200"},
    };

    IpamInfo ipam_updated_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
        {"2.2.2.0", 24, "2.2.2.200"},
        {"3.3.3.0", 16, "3.3.30.200"},
        {"4.0.0.0", 8, "4.4.40.200"},
    };

    IpamInfo ipam_updated_info_2[] = {
        {"2.2.2.0", 24, "2.2.2.200"},
        {"3.3.3.0", 16, "3.3.30.200"},
    };
    char buf[BUF_SIZE];
    int len = 0;

    client->Reset();
    CreateVmportEnv(input, 3, 0);
    client->WaitForIdle();

    client->Reset();

	Inet4UcRoute *rt;
    NextHop *nh;
    CompositeNH *cnh;
    MulticastGroupObject *mcobj;

    AddIPAM("vn1", ipam_info, 3);
    client->WaitForIdle();

	EXPECT_TRUE(VmPortActive(input, 0));
	EXPECT_TRUE(VmPortActive(input, 1));
	EXPECT_TRUE(VmPortActive(input, 2));

    while (RouteFind("vrf1", "1.1.1.255", 32) != true) {
        client->WaitForIdle();
    }
    EXPECT_TRUE(RouteFind("vrf1", "1.1.1.255", 32));
    EXPECT_TRUE(RouteFind("vrf1", "3.3.255.255", 32));

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000, 
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers("vrf1",
                                          IpAddress::from_string("1.1.1.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    AddArp("8.8.8.8", "00:00:08:08:08:08", Agent::GetIpFabricItfName().c_str());
    client->WaitForIdle();

    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    rt = RouteGet("vrf1", addr, 32);
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    cnh = static_cast<CompositeNH *>(nh);
    mcobj = MulticastHandler::GetInstance()->FindGroupObject(cnh->GetVrfName(),
                                              cnh->GetGrpAddr());
    ASSERT_TRUE(mcobj->GetSourceMPLSLabel() == 1111);
	MplsLabel *mpls = 
	    Agent::GetMplsTable()->FindMplsLabel(1111);
	ASSERT_TRUE(mpls == NULL);
    ASSERT_TRUE((mcobj->GetLocalOlist()).size() == 2);
    ASSERT_TRUE((mcobj->GetTunnelOlist()).size() == 1);

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
    MulticastHandler::ModifyFabricMembers("vrf1",
                                          IpAddress::from_string("3.3.255.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          2222, olist_map1);

    addr = Ip4Address::from_string("3.3.255.255");
    rt = RouteGet("vrf1", addr, 32);
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    cnh = static_cast<CompositeNH *>(nh);
    ASSERT_TRUE(nh != NULL);
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
    ASSERT_TRUE(mcobj->GetSourceMPLSLabel() == 2222);
	mpls = Agent::GetMplsTable()->FindMplsLabel(2222);
	ASSERT_TRUE(mpls == NULL);
    ASSERT_TRUE((mcobj->GetLocalOlist()).size() == 1);
    ASSERT_TRUE((mcobj->GetTunnelOlist()).size() == 3);

    client->WaitForIdle();
    TunnelOlist olist_map2;
    olist_map2.push_back(OlistTunnelEntry(8888, 
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    olist_map2.push_back(OlistTunnelEntry(5555, 
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers("vrf1",
                                          IpAddress::from_string("3.3.255.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          2222, olist_map2);
    client->WaitForIdle();

    addr = Ip4Address::from_string("3.3.255.255");
    rt = RouteGet("vrf1", addr, 32);
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
    ASSERT_TRUE(mcobj->GetSourceMPLSLabel() == 2222);
	mpls = Agent::GetMplsTable()->FindMplsLabel(2222);
	ASSERT_TRUE(mpls == NULL);
    ASSERT_TRUE((mcobj->GetLocalOlist()).size() == 1);
    ASSERT_TRUE((mcobj->GetTunnelOlist()).size() == 2);

    memset(buf, 0, BUF_SIZE);
    AddXmlHdr(buf, len);
    AddNodeString(buf, len, "virtual-network-network-ipam", 
                  "default-network-ipam,vn1", ipam_updated_info, 4);
    AddXmlTail(buf, len);
    ApplyXmlString(buf);
    client->WaitForIdle();

    rt = RouteGet("vrf1", IpAddress::from_string("4.255.255.255").to_v4(), 32);
    while (rt == NULL) {
        rt = RouteGet("vrf1", IpAddress::from_string("4.255.255.255").to_v4(), 32);
        client->WaitForIdle();
    }
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

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

    WaitForCompositeNHDelete(IpAddress::from_string("1.1.1.255").to_v4(), "vrf1");
    WaitForCompositeNHDelete(IpAddress::from_string("4.255.255.255").to_v4(), "vrf1");

    client->Reset();
    DelIPAM("vn1"); 
    client->WaitForIdle();


    client->Reset();
    DeleteVmportEnv(input, 3, 1, 0);
    client->WaitForIdle();

    WaitForCompositeNHDelete(IpAddress::from_string("1.1.1.255").to_v4(), "vrf1");
    WaitForCompositeNHDelete(IpAddress::from_string("4.255.255.255").to_v4(), "vrf1");
    WaitForCompositeNHDelete(IpAddress::from_string("2.2.2.255").to_v4(), "vrf1");
    WaitForCompositeNHDelete(IpAddress::from_string("3.3.255.255").to_v4(), "vrf1");
    WaitForCompositeNHDelete(IpAddress::from_string("255.255.255.255").to_v4(), "vrf1");
    client->WaitForIdle();
}

TEST_F(CfgTest, McastSubnet_DeleteRouteOnVRFDeleteofVN) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
    };

    client->Reset();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    client->Reset();

    char buf[BUF_SIZE];
    int len = 0;
	Inet4UcRoute *rt;
    NextHop *nh;
    CompositeNH *cnh;
    MulticastGroupObject *mcobj;

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

	EXPECT_TRUE(VmPortActive(input, 0));

    while (RouteFind("vrf1", "1.1.1.255", 32) != true) {
        client->WaitForIdle();
    }
    EXPECT_TRUE(RouteFind("vrf1", "1.1.1.255", 32));
    CompositeNHKey key("vrf1", IpAddress::from_string("1.1.1.255").to_v4(),
                       IpAddress::from_string("0.0.0.0").to_v4(), false);
    nh = static_cast<NextHop *>(Agent::GetNextHopTable()->FindActiveEntry(&key));
    cnh = static_cast<CompositeNH *>(nh);
    while(cnh->ComponentNHCount() == 0) {
        client->WaitForIdle();
    }

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000,
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers("vrf1",
                                          IpAddress::from_string("1.1.1.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    AddArp("8.8.8.8", "00:00:08:08:08:08", Agent::GetIpFabricItfName().c_str());
    client->WaitForIdle();

    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    rt = RouteGet("vrf1", addr, 32);
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    cnh = static_cast<CompositeNH *>(nh);
    mcobj = MulticastHandler::GetInstance()->FindGroupObject(cnh->GetVrfName(),
                                              cnh->GetGrpAddr());
    ASSERT_TRUE(mcobj->GetSourceMPLSLabel() == 1111);
	MplsLabel *mpls = 
	    Agent::GetMplsTable()->FindMplsLabel(1111);
	ASSERT_TRUE(mpls == NULL);
    ASSERT_TRUE((mcobj->GetLocalOlist()).size() == 1);
    ASSERT_TRUE((mcobj->GetTunnelOlist()).size() == 1);

    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    rt = RouteGet("vrf1", IpAddress::from_string("1.1.1.255").to_v4(), 32);
    while (rt != NULL) {
        rt = RouteGet("vrf1", IpAddress::from_string("1.1.1.255").to_v4(), 32);
        client->WaitForIdle();
    }
    client->WaitForIdle();
}

TEST_F(CfgTest, McastSubnet_DeleteRouteOnIPAMDeleteofVN) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
    };

    client->Reset();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    client->Reset();

    char buf[BUF_SIZE];
    int len = 0;
	Inet4UcRoute *rt;
    NextHop *nh;
    CompositeNH *cnh;
    MulticastGroupObject *mcobj;

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

	EXPECT_TRUE(VmPortActive(input, 0));

    while (RouteFind("vrf1", "1.1.1.255", 32) != true) {
        client->WaitForIdle();
    }
    EXPECT_TRUE(RouteFind("vrf1", "1.1.1.255", 32));

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000, 
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers("vrf1",
                                          IpAddress::from_string("1.1.1.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    AddArp("8.8.8.8", "00:00:08:08:08:08", Agent::GetIpFabricItfName().c_str());
    client->WaitForIdle();

    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    rt = RouteGet("vrf1", addr, 32);
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    cnh = static_cast<CompositeNH *>(nh);
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1",
                                              cnh->GetGrpAddr());
    ASSERT_TRUE(mcobj->GetSourceMPLSLabel() == 1111);
	MplsLabel *mpls = 
	    Agent::GetMplsTable()->FindMplsLabel(1111);
	ASSERT_TRUE(mpls == NULL);
    ASSERT_TRUE((mcobj->GetLocalOlist()).size() == 1);
    ASSERT_TRUE((mcobj->GetTunnelOlist()).size() == 1);

    DelLink("virtual-network", "vn1", "virtual-network-network-ipam", 
            "default-network-ipam,vn1");
    client->WaitForIdle();

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    rt = RouteGet("vrf1", IpAddress::from_string("1.1.1.255").to_v4(), 32);
    while (rt != NULL) {
        rt = RouteGet("vrf1", IpAddress::from_string("1.1.1.255").to_v4(), 32);
        client->WaitForIdle();
    }
    client->WaitForIdle();
}

TEST_F(CfgTest, McastSubnet_DeleteCompNHThenModifyFabricList) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
    };

    client->Reset();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    client->Reset();

    char buf[BUF_SIZE];
    int len = 0;
	Inet4UcRoute *rt;
    NextHop *nh;
    //CompositeNH *cnh;
    //MulticastGroupObject *mcobj;

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

	EXPECT_TRUE(VmPortActive(input, 0));

    while (RouteFind("vrf1", "1.1.1.255", 32) != true) {
        client->WaitForIdle();
    }
    EXPECT_TRUE(RouteFind("vrf1", "1.1.1.255", 32));

    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    rt = RouteGet("vrf1", addr, 32);
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    EXPECT_TRUE(nh != NULL);
    
    DBRequest req;
    NextHopKey *key = new CompositeNHKey
        ("vrf1", IpAddress::from_string("1.1.1.255").to_v4(),
         IpAddress::from_string("0.0.0.0").to_v4(), false);
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(key);
    req.data.reset(NULL);
    Agent::GetNextHopTable()->Enqueue(&req);

    MulticastGroupObject *mcobj;
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1",
                                              IpAddress::from_string("1.1.1.255").to_v4());
    EXPECT_FALSE(mcobj == NULL);
    mcobj->Deleted(true);

    TunnelOlist olist_map;
    olist_map.push_back(OlistTunnelEntry(2000, 
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::AllType()));
    MulticastHandler::ModifyFabricMembers("vrf1",
                                          IpAddress::from_string("1.1.1.255").to_v4(),
                                          IpAddress::from_string("0.0.0.0").to_v4(),
                                          1111, olist_map);
    AddArp("8.8.8.8", "00:00:08:08:08:08", Agent::GetIpFabricItfName().c_str());
    client->WaitForIdle();

    CompositeNHKey nhkey("vrf1", IpAddress::from_string("1.1.1.255").to_v4(),
                         IpAddress::from_string("0.0.0.0").to_v4(), false);
    nh = static_cast<NextHop *>(Agent::GetNextHopTable()->Find(&nhkey, true));
    EXPECT_TRUE(nh->IsDeleted() == true);

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();

    rt = RouteGet("vrf1", IpAddress::from_string("1.1.1.255").to_v4(), 32);
    while (rt != NULL) {
        rt = RouteGet("vrf1", IpAddress::from_string("1.1.1.255").to_v4(), 32);
        client->WaitForIdle();
    }
    client->WaitForIdle();
}

TEST_F(CfgTest, McastSubnet_SubnetIPAMAddDel) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    IpamInfo ipam_info[] = {
        {"10.1.1.0", 30, "10.1.1.2"},
        {"11.1.1.0", 30, "11.1.1.2"},
    };

    IpamInfo ipam_info_2[] = {
        {"10.1.1.0", 30, "10.1.1.2"},
        {"10.1.1.10", 29, "10.1.1.14"},
        {"11.1.1.0", 30, "11.1.1.2"},
    };

    client->Reset();
	EXPECT_TRUE(VmPortActive(input, 0));

    char buf[BUF_SIZE];
    int len = 0;

    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();

    while (RouteFind("vrf1", "11.1.1.3", 32) != true) {
        client->WaitForIdle();
    }
    EXPECT_TRUE(RouteFind("vrf1", "11.1.1.3", 32));
    EXPECT_TRUE(RouteFind("vrf1", "10.1.1.2", 32));

    AddIPAM("vn1", ipam_info_2, 3);
    client->WaitForIdle();
    while (RouteFind("vrf1", "10.1.1.14", 32) != true) {
        client->WaitForIdle();
    }
    EXPECT_TRUE(RouteFind("vrf1", "11.1.1.3", 32));
    EXPECT_TRUE(RouteFind("vrf1", "10.1.1.2", 32));

    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();
    while (RouteFind("vrf1", "10.1.1.14", 32) == true) {
        client->WaitForIdle();
    }
    EXPECT_TRUE(RouteFind("vrf1", "11.1.1.3", 32));
    EXPECT_TRUE(RouteFind("vrf1", "10.1.1.2", 32));

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();

    client->Reset();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
    while (RouteFind("vrf1", "11.1.1.3", 32) != false) {
        client->WaitForIdle();
    }

    client->WaitForIdle();
}

TEST_F(CfgTest, McastSubnet_VN2MultipleVRFtest_ignore_unknown_vrf) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"10.1.1.0", 30, "10.1.1.2"},
        {"10.1.1.10", 29, "10.1.1.14"},
        {"11.1.1.0", 30, "11.1.1.2"},
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

    client->Reset();
	EXPECT_TRUE(VmPortActive(input, 0));

    while (RouteFind("vrf1", "11.1.1.3", 32) != true) {
        client->WaitForIdle();
    }
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
    while (RouteFind("vrf1", "1.1.1.255", 32) != false) {
        client->WaitForIdle();
    }
    EXPECT_FALSE(RouteFind("vrf1", "11.1.1.3", 32));

    client->WaitForIdle();
}

TEST_F(CfgTest, McastSubnet_VN2MultipleVRFtest_negative) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "11.1.1.2", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"10.1.1.0", 30, "10.1.1.2"},
        {"10.1.1.10", 29, "10.1.1.14"},
        {"11.1.1.0", 30, "11.1.1.2"},
    };

    const char *vrf_name = "vn1:vn1";

    AddVn("vn1", 1);
    AddVrf(vrf_name, 2);
    AddIPAM("vn1", ipam_info, 3);
    AddLink("virtual-network", "vn1", "routing-instance", vrf_name);
    client->WaitForIdle();
    client->Reset();

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();

    client->Reset();
	EXPECT_TRUE(VmPortActive(input, 0));

    while (RouteFind("vrf1", "11.1.1.3", 32) != true) {
        client->WaitForIdle();
    }
    EXPECT_TRUE(RouteFind("vrf1", "11.1.1.3", 32));
    EXPECT_FALSE(RouteFind("vrf1", "10.1.1.2", 32));

    DelIPAM("vn1");
    client->Reset();
    client->WaitForIdle();
    DelLink("virtual-network", "vn1", "routing-instance", vrf_name);
    DelVrf(vrf_name);
    client->Reset();

    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    return RUN_ALL_TESTS();
}
