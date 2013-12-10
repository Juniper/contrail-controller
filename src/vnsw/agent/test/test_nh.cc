/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
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

void RouterIdDepInit() {
}

class CfgTest : public ::testing::Test {
};

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

static void WaitForIdle(uint32_t size, uint32_t max_retries) {
    do {
        client->WaitForIdle();
    } while ((Agent::GetInstance()->GetNextHopTable()->Size() == size) && max_retries-- > 0);
}

static void CreateTunnelNH(const string &vrf_name, const Ip4Address &sip,
                           const Ip4Address &dip, bool policy,
                           TunnelType::TypeBmap bmap){
    DBRequest req;
    TunnelNHData *data = new TunnelNHData();
    uint32_t table_size = Agent::GetInstance()->GetNextHopTable()->Size();

    NextHopKey *key = new TunnelNHKey(vrf_name, sip, dip, policy,
                                      TunnelType::ComputeType(bmap)); 
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(data);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
    WaitForIdle(table_size, 5);
}

static void DeleteTunnelNH(const string &vrf_name, const Ip4Address &sip,
                           const Ip4Address &dip, bool policy,
                           TunnelType::TypeBmap bmap){
    DBRequest req;
    TunnelNHData *data = new TunnelNHData();
    uint32_t table_size = Agent::GetInstance()->GetNextHopTable()->Size();

    NextHopKey *key = new TunnelNHKey(vrf_name, sip, dip, policy,
                                      TunnelType::ComputeType(bmap)); 
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(key);
    req.data.reset(data);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
    WaitForIdle(table_size, 5);
}

TEST_F(CfgTest, TunnelNh_1) {
    DBRequest req;

    client->WaitForIdle();

    NextHopKey *dscd_key = new DiscardNHKey();
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(dscd_key);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
    client->WaitForIdle();

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   TunnelType::AllType());
    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), true,
                   TunnelType::AllType());
    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(),
                   Ip4Address::from_string("30.30.30.30"),
                   Ip4Address::from_string("20.20.20.20"), true,
                   TunnelType::AllType());
    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), 
                   Ip4Address::from_string("33.30.30.30"),
                   Ip4Address::from_string("22.20.20.20"), false,
                   TunnelType::AllType());

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   TunnelType::AllType());
    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), true,
                   TunnelType::AllType());
    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), 
                   Ip4Address::from_string("30.30.30.30"),
                   Ip4Address::from_string("20.20.20.20"), true,
                   TunnelType::AllType());
    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), 
                   Ip4Address::from_string("33.30.30.30"),
                   Ip4Address::from_string("22.20.20.20"), false,
                   TunnelType::AllType());

    client->WaitForIdle();
}

TEST_F(CfgTest, TunnelNh_2) {
    DBRequest req;

    client->WaitForIdle();
    AddEncapList("MPLSoUDP", NULL, NULL);
    client->WaitForIdle();

    NextHopKey *dscd_key = new DiscardNHKey();
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(dscd_key);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
    client->WaitForIdle();

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::MPLS_UDP));
    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     false, TunnelType::MPLS_UDP) == true));
    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), true,
                   (1 << TunnelType::MPLS_UDP));
    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     true, TunnelType::MPLS_UDP)));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::MPLS_UDP));
    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), true,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, MirrorNh_1) {
    DBRequest req;
    uint32_t table_size = Agent::GetInstance()->GetNextHopTable()->Size();

    AddVrf("test_vrf");
    client->WaitForIdle();
    client->Reset();

    NextHopKey *dscd_key = new DiscardNHKey();
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(dscd_key);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
    client->WaitForIdle();

    MirrorNHKey key
        (Agent::GetInstance()->GetDefaultVrf(), Ip4Address::from_string("10.10.10.10"),
         10, Ip4Address::from_string("20.20.20.20"), 20);
    table_size = Agent::GetInstance()->GetNextHopTable()->Size();
    std::string analyzer_1 = "TestAnalyzer_1";
    MirrorTable::AddMirrorEntry(analyzer_1, Agent::GetInstance()->GetDefaultVrf(),
            Ip4Address::from_string("10.10.10.10"), 10,
            Ip4Address::from_string("20.20.20.20"), 20);
    WaitForIdle(table_size, 5);

    EXPECT_TRUE(Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&key) != NULL);

    /* Test dport */
    table_size = Agent::GetInstance()->GetNextHopTable()->Size();
    std::string analyzer_2 = "TestAnalyzer_2";
    MirrorTable::AddMirrorEntry(analyzer_2, Agent::GetInstance()->GetDefaultVrf(),
            Ip4Address::from_string("10.10.10.10"), 10,
            Ip4Address::from_string("20.20.20.20"), 30);
    MirrorNHKey mirror_dport_key
        (Agent::GetInstance()->GetDefaultVrf(), Ip4Address::from_string("10.10.10.10"),
         10, Ip4Address::from_string("20.20.20.20"), 30);
    WaitForIdle(table_size, 5);
    table_size = Agent::GetInstance()->GetNextHopTable()->Size();

    EXPECT_TRUE(Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&mirror_dport_key) != NULL);

    EXPECT_TRUE(Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&key) != NULL);

    /* Delete Added mirror entries */
    table_size = Agent::GetInstance()->GetNextHopTable()->Size();
    MirrorTable::DelMirrorEntry(analyzer_1);
    WaitForIdle(table_size, 5);
    table_size = Agent::GetInstance()->GetNextHopTable()->Size();

    MirrorTable::DelMirrorEntry(analyzer_2);
    WaitForIdle(table_size, 5);
}

TEST_F(CfgTest, NhSandesh_1) {
    uint32_t table_size = Agent::GetInstance()->GetNextHopTable()->Size();

    VrfAddReq("test_vrf_sandesh");
    client->WaitForIdle();

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("100.1.1.10"), false,
                   TunnelType::AllType());

    table_size = Agent::GetInstance()->GetNextHopTable()->Size();
    std::string analyzer_1 = "AnalyzerNhSandesh_1";
    MirrorTable::AddMirrorEntry(analyzer_1,
                          Agent::GetInstance()->GetDefaultVrf(),
                          Ip4Address::from_string("100.10.10.10"), 100,
                          Ip4Address::from_string("200.20.20.20"), 200);
    WaitForIdle(table_size, 5);

    //Mock the sandesh request, no expecatation just catch crashes.
    NhListReq *nh_list_req = new NhListReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    nh_list_req->HandleRequest();
    client->WaitForIdle();
    nh_list_req->Release();

    client->WaitForIdle();

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("100.1.1.10"), false,
                   TunnelType::AllType());

    table_size = Agent::GetInstance()->GetNextHopTable()->Size();
    MirrorTable::DelMirrorEntry(analyzer_1);
    WaitForIdle(table_size, 5);
}

TEST_F(CfgTest, CreateVrfNh_1) {
    DBRequest req;

    VrfAddReq("test_vrf");
    client->WaitForIdle();

    NextHopKey *key = new VrfNHKey("test_vrf", false);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(NULL);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
    client->WaitForIdle();
}

TEST_F(CfgTest, EcmpNH_1) {
    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.1", "00:00:00:02:02:01", 1, 2},
        {"vnet3", 3, "1.1.1.1", "00:00:00:02:02:03", 1, 3},
        {"vnet4", 4, "1.1.1.1", "00:00:00:02:02:04", 1, 4},
        {"vnet5", 5, "1.1.1.1", "00:00:00:02:02:05", 1, 5},
    };

    CreateVmportEnv(input1, 5);
    client->WaitForIdle();

    //Check that route points to composite NH,
    //with 5 members
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 5);

    //Get the MPLS label corresponding to this path and verify
    //that mpls label also has 5 component NH
    uint32_t mpls_label = rt->GetMplsLabel();
    EXPECT_TRUE(FindMplsLabel(MplsLabel::VPORT_NH, mpls_label));
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 5);

    DeleteVmportEnv(input1, 5, true);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));
    
    //Expect MPLS label to be not present
    EXPECT_FALSE(FindMplsLabel(MplsLabel::VPORT_NH, mpls_label));

    //Make sure composite NH is also deleted
    CompositeNHKey key("vrf1", ip, true);
    EXPECT_FALSE(FindNH(&key));
}

//Create multiple VM with same virtual IP and verify
//ecmp NH gets created and also verify that it gets deleted
//upon VM deletion.
TEST_F(CfgTest, EcmpNH_2) {
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

    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    //First VM added, route points to composite NH
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::INTERFACE);

    //Second VM added, route should point to composite NH
    CreateVmportEnv(input2, 1);
    client->WaitForIdle();
    nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);

    CreateVmportEnv(input3, 1);
    client->WaitForIdle();
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 3);

    CreateVmportEnv(input4, 1);
    client->WaitForIdle();
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 4);

    CreateVmportEnv(input5, 1);
    client->WaitForIdle();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 5);

    //Verify all the component NH have right label and nexthop
    CompositeNH::ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
                                     ((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");
    MplsLabel *mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                                   (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");

    //Verify that mpls label allocated for ECMP route, points 
    //to the same composite NH
    uint32_t composite_nh_mpls_label = rt->GetMplsLabel();
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, composite_nh_mpls_label);
    EXPECT_TRUE(mpls->GetNextHop() == comp_nh);


    //Delete couple of interface and verify composite NH also get 
    //deleted
    DeleteVmportEnv(input2, 1, false);
    DeleteVmportEnv(input4, 1, false);
    client->WaitForIdle();

    //Verify all the component NH have right label and nexthop
    component_nh_it = comp_nh->begin();
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    //Interface 2 and 4 have been deleted, expected the component NH to
    //be NULL
    component_nh_it++;
    EXPECT_TRUE(*component_nh_it == NULL);

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");

    //Interface vnet4 has been deleted, expect the component NH to be NULL
    component_nh_it++;
    EXPECT_TRUE(*component_nh_it == NULL);

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");

    DeleteVmportEnv(input3, 1, false);
    DeleteVmportEnv(input5, 1, false);
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));
    
    //Expect MPLS label to be not present
    EXPECT_FALSE(FindMplsLabel(MplsLabel::VPORT_NH, composite_nh_mpls_label));

    //Make sure composite NH is also deleted
    CompositeNHKey key("vrf1", ip, true);
    EXPECT_FALSE(FindNH(&key));
}

//Create multiple VM with same floating IP and verify
//ecmp NH gets created and also verify that it gets deleted
//upon floating IP disassociation
TEST_F(CfgTest, EcmpNH_3) {
    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:01", 1, 2},
        {"vnet3", 3, "1.1.1.3", "00:00:00:02:02:03", 1, 3},
        {"vnet4", 4, "1.1.1.4", "00:00:00:02:02:04", 1, 4},
        {"vnet5", 5, "1.1.1.5", "00:00:00:02:02:05", 1, 5}
    };

    CreateVmportEnv(input1, 5);
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
    CreateVmportEnv(input2, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(6));

    //Create floating IP pool
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.2.2.2");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn2");

    //Associate vnet1 with floating IP
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    //First VM added, route points to composite NH
    Ip4Address ip = Ip4Address::from_string("2.2.2.2");
    Inet4UnicastRouteEntry *rt = RouteGet("vrf2", ip, 32);
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
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 3);

    AddLink("virtual-machine-interface", "vnet4", "floating-ip", "fip1");
    client->WaitForIdle();
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 4);

    AddLink("virtual-machine-interface", "vnet5", "floating-ip", "fip1");
    client->WaitForIdle();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 5);

    //Verify all the component NH have right label and nexthop
    CompositeNH::ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
                                     ((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");
    MplsLabel *mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                                   (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");

    //Verify that mpls label allocated for ECMP route, points 
    //to the same composite NH
    uint32_t composite_mpls_label = rt->GetMplsLabel();
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, composite_mpls_label);
    EXPECT_TRUE(mpls->GetNextHop() == comp_nh);

    //Delete couple of interface and verify composite NH also get 
    //deleted
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();

    //Interface 1 has been deleted, expected the component NH to
    //be NULL
    component_nh_it = comp_nh->begin();
    EXPECT_TRUE(*component_nh_it == NULL);

    DelLink("virtual-machine-interface", "vnet2", "floating-ip", "fip1");
    client->WaitForIdle();
    //Interface 2 has been deleted, expected the component NH to
    //be NULL
    component_nh_it++;
    EXPECT_TRUE(*component_nh_it == NULL);

    DelLink("virtual-machine-interface", "vnet3", "floating-ip", "fip1");
    client->WaitForIdle();
    //Interface 2 has been deleted, expected the component NH to
    //be NULL
    component_nh_it++;
    EXPECT_TRUE(*component_nh_it == NULL);

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->GetLabel());
    intf_nh = static_cast<const InterfaceNH *>(mpls->GetNextHop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");

    //Delete the vnet4 floating ip. Since only vent5 has floating IP
    //route should point to interface NH
    DelLink("virtual-machine-interface", "vnet4", "floating-ip", "fip1");
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActiveNextHop() == intf_nh);

    //Make sure composite NH is also deleted
    CompositeNHKey key("vrf2", ip, true);
    EXPECT_FALSE(FindNH(&key));
    //Expect MPLS label to be not present
    EXPECT_FALSE(FindMplsLabel(MplsLabel::VPORT_NH, composite_mpls_label));
 
    DelLink("virtual-machine-interface", "vnet5", "floating-ip", "fip1");
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn2");
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf2", ip, 32));

    DeleteVmportEnv(input2, 1, true);
    DeleteVmportEnv(input1, 5, true);
    client->WaitForIdle();
}

//Create a ECMP NH on a delete marked VRF,
//and make sure NH is not created
TEST_F(CfgTest, EcmpNH_4) {
    AddVrf("vrf2");
    client->WaitForIdle();

    VrfEntryRef vrf1 = VrfGet("vrf2");
    client->WaitForIdle();

    DelVrf("vrf2");
    client->WaitForIdle();

    //Enqueue a request to add composite NH
    //since the entry is marked delete, composite NH will not get
    //created
    std::vector<ComponentNHData> comp_nh_list;
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    CompositeNH::CreateCompositeNH("vrf2", ip, false, comp_nh_list);
    client->WaitForIdle();

    CompositeNHKey key("vrf2", ip, false);
    EXPECT_FALSE(FindNH(&key));
    vrf1.reset();
}

//Create a remote route first pointing to tunnel NH
//Change the route to point to composite NH with old tunnel NH 
//and a new tunnel NH, and make sure
//preexiting NH gets slot 0 in composite NH
TEST_F(CfgTest, EcmpNH_5) {
    AddVrf("vrf2");
    client->WaitForIdle();

    Ip4Address remote_vm_ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    //Add a remote VM route
    Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->
        AddRemoteVmRouteReq(NULL, "vrf2", remote_vm_ip, 32, remote_server_ip1,
                            TunnelType::DefaultType(), 30, "vn2");
    client->WaitForIdle();
    //Create component NH list
    //Transition remote VM route to ECMP route
    NextHopKey *nh_key1 = new TunnelNHKey(Agent::GetInstance()->GetDefaultVrf(),
                                          Agent::GetInstance()->GetRouterId(),
                                          remote_server_ip1, false,
                                          TunnelType::DefaultType());
    NextHopKey *nh_key2 = new TunnelNHKey(Agent::GetInstance()->GetDefaultVrf(),
                                          Agent::GetInstance()->GetRouterId(),
                                          remote_server_ip2, false,
                                          TunnelType::DefaultType());

    ComponentNHData nh_data1(30, nh_key1);
    ComponentNHData nh_data2(20, nh_key2);   

    std::vector<ComponentNHData> comp_nh_list;
    //Insert new NH first and then existing route NH
    comp_nh_list.push_back(nh_data2);
    comp_nh_list.push_back(nh_data1);

    SecurityGroupList sg_id_list;
    Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->
        AddRemoteVmRouteReq(NULL, "vrf2", remote_vm_ip, 32,
                            comp_nh_list, -1, "vn2", sg_id_list);
    client->WaitForIdle();
    Inet4UnicastRouteEntry *rt = RouteGet("vrf2", remote_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);

    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);

    //Verify all the component NH have right label and nexthop
    CompositeNH::ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    const TunnelNH *tun_nh1 = static_cast<const TunnelNH *>
        ((*component_nh_it)->GetNH());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip1);
    EXPECT_TRUE((*component_nh_it)->GetLabel() == 30);

    component_nh_it++;
    tun_nh1 = static_cast<const TunnelNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip2);
    EXPECT_TRUE((*component_nh_it)->GetLabel() == 20);

    Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->DeleteReq(NULL, "vrf2", 
                                                                        remote_vm_ip, 32);
    DelVrf("vrf2");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf2"));
}

//Create a remote route first pointing to tunnel NH
//Change the route to point to composite NH with all new tunnel NH
//make sure preexiting NH doesnt exist in the new component NH list
TEST_F(CfgTest, EcmpNH_6) {
    AddVrf("vrf2");
    client->WaitForIdle();

    Ip4Address remote_vm_ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    Ip4Address remote_server_ip3 = Ip4Address::from_string("10.10.10.102");
    //Add a remote VM route
    Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->AddRemoteVmRouteReq(
                                                  NULL, "vrf2",
                                                  remote_vm_ip,
                                                  32, remote_server_ip1,
                                                  TunnelType::AllType(),
                                                  30, "vn2");
    client->WaitForIdle();
    //Create component NH list
    //Transition remote VM route to ECMP route
    NextHopKey *nh_key1 = new TunnelNHKey(Agent::GetInstance()->GetDefaultVrf(), 
                                          Agent::GetInstance()->GetRouterId(),
                                          remote_server_ip2, false,
                                          TunnelType::DefaultType());
    NextHopKey *nh_key2 = new TunnelNHKey(Agent::GetInstance()->GetDefaultVrf(), 
                                          Agent::GetInstance()->GetRouterId(),
                                          remote_server_ip3, false,
                                          TunnelType::DefaultType());

    ComponentNHData nh_data1(30, nh_key1);
    ComponentNHData nh_data2(20, nh_key2);   

    std::vector<ComponentNHData> comp_nh_list;
    //Insert new NH first and then existing route NH
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);

    SecurityGroupList sg_list;
    Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->
        AddRemoteVmRouteReq(NULL, "vrf2", remote_vm_ip, 32,
                            comp_nh_list, -1, "vn2", sg_list);
    client->WaitForIdle();
    Inet4UnicastRouteEntry *rt = RouteGet("vrf2", remote_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);

    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);

    //Verify all the component NH have right label and nexthop
    CompositeNH::ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    const TunnelNH *tun_nh1 = static_cast<const TunnelNH *>
        ((*component_nh_it)->GetNH());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip2);
    EXPECT_TRUE((*component_nh_it)->GetLabel() == 30);

    component_nh_it++;
    tun_nh1 = static_cast<const TunnelNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(*tun_nh1->GetDip() == remote_server_ip3);
    EXPECT_TRUE((*component_nh_it)->GetLabel() == 20);

    Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->DeleteReq(NULL, "vrf2", 
                                                                        remote_vm_ip, 32);
    DelVrf("vrf2");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf2"));
}

TEST_F(CfgTest, TunnelType_1) {
    AddEncapList("MPLSoGRE", NULL, NULL);
    client->WaitForIdle();

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   ((1 << TunnelType::MPLS_GRE) | (1 << TunnelType::MPLS_UDP)));

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     false, TunnelType::MPLS_GRE) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.11"),
                                     false, TunnelType::MPLS_GRE) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.12"),
                                     false, TunnelType::DefaultType()) == true));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::MPLS_GRE));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::DefaultType()));
    client->WaitForIdle();

    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, TunnelType_2) {
    AddEncapList("MPLSoUDP", NULL, NULL);
    client->WaitForIdle();

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   ((1 << TunnelType::MPLS_GRE) | (1 << TunnelType::MPLS_UDP)));

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     false, TunnelType::MPLS_UDP) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.11"),
                                     false, TunnelType::DefaultType()) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.12"),
                                     false, TunnelType::MPLS_UDP) == true));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::MPLS_GRE));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::DefaultType()));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, TunnelType_3) {
    AddEncapList("MPLSoGRE", "MPLSoUDP", NULL);
    client->WaitForIdle();

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   ((1 << TunnelType::MPLS_GRE) | (1 << TunnelType::MPLS_UDP)));

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     false, TunnelType::MPLS_GRE) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.11"),
                                     false, TunnelType::MPLS_GRE) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.12"),
                                     false, TunnelType::MPLS_UDP) == true));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::MPLS_GRE));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, TunnelType_4) {
    AddEncapList("MPLSoUDP", "MPLSoGRE", NULL);
    client->WaitForIdle();

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   ((1 << TunnelType::MPLS_GRE) | (1 << TunnelType::MPLS_UDP)));

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     false, TunnelType::MPLS_UDP) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.11"),
                                     false, TunnelType::MPLS_GRE) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.12"),
                                     false, TunnelType::MPLS_UDP) == true));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::MPLS_UDP));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, TunnelType_5) {
    AddEncapList(NULL, NULL, NULL);
    client->WaitForIdle();

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   ((1 << TunnelType::MPLS_GRE) | (1 << TunnelType::MPLS_UDP)));

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    CreateTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     false, TunnelType::DefaultType()) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.11"),
                                     false, TunnelType::DefaultType()) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.12"),
                                     false, TunnelType::DefaultType()) == true));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::DefaultType()));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::DefaultType()));

    DeleteTunnelNH(Agent::GetInstance()->GetDefaultVrf(), Agent::GetInstance()->GetRouterId(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::DefaultType()));
    client->WaitForIdle();

    DelEncapList();
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    return ret;
}
