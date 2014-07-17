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

void RouterIdDepInit(Agent *agent) {
}

class CfgTest : public ::testing::Test {
public:
    void SetUp() {
        boost::system::error_code ec;
        bgp_peer = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                 "xmpp channel");
        agent_ = Agent::GetInstance();
    }
    void TearDown() {
        WAIT_FOR(1000, 1000, agent_->vrf_table()->Size() == 1);
    }

    Agent *agent_;
    BgpPeer *bgp_peer;
};

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

static void DoNextHopSandesh() {
    NhListReq *nh_list_req = new NhListReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    nh_list_req->HandleRequest();
    client->WaitForIdle();
    nh_list_req->Release();
    client->WaitForIdle();

}

static void WaitForIdle(uint32_t size, uint32_t max_retries) {
    do {
        client->WaitForIdle();
    } while ((Agent::GetInstance()->nexthop_table()->Size() == size) && max_retries-- > 0);
}

static void CreateTunnelNH(const string &vrf_name, const Ip4Address &sip,
                           const Ip4Address &dip, bool policy,
                           TunnelType::TypeBmap bmap){
    DBRequest req;
    TunnelNHData *data = new TunnelNHData();
    uint32_t table_size = Agent::GetInstance()->nexthop_table()->Size();

    NextHopKey *key = new TunnelNHKey(vrf_name, sip, dip, policy,
                                      TunnelType::ComputeType(bmap)); 
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(data);
    Agent::GetInstance()->nexthop_table()->Enqueue(&req);
    WaitForIdle(table_size, 5);
}

static void DeleteTunnelNH(const string &vrf_name, const Ip4Address &sip,
                           const Ip4Address &dip, bool policy,
                           TunnelType::TypeBmap bmap){
    DBRequest req;
    TunnelNHData *data = new TunnelNHData();
    uint32_t table_size = Agent::GetInstance()->nexthop_table()->Size();

    NextHopKey *key = new TunnelNHKey(vrf_name, sip, dip, policy,
                                      TunnelType::ComputeType(bmap)); 
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(key);
    req.data.reset(data);
    Agent::GetInstance()->nexthop_table()->Enqueue(&req);
    WaitForIdle(table_size, 5);
}

TEST_F(CfgTest, TunnelNh_1) {
    DBRequest req;

    client->WaitForIdle();

    NextHopKey *dscd_key = new DiscardNHKey();
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(dscd_key);
    agent_->nexthop_table()->Enqueue(&req);
    client->WaitForIdle();

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   TunnelType::AllType());
    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), true,
                   TunnelType::AllType());
    CreateTunnelNH(agent_->fabric_vrf_name(),
                   Ip4Address::from_string("30.30.30.30"),
                   Ip4Address::from_string("20.20.20.20"), true,
                   TunnelType::AllType());
    CreateTunnelNH(agent_->fabric_vrf_name(), 
                   Ip4Address::from_string("33.30.30.30"),
                   Ip4Address::from_string("22.20.20.20"), false,
                   TunnelType::AllType());

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   TunnelType::AllType());
    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), true,
                   TunnelType::AllType());
    DeleteTunnelNH(agent_->fabric_vrf_name(), 
                   Ip4Address::from_string("30.30.30.30"),
                   Ip4Address::from_string("20.20.20.20"), true,
                   TunnelType::AllType());
    DeleteTunnelNH(agent_->fabric_vrf_name(), 
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
    agent_->nexthop_table()->Enqueue(&req);
    client->WaitForIdle();

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::MPLS_UDP));
    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     false, TunnelType::MPLS_UDP) == true));
    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), true,
                   (1 << TunnelType::MPLS_UDP));
    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     true, TunnelType::MPLS_UDP)));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::MPLS_UDP));
    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), true,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, MirrorNh_1) {
    DBRequest req;
    uint32_t table_size = agent_->nexthop_table()->Size();

    AddVrf("test_vrf");
    client->WaitForIdle();
    client->Reset();

    NextHopKey *dscd_key = new DiscardNHKey();
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(dscd_key);
    agent_->nexthop_table()->Enqueue(&req);
    client->WaitForIdle();

    MirrorNHKey key
        (agent_->fabric_vrf_name(), Ip4Address::from_string("10.10.10.10"),
         10, Ip4Address::from_string("20.20.20.20"), 20);
    table_size = agent_->nexthop_table()->Size();
    std::string analyzer_1 = "TestAnalyzer_1";
    MirrorTable::AddMirrorEntry(analyzer_1, agent_->fabric_vrf_name(),
            Ip4Address::from_string("10.10.10.10"), 10,
            Ip4Address::from_string("20.20.20.20"), 20);
    WaitForIdle(table_size, 5);

    //Do key operations
    MirrorNH *mirror_nh = static_cast<MirrorNH *>(agent_->nexthop_table()->
                                                  FindActiveEntry(&key));
    EXPECT_TRUE(mirror_nh != NULL);
    mirror_nh->SetKey(mirror_nh->GetDBRequestKey().release());

    /* Test dport */
    table_size = agent_->nexthop_table()->Size();
    std::string analyzer_2 = "TestAnalyzer_2";
    MirrorTable::AddMirrorEntry(analyzer_2, agent_->fabric_vrf_name(),
            Ip4Address::from_string("10.10.10.10"), 10,
            Ip4Address::from_string("20.20.20.20"), 30);
    MirrorNHKey mirror_dport_key
        (agent_->fabric_vrf_name(), Ip4Address::from_string("10.10.10.10"),
         10, Ip4Address::from_string("20.20.20.20"), 30);
    WaitForIdle(table_size, 5);
    table_size = agent_->nexthop_table()->Size();

    EXPECT_TRUE(agent_->nexthop_table()->FindActiveEntry(&mirror_dport_key) != NULL);

    EXPECT_TRUE(agent_->nexthop_table()->FindActiveEntry(&key) != NULL);

    /* Delete Added mirror entries */
    table_size = agent_->nexthop_table()->Size();
    MirrorTable::DelMirrorEntry(analyzer_1);
    WaitForIdle(table_size, 5);
    table_size = agent_->nexthop_table()->Size();

    MirrorTable::DelMirrorEntry(analyzer_2);
    WaitForIdle(table_size, 5);
    DelVrf("test_vrf");
}

TEST_F(CfgTest, NhSandesh_1) {
    uint32_t table_size = agent_->nexthop_table()->Size();

    VrfAddReq("test_vrf_sandesh");
    client->WaitForIdle();

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("100.1.1.10"), false,
                   TunnelType::AllType());

    table_size = agent_->nexthop_table()->Size();
    std::string analyzer_1 = "AnalyzerNhSandesh_1";
    MirrorTable::AddMirrorEntry(analyzer_1,
                          agent_->fabric_vrf_name(),
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

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("100.1.1.10"), false,
                   TunnelType::AllType());

    table_size = agent_->nexthop_table()->Size();
    MirrorTable::DelMirrorEntry(analyzer_1);
    WaitForIdle(table_size, 5);
    VrfDelReq("test_vrf_sandesh");
}

TEST_F(CfgTest, CreateVrfNh_1) {
    DBRequest req;

    VrfAddReq("test_vrf");
    client->WaitForIdle();

    NextHopKey *key = new VrfNHKey("test_vrf", false);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(NULL);
    agent_->nexthop_table()->Enqueue(&req);
    client->WaitForIdle();

    key = new VrfNHKey("test_vrf", false);
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(key);
    req.data.reset(NULL);
    agent_->nexthop_table()->Enqueue(&req);

    VrfDelReq("test_vrf");
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

    CreateVmportWithEcmp(input1, 5);
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
    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));
    
    //Expect MPLS label to be not present
    EXPECT_FALSE(FindMplsLabel(MplsLabel::VPORT_NH, mpls_label));
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

    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();
    //First VM added, route points to composite NH
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
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

    //Verify all the component NH have right label and nexthop
    ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
                                     ((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");
    MplsLabel *mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                                   (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");

    //Verify that mpls label allocated for ECMP route, points 
    //to the same composite NH
    uint32_t composite_nh_mpls_label = rt->GetMplsLabel();
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, composite_nh_mpls_label);
    EXPECT_TRUE(mpls->nexthop() == comp_nh);


    //Delete couple of interface and verify composite NH also get 
    //deleted
    DeleteVmportEnv(input2, 1, false);
    DeleteVmportEnv(input4, 1, false);
    client->WaitForIdle();

    //Verify all the component NH have right label and nexthop
    comp_nh = static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    component_nh_it = comp_nh->begin();
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    //Interface 2 and 4 have been deleted, expected the component NH to
    //be NULL
    component_nh_it++;
    EXPECT_TRUE(*component_nh_it == NULL);

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");

    //Interface vnet4 has been deleted, expect the component NH to be NULL
    component_nh_it++;
    EXPECT_TRUE(*component_nh_it == NULL);

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");

    DeleteVmportEnv(input3, 1, false);
    DeleteVmportEnv(input5, 1, false);
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));
    
    //Expect MPLS label to be not present
    EXPECT_FALSE(FindMplsLabel(MplsLabel::VPORT_NH, composite_nh_mpls_label));

    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
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
    Inet4UnicastRouteEntry *rt = RouteGet("default-project:vn2:vn2", ip, 32);
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
    MplsLabel *mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                                   (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");

    //Verify that mpls label allocated for ECMP route, points 
    //to the same composite NH
    uint32_t composite_mpls_label = rt->GetMplsLabel();
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, composite_mpls_label);
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
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");

    //Delete the vnet4 floating ip. Since only vent5 has floating IP
    //route should point to interface NH
    DelLink("virtual-machine-interface", "vnet4", "floating-ip", "fip1");
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActiveNextHop() == intf_nh);

    //Expect MPLS label to be not present
    EXPECT_FALSE(FindMplsLabel(MplsLabel::VPORT_NH, composite_mpls_label));
 
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
TEST_F(CfgTest, EcmpNH_4) {
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
TEST_F(CfgTest, EcmpNH_5) {
    AddVrf("vrf2");
    client->WaitForIdle();

    Ip4Address remote_vm_ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    //Add a remote VM route
    Inet4TunnelRouteAdd(NULL, "vrf2", remote_vm_ip, 32, remote_server_ip1,
                        TunnelType::DefaultType(), 30, "vn2",
                        SecurityGroupList(), PathPreference());
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
    EcmpTunnelRouteAdd(NULL, "vrf2", remote_vm_ip, 32,
                       comp_nh_list, -1, "vn2", sg_id_list, PathPreference());
    client->WaitForIdle();
    Inet4UnicastRouteEntry *rt = RouteGet("vrf2", remote_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);

    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);

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

    agent_->fabric_inet4_unicast_table()->DeleteReq(NULL, "vrf2", 
                                                remote_vm_ip, 32, NULL);
    DelVrf("vrf2");
    WAIT_FOR(100, 1000, (VrfFind("vrf2") == false));
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
    Inet4TunnelRouteAdd(NULL, "vrf2",
                        remote_vm_ip,
                        32, remote_server_ip1,
                        TunnelType::AllType(),
                        30, "vn2",
                        SecurityGroupList(), PathPreference());
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
    EcmpTunnelRouteAdd(NULL, "vrf2", remote_vm_ip, 32,
                        comp_nh_list, -1, "vn2", sg_list, PathPreference());
    client->WaitForIdle();
    Inet4UnicastRouteEntry *rt = RouteGet("vrf2", remote_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::COMPOSITE);

    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);

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

    agent_->fabric_inet4_unicast_table()->DeleteReq(NULL, "vrf2", 
                                                remote_vm_ip, 32, NULL);
    DelVrf("vrf2");
    WAIT_FOR(100, 1000, (VrfFind("vrf2") == false));
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf2"));
}

TEST_F(CfgTest, EcmpNH_7) {
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

    boost::system::error_code ec;
    BgpPeer *bgp_peer = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                      "xmpp channel");
    client->WaitForIdle();

    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();
    //First VM added, route points to composite NH
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
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

    DBEntryBase::KeyPtr comp_key = comp_nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(comp_key.release());
    std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(rt->GetMplsLabel(),
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
            comp_nh_list, false, "vn1", sg_id_list, PathPreference());
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
    MplsLabel *mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                                   (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    component_nh_it++;
    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH,
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH,
                        (*component_nh_it)->label());
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
    mpls = GetMplsLabel(MplsLabel::VPORT_NH, 
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");

    //Interface vnet3 has been deleted, expect the component NH to be NULL
    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH,
                        (*component_nh_it)->label());
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
    mpls = GetMplsLabel(MplsLabel::VPORT_NH,
                       (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet1");

    component_nh_it++;
    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH,
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet2");

    component_nh_it++;
    intf_nh = static_cast<const InterfaceNH *>((*component_nh_it)->nh());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");
    mpls = GetMplsLabel(MplsLabel::VPORT_NH,
                        (*component_nh_it)->label());
    intf_nh = static_cast<const InterfaceNH *>(mpls->nexthop());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet3");


    DeleteVmportEnv(input1, 1, false);
    DeleteVmportEnv(input2, 1, false);
    DeleteVmportEnv(input3, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));

    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
}

//Add multiple remote routes with same set of composite NH
//make sure they share the composite NH
TEST_F(CfgTest, EcmpNH_8) {
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
            comp_nh_list, false, "vn1", sg_id_list, PathPreference());
    client->WaitForIdle();

    Ip4Address ip2 = Ip4Address::from_string("100.1.1.2");
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip2, 32,
            comp_nh_list, false, "vn1", sg_id_list, PathPreference());
    client->WaitForIdle();

    Inet4UnicastRouteEntry *rt1 = RouteGet("vrf1", ip1, 32);
    Inet4UnicastRouteEntry *rt2 = RouteGet("vrf1", ip2, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt1->GetActiveNextHop() == rt2->GetActiveNextHop());

    const NextHop *nh = rt1->GetActiveNextHop();
    //Change ip1 route nexthop to be unicast nexthop
    //and ensure ip2 route still points to old composite nexthop
    Inet4TunnelRouteAdd(bgp_peer, "vrf1", ip1, 32, remote_server_ip1,
                        TunnelType::AllType(), 8, "vn1",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();

    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::TUNNEL);
    EXPECT_TRUE(rt2->GetActiveNextHop() == nh);
    client->WaitForIdle();

    //Delete all the routes and make sure nexthop is also deleted
    agent_->fabric_inet4_unicast_table()->
        DeleteReq(bgp_peer, "vrf1", ip1, 32, NULL);
    agent_->fabric_inet4_unicast_table()->
        DeleteReq(bgp_peer, "vrf1", ip2, 32, NULL);
    client->WaitForIdle();

    CompositeNHKey composite_nh_key(Composite::ECMP, true, comp_nh_list, "vrf1");
    EXPECT_FALSE(FindNH(&composite_nh_key));
    DelVrf("vrf1");
}

//Add ECMP composite NH with no member
TEST_F(CfgTest, EcmpNH_9) {
    AddVrf("vrf1");
    client->WaitForIdle();
    ComponentNHKeyList comp_nh_list;
    Ip4Address ip1 = Ip4Address::from_string("100.1.1.1");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.1.1.10");
    SecurityGroupList sg_id_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip1, 32,
            comp_nh_list, false, "vn1", sg_id_list, PathPreference());
    client->WaitForIdle();

    Inet4UnicastRouteEntry *rt1 = RouteGet("vrf1", ip1, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    const NextHop *nh = rt1->GetActiveNextHop();
    //Change ip1 route nexthop to be unicast nexthop
    Inet4TunnelRouteAdd(bgp_peer, "vrf1", ip1, 32, remote_server_ip1,
                        TunnelType::AllType(), 8, "vn1",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();

    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::TUNNEL);
    client->WaitForIdle();

    //Delete all the routes and make sure nexthop is also deleted
    agent_->fabric_inet4_unicast_table()->
        DeleteReq(bgp_peer, "vrf1", ip1, 32, NULL);
    client->WaitForIdle();

    CompositeNHKey composite_nh_key(Composite::ECMP, true, comp_nh_list, "vrf1");
    EXPECT_FALSE(FindNH(&composite_nh_key));
    DelVrf("vrf1");
}

//Add 2 routes with different composite NH
//Modify the routes, to point to same composite NH
//and ensure both routes would share same nexthop
TEST_F(CfgTest, EcmpNH_10) {
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
            comp_nh_list1, false, "vn1", sg_id_list, PathPreference());
    client->WaitForIdle();

    ComponentNHKeyList comp_nh_list2;
    comp_nh_list2.push_back(nh_data3);
    comp_nh_list2.push_back(nh_data2);

    Ip4Address ip2 = Ip4Address::from_string("100.1.1.2");
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip2, 32,
            comp_nh_list2, false, "vn1", sg_id_list, PathPreference());
    client->WaitForIdle();

    Inet4UnicastRouteEntry *rt1 = RouteGet("vrf1", ip1, 32);
    Inet4UnicastRouteEntry *rt2 = RouteGet("vrf1", ip2, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt1->GetActiveNextHop() != rt2->GetActiveNextHop());

    const NextHop *nh = rt1->GetActiveNextHop();
    //Change ip2 route, such that ip1 route and ip2 route
    //should share same nexthop
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip2, 32,
                       comp_nh_list1, false, "vn1", sg_id_list,
                       PathPreference());
    client->WaitForIdle();

    EXPECT_TRUE(rt1->GetActiveNextHop() == rt2->GetActiveNextHop());
    EXPECT_TRUE(rt2->GetActiveNextHop() == nh);
    EXPECT_TRUE(rt2->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    //Make sure old nexthop is deleted
    CompositeNHKey composite_nh_key2(Composite::ECMP, true, comp_nh_list2, "vrf1");
    EXPECT_FALSE(FindNH(&composite_nh_key2));
    CompositeNHKey composite_nh_key1(Composite::ECMP, true, comp_nh_list1, "vrf1");
    EXPECT_TRUE(GetNH(&composite_nh_key1)->GetRefCount() == 2);

    //Delete all the routes and make sure nexthop is also deleted
    agent_->fabric_inet4_unicast_table()->
        DeleteReq(bgp_peer, "vrf1", ip1, 32, NULL);
    agent_->fabric_inet4_unicast_table()->
        DeleteReq(bgp_peer, "vrf1", ip2, 32, NULL);
    client->WaitForIdle();

    EXPECT_FALSE(FindNH(&composite_nh_key1));
    DelVrf("vrf1");
}

//Add 2 routes with different composite NH
//Modify the routes, to point to same composite NH
//and ensure both routes would share same nexthop
TEST_F(CfgTest, EcmpNH_11) {
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
            comp_nh_list1, false, "vn1", sg_id_list, PathPreference());
    client->WaitForIdle();

    ComponentNHKeyList comp_nh_list2;
    comp_nh_list2.push_back(nh_data1);
    comp_nh_list2.push_back(nh_data2);
    comp_nh_list2.push_back(nh_data3);

    Ip4Address ip2 = Ip4Address::from_string("100.1.1.2");
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip2, 32,
            comp_nh_list2, false, "vn1", sg_id_list, PathPreference());
    client->WaitForIdle();

    Inet4UnicastRouteEntry *rt1 = RouteGet("vrf1", ip1, 32);
    Inet4UnicastRouteEntry *rt2 = RouteGet("vrf1", ip2, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt1->GetActiveNextHop() != rt2->GetActiveNextHop());
    CompositeNHKey composite_nh_key1(Composite::ECMP, true, comp_nh_list1, "vrf1");
    CompositeNHKey composite_nh_key2(Composite::ECMP, true, comp_nh_list2, "vrf1");
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
                       PathPreference());
    client->WaitForIdle();

    EXPECT_TRUE(rt1->GetActiveNextHop() == rt2->GetActiveNextHop());
    EXPECT_TRUE(rt2->GetActiveNextHop() == composite_nh2);
    EXPECT_TRUE(rt2->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    //Make sure old nexthop is deleted
    EXPECT_TRUE(composite_nh2->GetRefCount() == 2);
    EXPECT_TRUE(composite_nh2->ComponentNHCount() == 3);

    //Delete all the routes and make sure nexthop is also deleted
    agent_->fabric_inet4_unicast_table()->
        DeleteReq(bgp_peer, "vrf1", ip1, 32, NULL);
    agent_->fabric_inet4_unicast_table()->
        DeleteReq(bgp_peer, "vrf1", ip2, 32, NULL);
    client->WaitForIdle();

    EXPECT_FALSE(FindNH(&composite_nh_key1));
    DelVrf("vrf1");
}

//Add a route pointing to tunnel NH
//Change the route to point to ECMP composite NH with no member
TEST_F(CfgTest, EcmpNH_12) {
    AddVrf("vrf1");
    client->WaitForIdle();

    Ip4Address ip1 = Ip4Address::from_string("100.1.1.1");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.1.1.10");
    SecurityGroupList sg_id_list;

    //Change ip1 route nexthop to be unicast nexthop
    Inet4TunnelRouteAdd(bgp_peer, "vrf1", ip1, 32, remote_server_ip1,
                        TunnelType::AllType(), 8, "vn1",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();

    Inet4UnicastRouteEntry *rt1 = RouteGet("vrf1", ip1, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::TUNNEL);
    client->WaitForIdle();

    ComponentNHKeyList comp_nh_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf1", ip1, 32,
            comp_nh_list, false, "vn1", sg_id_list, PathPreference());
    client->WaitForIdle();

    rt1 = RouteGet("vrf1", ip1, 32);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    //Delete all the routes and make sure nexthop is also deleted
    agent_->fabric_inet4_unicast_table()->
        DeleteReq(bgp_peer, "vrf1", ip1, 32, NULL);
    client->WaitForIdle();

    CompositeNHKey composite_nh_key(Composite::ECMP, true, comp_nh_list, "vrf1");
    EXPECT_FALSE(FindNH(&composite_nh_key));
    DelVrf("vrf1");
}

//Verify renewal of composite NH
TEST_F(CfgTest, EcmpNH_13) {
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
            comp_nh_list, false, "vn1", sg_id_list, PathPreference());
    client->WaitForIdle();

    //Delete composite NH
    CompositeNHKey composite_nh_key(Composite::ECMP, true, comp_nh_list, "vrf1");
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(
            GetNH(&composite_nh_key));
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    NextHopKey *key = new CompositeNHKey(Composite::ECMP, true,
                                         comp_nh_list, "vrf1");
    req.key.reset(key);
    req.data.reset(NULL);
    NextHopTable::GetInstance()->Enqueue(&req);
    client->WaitForIdle();
    EXPECT_TRUE(comp_nh->ActiveComponentNHCount() == 0);
    client->NextHopReset();

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    key = new CompositeNHKey(Composite::ECMP, true,
                             comp_nh_list, "vrf1");
    req.key.reset(key);
    req.data.reset(NULL);
    NextHopTable::GetInstance()->Enqueue(&req);
    client->WaitForIdle();
    client->CompositeNHWait(1);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);
 
    //Delete all the routes and make sure nexthop is also deleted
    agent_->fabric_inet4_unicast_table()->
        DeleteReq(bgp_peer, "vrf1", ip1, 32, NULL);
    client->WaitForIdle();

    EXPECT_FALSE(FindNH(&composite_nh_key));
    DelVrf("vrf1");
}

TEST_F(CfgTest, TunnelType_1) {
    AddEncapList("MPLSoGRE", NULL, NULL);
    client->WaitForIdle();

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   ((1 << TunnelType::MPLS_GRE) | (1 << TunnelType::MPLS_UDP)));

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     false, TunnelType::MPLS_GRE) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.11"),
                                     false, TunnelType::MPLS_GRE) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.12"),
                                     false, TunnelType::DefaultType()) == true));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::MPLS_GRE));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::DefaultType()));
    client->WaitForIdle();

    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, TunnelType_2) {
    AddEncapList("MPLSoUDP", NULL, NULL);
    client->WaitForIdle();

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   ((1 << TunnelType::MPLS_GRE) | (1 << TunnelType::MPLS_UDP)));

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     false, TunnelType::MPLS_UDP) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.11"),
                                     false, TunnelType::DefaultType()) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.12"),
                                     false, TunnelType::MPLS_UDP) == true));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::MPLS_GRE));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::DefaultType()));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, TunnelType_3) {
    AddEncapList("MPLSoGRE", "MPLSoUDP", NULL);
    client->WaitForIdle();

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   ((1 << TunnelType::MPLS_GRE) | (1 << TunnelType::MPLS_UDP)));

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     false, TunnelType::MPLS_GRE) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.11"),
                                     false, TunnelType::MPLS_GRE) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.12"),
                                     false, TunnelType::MPLS_UDP) == true));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::MPLS_GRE));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, TunnelType_4) {
    AddEncapList("MPLSoUDP", "MPLSoGRE", NULL);
    client->WaitForIdle();

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   ((1 << TunnelType::MPLS_GRE) | (1 << TunnelType::MPLS_UDP)));

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     false, TunnelType::MPLS_UDP) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.11"),
                                     false, TunnelType::MPLS_GRE) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.12"),
                                     false, TunnelType::MPLS_UDP) == true));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::MPLS_UDP));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, TunnelType_5) {
    AddEncapList(NULL, NULL, NULL);
    client->WaitForIdle();

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   ((1 << TunnelType::MPLS_GRE) | (1 << TunnelType::MPLS_UDP)));

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));
    client->WaitForIdle();

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     false, TunnelType::DefaultType()) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.11"),
                                     false, TunnelType::DefaultType()) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.12"),
                                     false, TunnelType::DefaultType()) == true));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::DefaultType()));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::DefaultType()));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::DefaultType()));
    client->WaitForIdle();

    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, Nexthop_keys) {
    client->WaitForIdle();
    struct PortInfo input1[] = {
        {"vnet10", 10, "1.1.1.1", "00:00:00:01:01:01", 10, 10}
    };

    client->Reset();
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    client->WaitForIdle();
    CreateVmportEnv(input1, 1);
    WAIT_FOR(1000, 1000, VmPortActive(10)); 
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    WAIT_FOR(1000, 1000, (VrfGet("vrf10") != NULL));
    WAIT_FOR(1000, 1000, (RouteGet("vrf10", ip, 32) != NULL));
    WAIT_FOR(1000, 1000, (RouteGet("vrf10", ip, 32) != NULL));

    //First VM added

    Inet4UnicastRouteEntry *rt = RouteGet("vrf10", ip, 32);
    WAIT_FOR(1000, 1000, (rt->GetActivePath() != NULL));
    const NextHop *nh = rt->GetActivePath()->nexthop(agent_);
    EXPECT_TRUE(nh != NULL);
    WAIT_FOR(100, 1000, (rt->GetActiveNextHop()->GetType() ==
                         NextHop::INTERFACE));

    //Sandesh request
    DoNextHopSandesh();

    //Issue set for nexthop key
    nh = rt->GetActiveNextHop();
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
    NextHopKey *nh_key = static_cast<NextHopKey *>(intf_nh->GetDBRequestKey().release()); 
    EXPECT_TRUE(nh_key->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(nh_key->GetPolicy() == false);
    NextHop *base_nh = static_cast<NextHop *>(agent_->
                                              nexthop_table()->FindActiveEntry(nh_key));
    base_nh->SetKey(base_nh->GetDBRequestKey().release());

    //Issue set for interface nexthop key
    InterfaceNHKey *interface_key = static_cast<InterfaceNHKey *>(nh_key);
    InterfaceNH *interface_nh = static_cast<InterfaceNH *>(base_nh);
    interface_nh->SetKey(interface_key);

    //Issue set for VRF nexthop key
    VxLanIdKey vxlan_key(10);
    VxLanId *vxlan_id_entry = static_cast<VxLanId *>(agent_->
                                         vxlan_table()->FindActiveEntry(&vxlan_key));
    VrfNHKey *vrf_nh_key = static_cast<VrfNHKey *>(vxlan_id_entry->nexthop()->
                                                   GetDBRequestKey().release());
    VrfNH *vrf_nh = static_cast<VrfNH*>(agent_->
                                        nexthop_table()->FindActiveEntry(vrf_nh_key));
    vrf_nh->SetKey(vrf_nh_key);
    DoNextHopSandesh();

    //Tunnel NH key
    agent_->
        fabric_inet4_unicast_table()->
        AddResolveRoute(agent_->fabric_vrf_name(),
                        Ip4Address::from_string("10.1.1.100"), 32);
    client->WaitForIdle();

    struct ether_addr *remote_vm_mac = (struct ether_addr *)malloc(sizeof(struct ether_addr));
    memcpy (remote_vm_mac, ether_aton("00:00:01:01:01:11"), sizeof(struct ether_addr));
    Layer2TunnelRouteAdd(agent_->local_peer(), "vrf10", TunnelType::MplsType(), 
                         Ip4Address::from_string("10.1.1.100"),
                         1000, *remote_vm_mac, Ip4Address::from_string("1.1.1.10"), 32);
    client->WaitForIdle();
    Layer2RouteEntry *l2_rt = L2RouteGet("vrf10", *remote_vm_mac);
    EXPECT_TRUE(l2_rt != NULL);
    TunnelNHKey *tnh_key = static_cast<TunnelNHKey *>(l2_rt->GetActivePath()->
                                                      nexthop(agent_)->
                                                      GetDBRequestKey().release());
    TunnelNH *tnh = static_cast<TunnelNH*>(agent_->nexthop_table()->
                                        FindActiveEntry(tnh_key));
    EXPECT_TRUE(tnh->ToString() == "Tunnel to 10.1.1.100");
    tnh->SetKey(tnh->GetDBRequestKey().release());
    DoNextHopSandesh();
    Layer2AgentRouteTable::DeleteReq(agent_->local_peer(),
                                     "vrf10", *remote_vm_mac, NULL);
    client->WaitForIdle();

    //CompositeNHKey
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    ComponentNHKeyList component_nh_key_list;
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
    //Composite NH key
    DBRequest comp_nh_req;
    comp_nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    comp_nh_req.key.reset(new CompositeNHKey(Composite::ECMP, true,
                                             component_nh_key_list, "vrf10"));
    comp_nh_req.data.reset(new CompositeNHData());
    agent_->nexthop_table()->Enqueue(&comp_nh_req);
    client->WaitForIdle();
    CompositeNHKey find_cnh_key(Composite::ECMP, true, component_nh_key_list, "vrf10");
    EXPECT_TRUE(agent_->nexthop_table()->
                FindActiveEntry(&find_cnh_key) != NULL);
    DoNextHopSandesh();
    DBRequest del_comp_nh_req;
    del_comp_nh_req.oper = DBRequest::DB_ENTRY_DELETE;
    del_comp_nh_req.key.reset(new CompositeNHKey(Composite::ECMP, true,
                                                 component_nh_key_list,
                                                 "vrf10"));
    del_comp_nh_req.data.reset(new CompositeNHData());
    agent_->nexthop_table()->Enqueue(&del_comp_nh_req);
    client->WaitForIdle();

    //VLAN nh
    struct ether_addr *dst_vlan_mac = 
        (struct ether_addr *)malloc(sizeof(struct ether_addr));
    memcpy (dst_vlan_mac, ether_aton("00:00:01:01:01:12"), sizeof(struct ether_addr));
    struct ether_addr *src_vlan_mac = 
        (struct ether_addr *)malloc(sizeof(struct ether_addr));
    memcpy (src_vlan_mac, ether_aton("00:00:01:01:01:11"), sizeof(struct ether_addr));
    VlanNHKey *vlan_nhkey = new VlanNHKey(MakeUuid(10), 100);
    VlanNHData *vlan_nhdata = new VlanNHData("vrf10", *src_vlan_mac, *dst_vlan_mac);
    DBRequest nh_req;
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    nh_req.key.reset(vlan_nhkey);
    nh_req.data.reset(vlan_nhdata);
    agent_->nexthop_table()->Enqueue(&nh_req);
    client->WaitForIdle();
    SecurityGroupList sg_l;
    agent_->fabric_inet4_unicast_table()->AddVlanNHRouteReq(NULL, "vrf10",
                          Ip4Address::from_string("2.2.2.0"), 24, MakeUuid(10), 100, 100, 
                          "vn10", sg_l, PathPreference());
    client->WaitForIdle();
    Inet4UnicastRouteEntry *vlan_rt = 
        RouteGet("vrf10", Ip4Address::from_string("2.2.2.0"), 24);
    EXPECT_TRUE(vlan_rt != NULL);
    VlanNH *vlan_nh = static_cast<VlanNH *>(agent_->
                   nexthop_table()->FindActiveEntry(vlan_rt->
                   GetActivePath()->nexthop(agent_)->GetDBRequestKey().release()));
    EXPECT_TRUE(vlan_nh == VlanNH::Find(MakeUuid(10), 100));
    vlan_nh->SetKey(vlan_nh->GetDBRequestKey().release());

    //Sandesh request
    DoNextHopSandesh();

    agent_->fabric_inet4_unicast_table()->DeleteReq(NULL, 
                          "vrf10", Ip4Address::from_string("2.2.2.0"), 24, NULL);
    VlanNHKey *del_vlan_nhkey = new VlanNHKey(MakeUuid(10), 100);
    DBRequest del_nh_req;
    del_nh_req.oper = DBRequest::DB_ENTRY_DELETE;
    del_nh_req.key.reset(del_vlan_nhkey);
    del_nh_req.data.reset();
    agent_->nexthop_table()->Enqueue(&del_nh_req);
    client->WaitForIdle();

    //ARP NH with vm interface
    DBRequest arp_nh_req;
    arp_nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    arp_nh_req.key.reset(new ArpNHKey("vrf10", Ip4Address::from_string("11.11.11.11")));
    struct ether_addr *intf_vm_mac= 
        (struct ether_addr *)malloc(sizeof(struct ether_addr));
    memcpy(intf_vm_mac, ether_aton("00:00:01:01:01:11"), sizeof(struct ether_addr));
    VmInterfaceKey *intf_key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, 
                                              MakeUuid(10), "vrf10");
    arp_nh_req.data.reset(new ArpNHData(*intf_vm_mac, intf_key, true));
    agent_->nexthop_table()->Enqueue(&arp_nh_req);
    client->WaitForIdle();
    ArpNHKey find_arp_nh_key("vrf10", Ip4Address::from_string("11.11.11.11")); 
    ArpNH *arp_nh = static_cast<ArpNH *>
        (agent_->nexthop_table()->FindActiveEntry(&find_arp_nh_key));
    EXPECT_TRUE(arp_nh != NULL);
    EXPECT_TRUE(arp_nh->GetIfUuid() == MakeUuid(10));
    arp_nh->SetKey(arp_nh->GetDBRequestKey().release());
    DoNextHopSandesh();

    DBRequest del_arp_nh_req;
    del_arp_nh_req.oper = DBRequest::DB_ENTRY_DELETE;
    del_arp_nh_req.key.reset(new ArpNHKey("vrf10", Ip4Address::from_string("11.11.11.11")));
    del_arp_nh_req.data.reset(new ArpNHData());
    agent_->nexthop_table()->Enqueue(&del_arp_nh_req);
    client->WaitForIdle();
    ArpNHKey find_del_arp_nh_key("vrf10", Ip4Address::from_string("11.11.11.11")); 
    EXPECT_TRUE(agent_->nexthop_table()->
                FindActiveEntry(&find_del_arp_nh_key) == NULL);

    //Delete 
    agent_->fabric_inet4_unicast_table()->
        DeleteReq(agent_->local_peer(),
                  agent_->fabric_vrf_name(),
                  Ip4Address::from_string("10.1.1.100"), 32, NULL);
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf10", ip, 32));
    WAIT_FOR(1000, 1000, (VrfGet("vrf10") == NULL));
}

TEST_F(CfgTest, Nexthop_invalid_vrf) {
    client->WaitForIdle();
    client->Reset();

    //ARP NH
    DBRequest arp_nh_req;
    arp_nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    arp_nh_req.key.reset(new ArpNHKey("vrf11", Ip4Address::from_string("11.11.11.11")));
    arp_nh_req.data.reset(new ArpNHData());
    agent_->nexthop_table()->Enqueue(&arp_nh_req);
    client->WaitForIdle();
    ArpNHKey find_arp_nh_key("vrf11", Ip4Address::from_string("11.11.11.11")); 
    EXPECT_TRUE(agent_->nexthop_table()->
                FindActiveEntry(&find_arp_nh_key) == NULL);

    //Interface NH
    struct ether_addr *intf_vm_mac= 
        (struct ether_addr *)malloc(sizeof(struct ether_addr));
    memcpy(intf_vm_mac, ether_aton("00:00:01:01:01:11"), sizeof(struct ether_addr));
    VmInterfaceKey *intf_key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, 
                                              MakeUuid(11), "vrf11");
    DBRequest intf_nh_req;
    intf_nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    intf_nh_req.key.reset(new InterfaceNHKey(intf_key, true, 5));
    intf_nh_req.data.reset(new InterfaceNHData("vrf11", *intf_vm_mac));
    agent_->nexthop_table()->Enqueue(&intf_nh_req);
    client->WaitForIdle();
    VmInterfaceKey *find_intf_key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                                       MakeUuid(11), "vrf11");
    InterfaceNHKey find_intf_nh_key(find_intf_key, true, 5); 
    EXPECT_TRUE(agent_->nexthop_table()->
                FindActiveEntry(&find_intf_nh_key) == NULL);

    //VRF NH
    DBRequest vrf_nh_req;
    vrf_nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    vrf_nh_req.key.reset(new VrfNHKey("vrf11", true));
    vrf_nh_req.data.reset(new VrfNHData());
    agent_->nexthop_table()->Enqueue(&vrf_nh_req);
    client->WaitForIdle();
    VrfNHKey find_vrf_nh_key("vrf11", true);
    EXPECT_TRUE(agent_->nexthop_table()->
                FindActiveEntry(&find_vrf_nh_key) == NULL);

    //Tunnel NH
    DBRequest tnh_req;
    tnh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    tnh_req.key.reset(new TunnelNHKey("vrf11", Ip4Address::from_string("11.11.11.11"),
                                      Ip4Address::from_string("12.12.12.12"), true, 
                                      TunnelType::DefaultType()));
    tnh_req.data.reset(new TunnelNHData());
    agent_->nexthop_table()->Enqueue(&tnh_req);
    client->WaitForIdle();
    TunnelNHKey find_tnh_key("vrf11", Ip4Address::from_string("11.11.11.11"),
                             Ip4Address::from_string("12.12.12.12"), true,
                             TunnelType::DefaultType());
    EXPECT_TRUE(agent_->nexthop_table()->
                FindActiveEntry(&find_tnh_key) == NULL);

    //Receive NH
    VmInterfaceKey *recv_intf_key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                                       MakeUuid(11), "vrf11");
    DBRequest recv_nh_req;
    recv_nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    recv_nh_req.key.reset(new ReceiveNHKey(recv_intf_key, true));
    recv_nh_req.data.reset(new ReceiveNHData());
    agent_->nexthop_table()->Enqueue(&recv_nh_req);
    client->WaitForIdle();
    VmInterfaceKey *find_recv_intf_key = 
        new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                           MakeUuid(11), "vrf11");
    ReceiveNHKey find_recv_nh_key(find_recv_intf_key, true);
    EXPECT_TRUE(agent_->nexthop_table()->
                FindActiveEntry(&find_recv_nh_key) == NULL);

    //Vlan NH
    struct ether_addr *vlan_dmac = 
        (struct ether_addr *)malloc(sizeof(struct ether_addr));
    memcpy(vlan_dmac, ether_aton("00:00:01:01:01:11"), sizeof(struct ether_addr));
    struct ether_addr *vlan_smac = 
        (struct ether_addr *)malloc(sizeof(struct ether_addr));
    memcpy(vlan_smac, ether_aton("00:00:01:01:01:10"), sizeof(struct ether_addr));
    DBRequest vlan_nh_req;
    vlan_nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    vlan_nh_req.key.reset(new VlanNHKey(MakeUuid(11), 11));
    vlan_nh_req.data.reset(new VlanNHData("vrf11", *vlan_smac, *vlan_dmac));
    agent_->nexthop_table()->Enqueue(&vlan_nh_req);
    client->WaitForIdle();
    VlanNHKey find_vlan_nh_key(MakeUuid(11), 11);
    EXPECT_TRUE(agent_->nexthop_table()->
                FindActiveEntry(&find_vlan_nh_key) == NULL);

    //Composite NH
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    ComponentNHKeyList component_nh_key_list;
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
    DBRequest comp_nh_req;
    comp_nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    comp_nh_req.key.reset(new CompositeNHKey(Composite::L3COMP, false, component_nh_key_list,
                                             "vrf11"));
    comp_nh_req.data.reset(new CompositeNHData());

    agent_->nexthop_table()->Enqueue(&comp_nh_req);
    client->WaitForIdle();
    CompositeNHKey find_cnh_key(Composite::L3COMP, false, component_nh_key_list, "vrf11");
    EXPECT_TRUE(agent_->nexthop_table()->
                FindActiveEntry(&find_cnh_key) == NULL);

    //First VM added
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (VrfGet("vrf11") == NULL));
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
