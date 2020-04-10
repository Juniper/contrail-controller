/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
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

class CfgTest : public ::testing::Test {
public:
    void SetUp() {
        boost::system::error_code ec;
        bgp_peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                 "xmpp channel");
        agent_ = Agent::GetInstance();
    }
    void TearDown() {
        WAIT_FOR(1000, 1000, agent_->vrf_table()->Size() == 2);
        DeleteBgpPeer(bgp_peer_);
    }

    Agent *agent_;
    BgpPeer *bgp_peer_;
};

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

static void DoNextHopSandesh() {
    NhListReq *nh_list_req = new NhListReq();
    std::vector<int> result = {1};
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

// Verify that index-0 is not used by agent and discard nh uses index 1
TEST_F(CfgTest, DiscardNhIndex_) {
    NextHopTable *table = Agent::GetInstance()->nexthop_table();
    EXPECT_TRUE(table->FindNextHop(0) == NULL);
    NextHop *nh = table->discard_nh();
    EXPECT_NE(nh->id(), 0U);
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

//Enqueue change request for tunnel
//Since all the request are same,
//make sure tunnel NH is not notified
TEST_F(CfgTest, TunnelNh_3) {
    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
            Ip4Address::from_string("10.1.1.10"), false,
            TunnelType::AllType());
    client->WaitForIdle();

    client->NextHopReset();
    for (uint32_t i = 0; i < 10; i++) {
        CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                       Ip4Address::from_string("10.1.1.10"), false,
                       TunnelType::AllType());
        client->WaitForIdle();
    }
    EXPECT_TRUE(client->nh_notify_ == 0);
    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), true,
                   TunnelType::AllType());
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
    mirror_nh->SetKey(mirror_nh->GetDBRequestKey().get());

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

TEST_F(CfgTest, MirrorNh_2) {
    AddVrf("test_vrf");
    client->WaitForIdle();
    client->Reset();

    MirrorNHKey key
        (agent_->fabric_vrf_name(), Ip4Address::from_string("10.10.10.10"),
         10, Ip4Address::from_string("20.20.20.20"), 20);
    std::string analyzer_1 = "TestAnalyzer_1";
    MirrorTable::AddMirrorEntry(analyzer_1, agent_->fabric_vrf_name(),
            Ip4Address::from_string("10.10.10.10"), 10,
            Ip4Address::from_string("20.20.20.20"), 20);
    client->WaitForIdle();

    client->Reset();

    for (uint32_t i = 0; i < 10; i++) {
        MirrorTable::AddMirrorEntry(analyzer_1, agent_->fabric_vrf_name(),
                Ip4Address::from_string("10.10.10.10"), 10,
                Ip4Address::from_string("20.20.20.20"), 20);
        client->WaitForIdle();
    }
    EXPECT_TRUE(client->nh_notify_ == 0);

    MirrorTable::DelMirrorEntry(analyzer_1);
    client->WaitForIdle();
    DelVrf("test_vrf");
    client->WaitForIdle();
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
    std::vector<int> result = {1};
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
    ConcurrencyScope scope("db::DBTable");
    VrfAddReq("test_vrf");
    client->WaitForIdle();

    VrfEntryRef vrf(VrfGet("test_vrf"));
    vrf->CreateTableLabel(false, false, false, false);
    client->WaitForIdle();

    VrfDelReq("test_vrf");
    client->WaitForIdle();

    vrf.reset(NULL);
    client->WaitForIdle();
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
                                     false, TunnelType::MPLS_UDP) == true));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::DefaultType()));

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

TEST_F(CfgTest, Nexthop_keys) {
    client->WaitForIdle();
    struct PortInfo input1[] = {
        {"vnet10", 10, "1.1.1.1", "00:00:00:01:01:01", 10, 10}
    };
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.254", true},
    };

    AddIPAM("vn10", ipam_info, 1);
    client->WaitForIdle();

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

    InetUnicastRouteEntry *rt = RouteGet("vrf10", ip, 32);
    WAIT_FOR(1000, 1000, (rt->GetActivePath() != NULL));
    const NextHop *nh = rt->GetActivePath()->ComputeNextHop(agent_);
    EXPECT_TRUE(nh != NULL);
    WAIT_FOR(100, 1000, (rt->GetActiveNextHop()->GetType() ==
                         NextHop::INTERFACE));

    //Sandesh request
    DoNextHopSandesh();

    //Issue set for nexthop key
    nh = rt->GetActiveNextHop();
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);

    DBEntryBase::KeyPtr nh_key_base(intf_nh->GetDBRequestKey());
    NextHopKey *nh_key = static_cast<NextHopKey *>(nh_key_base.get());
    EXPECT_TRUE(nh_key->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(nh_key->GetPolicy());
    NextHop *base_nh = static_cast<NextHop *>
        (agent_-> nexthop_table()->FindActiveEntry(nh_key));
    base_nh->SetKey(base_nh->GetDBRequestKey().get());

    //Issue set for interface nexthop key
    InterfaceNH *interface_nh = static_cast<InterfaceNH *>(base_nh);
    interface_nh->SetKey(static_cast<InterfaceNHKey *>(nh_key));

    //Issue set for VRF nexthop key
    VxLanIdKey vxlan_key(10);
    VxLanId *vxlan_id_entry = static_cast<VxLanId *>
        (agent_->vxlan_table()->FindActiveEntry(&vxlan_key));

    DBEntryBase::KeyPtr vrf_nh_key_base(vxlan_id_entry->nexthop()->GetDBRequestKey());
    VrfNHKey *vrf_nh_key = static_cast<VrfNHKey *>(vrf_nh_key_base.get());
    VrfNH *vrf_nh = static_cast<VrfNH*>
        (agent_->nexthop_table()->FindActiveEntry(vrf_nh_key));
    vrf_nh->SetKey(vrf_nh_key);
    EXPECT_TRUE(vrf_nh->bridge_nh() == true);
    DoNextHopSandesh();

    VmInterfaceKey vhost_intf_key(AgentKey::ADD_DEL_CHANGE,
                                  boost::uuids::nil_uuid(),
                                  agent_->vhost_interface()->name());
    //Tunnel NH key
    agent_->
        fabric_inet4_unicast_table()->
        AddResolveRoute(agent_->local_peer(), agent_->fabric_vrf_name(),
                        Ip4Address::from_string("10.1.1.100"), 32,
                        vhost_intf_key, 0, false, "", SecurityGroupList(),
                        TagList());
    client->WaitForIdle();

    Ip4Address vm_ip = Ip4Address::from_string("1.1.1.10");
    MacAddress remote_vm_mac("00:00:01:01:01:11");
    BridgeTunnelRouteAdd(bgp_peer_, "vrf10", TunnelType::MplsType(),
                         Ip4Address::from_string("10.1.1.100"),
                         1000, remote_vm_mac, vm_ip, 32);
    client->WaitForIdle();
    BridgeRouteEntry *l2_rt = L2RouteGet("vrf10", remote_vm_mac);
    EXPECT_TRUE(l2_rt != NULL);
    const NextHop *l2_rt_nh = l2_rt->GetActivePath()->ComputeNextHop(agent_);

    DBEntryBase::KeyPtr tnh_key_base(l2_rt_nh->GetDBRequestKey());
    TunnelNHKey *tnh_key = static_cast<TunnelNHKey *>(tnh_key_base.get());
    TunnelNH *tnh = static_cast<TunnelNH*>(agent_->nexthop_table()->
                                        FindActiveEntry(tnh_key));
    EXPECT_TRUE(tnh->ToString() ==
                "Tunnel to 10.1.1.100 rewrite mac 00:00:00:00:00:00");
    tnh->SetKey(tnh->GetDBRequestKey().get());
    DoNextHopSandesh();
    EvpnAgentRouteTable::DeleteReq(agent_->local_peer(),
                                     "vrf10", remote_vm_mac, IpAddress(vm_ip),
                                     32, 0, NULL);
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
    MacAddress dst_vlan_mac("00:00:01:01:01:12");
    MacAddress src_vlan_mac("00:00:01:01:01:11");
    VlanNHKey *vlan_nhkey = new VlanNHKey(MakeUuid(10), 100);
    VlanNHData *vlan_nhdata = new VlanNHData("vrf10", src_vlan_mac, dst_vlan_mac);
    DBRequest nh_req;
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    nh_req.key.reset(vlan_nhkey);
    nh_req.data.reset(vlan_nhdata);
    agent_->nexthop_table()->Enqueue(&nh_req);
    client->WaitForIdle();
    SecurityGroupList sg_l;
    VnListType vn_list;
    vn_list.insert("vn10");
    agent_->fabric_inet4_unicast_table()->AddVlanNHRouteReq
        (agent_->local_peer(), "vrf10", Ip4Address::from_string("2.2.2.0"), 24,
         MakeUuid(10), 100, 100, vn_list, sg_l, TagList(), PathPreference());
    client->WaitForIdle();
    InetUnicastRouteEntry *vlan_rt =
        RouteGet("vrf10", Ip4Address::from_string("2.2.2.0"), 24);
    EXPECT_TRUE(vlan_rt != NULL);
    VlanNH *vlan_nh = static_cast<VlanNH *>(agent_->
                   nexthop_table()->FindActiveEntry(vlan_rt->
                   GetActivePath()->ComputeNextHop(agent_)->GetDBRequestKey().get()));
    EXPECT_TRUE(vlan_nh == VlanNH::Find(MakeUuid(10), 100));
    vlan_nh->SetKey(vlan_nh->GetDBRequestKey().get());

    //Sandesh request
    DoNextHopSandesh();

    agent_->fabric_inet4_unicast_table()->DeleteReq(agent_->local_peer(),
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
    arp_nh_req.key.reset(new ArpNHKey("vrf10", Ip4Address::from_string("11.11.11.11"),
                                      false));
    MacAddress intf_vm_mac("00:00:01:01:01:11");
    VmInterfaceKey *intf_key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                              MakeUuid(10), "vrf10");
    arp_nh_req.data.reset(new ArpNHData(intf_vm_mac, intf_key, true));
    agent_->nexthop_table()->Enqueue(&arp_nh_req);
    client->WaitForIdle();
    ArpNHKey find_arp_nh_key("vrf10", Ip4Address::from_string("11.11.11.11"),
                             false);
    ArpNH *arp_nh = static_cast<ArpNH *>
        (agent_->nexthop_table()->FindActiveEntry(&find_arp_nh_key));
    EXPECT_TRUE(arp_nh != NULL);
    EXPECT_TRUE(arp_nh->GetIfUuid() == MakeUuid(10));
    arp_nh->SetKey(arp_nh->GetDBRequestKey().get());
    DoNextHopSandesh();

    DBRequest del_arp_nh_req;
    del_arp_nh_req.oper = DBRequest::DB_ENTRY_DELETE;
    del_arp_nh_req.key.reset(new ArpNHKey("vrf10", Ip4Address::from_string("11.11.11.11"),
                                          false));
    del_arp_nh_req.data.reset(NULL);
    agent_->nexthop_table()->Enqueue(&del_arp_nh_req);
    client->WaitForIdle();
    ArpNHKey find_del_arp_nh_key("vrf10", Ip4Address::from_string("11.11.11.11"),
                                 false);
    EXPECT_TRUE(agent_->nexthop_table()->
                FindActiveEntry(&find_del_arp_nh_key) == NULL);

    //Delete
    agent_->fabric_inet4_unicast_table()->DeleteReq
        (bgp_peer_, agent_->fabric_vrf_name(),
         Ip4Address::from_string("10.1.1.100"), 32, NULL);
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf10", ip, 32));
    WAIT_FOR(1000, 1000, (VrfGet("vrf10") == NULL));
    DelIPAM("vn10");
    client->WaitForIdle();
}

TEST_F(CfgTest, Nexthop_invalid_vrf) {
    client->WaitForIdle();
    client->Reset();

    InetInterfaceKey *vhost_intf_key =
        new InetInterfaceKey(agent_->vhost_interface()->name());
    //ARP NH
    DBRequest arp_nh_req;
    arp_nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    arp_nh_req.key.reset(new ArpNHKey("vrf11", Ip4Address::from_string("11.11.11.11"),
                                      false));
    arp_nh_req.data.reset(new ArpNHData(vhost_intf_key));
    agent_->nexthop_table()->Enqueue(&arp_nh_req);
    client->WaitForIdle();
    ArpNHKey find_arp_nh_key("vrf11", Ip4Address::from_string("11.11.11.11"),
                             false);
    EXPECT_TRUE(agent_->nexthop_table()->
                FindActiveEntry(&find_arp_nh_key) == NULL);

    //Interface NH
    MacAddress intf_vm_mac("00:00:01:01:01:11");
    VmInterfaceKey *intf_key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                              MakeUuid(11), "vrf11");
    DBRequest intf_nh_req;
    intf_nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    intf_nh_req.key.reset(new InterfaceNHKey(intf_key, true, 5, intf_vm_mac));
    intf_nh_req.data.reset(new InterfaceNHData("vrf11"));
    agent_->nexthop_table()->Enqueue(&intf_nh_req);
    client->WaitForIdle();
    VmInterfaceKey *find_intf_key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                                       MakeUuid(11), "vrf11");
    InterfaceNHKey find_intf_nh_key(find_intf_key, true, 5, intf_vm_mac);
    EXPECT_TRUE(agent_->nexthop_table()->
                FindActiveEntry(&find_intf_nh_key) == NULL);

    //VRF NH
    DBRequest vrf_nh_req;
    vrf_nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    vrf_nh_req.key.reset(new VrfNHKey("vrf11", true, false));
    vrf_nh_req.data.reset(new VrfNHData(false, false, false));
    agent_->nexthop_table()->Enqueue(&vrf_nh_req);
    client->WaitForIdle();
    VrfNHKey find_vrf_nh_key("vrf11", true, false);
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
    MacAddress vlan_dmac("00:00:01:01:01:11");
    MacAddress vlan_smac("00:00:01:01:01:10");
    DBRequest vlan_nh_req;
    vlan_nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    vlan_nh_req.key.reset(new VlanNHKey(MakeUuid(11), 11));
    vlan_nh_req.data.reset(new VlanNHData("vrf11", vlan_smac, vlan_dmac));
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

//Delete local ecmp mpls label by
//deleting all local interfaces, add them
//back with BGP path still retaining old local ecmp
//mpls and ensure there is no crash
TEST_F(CfgTest, EcmpNH_18) {
    //Add interface
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
    };
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.254", true},
    };

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    CreateVmportWithEcmp(input, 2);
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    InetUnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    const NextHop *nh = rt->GetActiveNextHop();

    //Create component NH list
    //Transition remote VM route to ECMP route
    DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
    std::auto_ptr<const NextHopKey> nh_akey(nh_key);
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(rt->GetActiveLabel(), nh_akey));

    ComponentNHKeyList comp_nh_list;
    //Insert new NH first and then existing route NH
    comp_nh_list.push_back(nh_data1);

    SecurityGroupList sg_list;
    EcmpTunnelRouteAdd(bgp_peer_, "vrf1", ip, 32,
            comp_nh_list, false, "vn1", sg_list, TagList(), PathPreference());
    client->WaitForIdle();

    DeleteVmportEnv(input, 2, false);
    client->WaitForIdle();

    struct PortInfo input1[] = {
        {"vnet3", 3, "1.1.1.3", "00:00:00:01:01:01", 1, 3},
        {"vnet4", 4, "1.1.1.4", "00:00:00:01:01:01", 1, 4},
    };
    CreateVmportWithEcmp(input1, 2, 1);
    client->WaitForIdle();
    //Add back the VM interface of ECMP
    //This would result ing resync of route, due to policy change
    CreateVmportWithEcmp(input, 1, 1);
    client->WaitForIdle();
    const VmInterface *vm_intf =
        static_cast<const VmInterface *>(VmPortGet(1));

    VnListType vn_list;
    vn_list.insert("vn1");
    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(bgp_peer_, "vrf1", ip, 32,
                MakeUuid(1), vn_list, vm_intf->label(),
                SecurityGroupList(), TagList(), CommunityList(),
                false, PathPreference(),
                Ip4Address(0), EcmpLoadBalance(), false, false, false);
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);

    DeleteVmportEnv(input, 1, false);
    DeleteVmportEnv(input1, 2, true);
    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));
    DelIPAM("vn1");
    client->WaitForIdle();
}

TEST_F(CfgTest, EcmpNH_19) {
    //Add interface
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
    };
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.254", true},
    };

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    CreateVmportWithEcmp(input, 2);
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    InetUnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);

    //Update the SG for path
    AddSg("sg1", 1);
    AddAcl("acl1", 1);
    AddLink("security-group", "sg1", "access-control-list", "acl1");
    AddLink("virtual-machine-interface", "vnet1", "security-group", "sg1");
    client->WaitForIdle();

    EXPECT_TRUE(rt->GetActivePath()->sg_list().empty() == false);

    DelLink("virtual-machine-interface", "vnet1", "security-group", "sg1");
    DelLink("security-group", "sg1", "access-control-list", "acl1");
    DelAcl("acl1");
    DelNode("security-group", "sg1");
    client->WaitForIdle();

    EXPECT_TRUE(rt->GetActivePath()->sg_list().empty() == true);
    DeleteVmportEnv(input, 1, true);
    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));
    DelIPAM("vn1");
    client->WaitForIdle();
}

TEST_F(CfgTest, EcmpNH_20) {
    //Add interface
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.3", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.4", "00:00:00:01:01:01", 1, 2},
    };
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.254", true},
    };

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    CreateVmportWithEcmp(input, 2);
    client->WaitForIdle();

    AddServiceInstanceIp("instance1", 100, "1.1.1.10", false, NULL);
    AddLink("virtual-machine-interface", "vnet1", "instance-ip", "instance1");
    AddServiceInstanceIp("instance2", 101, "1.1.1.10", false, NULL);
    AddLink("virtual-machine-interface", "vnet2", "instance-ip", "instance2");
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("1.1.1.10");
    InetUnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);

    AddServiceInstanceIp("instance1", 100, "1.1.1.10", true, NULL);
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);

    AddServiceInstanceIp("instance2", 101, "1.1.1.10", true, NULL);
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    AddServiceInstanceIp("instance2", 101, "1.1.1.10", false, NULL);
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);

    DelLink("virtual-machine-interface", "vnet1", "instance-ip", "instance1");
    DelLink("virtual-machine-interface", "vnet2", "instance-ip", "instance2");
    DelNode("instance-ip", "instance1");
    DelNode("instance-ip", "instance2");
    client->WaitForIdle();
    DeleteVmportEnv(input, 2, true);
    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));
    DelIPAM("vn1");
    client->WaitForIdle();
}

TEST_F(CfgTest, mcast_comp_nh_encap_change) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.3", "00:00:00:01:01:01", 1, 1},
    };
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.254", true},
    };

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    CreateVmportWithEcmp(input, 1);
    client->WaitForIdle();

    MulticastHandler *mc_handler =
        static_cast<MulticastHandler *>(agent_->
                                        oper_db()->multicast());
    TunnelOlist tor_olist;
    tor_olist.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 10,
                                     IpAddress::from_string("8.8.8.8").to_v4(),
                                     TunnelType::VxlanType()));
    mc_handler->ModifyTorMembers(bgp_peer_,
                                 "vrf1",
                                 tor_olist,
                                 10,
                                 1);
    client->WaitForIdle();
    TunnelOlist evpn_olist;
    evpn_olist.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 10,
                                     IpAddress::from_string("8.8.8.8").to_v4(),
                                     TunnelType::MplsType()));
    mc_handler->ModifyEvpnMembers(bgp_peer_,
                                 "vrf1",
                                 evpn_olist,
                                 0,
                                 1);
    client->WaitForIdle();

    BridgeRouteEntry *l2_rt =
        L2RouteGet("vrf1",
                   MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                   Ip4Address(0));
    CompositeNH *cnh =
        dynamic_cast<CompositeNH *>(l2_rt->GetActivePath()->nexthop());
    EXPECT_TRUE(cnh != NULL);
    AddEncapList("MPLSoUDP", "MPLSoGRE", "VXLAN");
    client->WaitForIdle();

    CompositeNH *cnh_2 =
        dynamic_cast<CompositeNH *>(l2_rt->GetActivePath()->nexthop());
    for (ComponentNHList::const_iterator it = cnh_2->component_nh_list().begin();
         it != cnh_2->component_nh_list().end(); it++) {
        ASSERT_FALSE((*it) == NULL);
    }

    DeleteVmportEnv(input, 1, true);
    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
}

TEST_F(CfgTest, NhUsageLimit) {
    VrLimitExceeded &vr_limits = agent_->get_vr_limits_exceeded_map();
    VrLimitExceeded::iterator vr_limit_itr = vr_limits.find("vr_nexthops");

    // Since 512k is default limit, so currently usage is normal
    EXPECT_EQ(vr_limit_itr->second, "Normal");

    uint32_t default_nh_count = agent_->vrouter_max_nexthops();
    agent_->set_vrouter_max_nexthops(46);

    uint32_t default_high_watermark = agent_->vr_limit_high_watermark();
    agent_->set_vr_limit_high_watermark(65);

    uint32_t default_low_watermark = agent_->vr_limit_low_watermark();
    agent_->set_vr_limit_low_watermark(50);

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2},
    };
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.254", true},
    };

    struct PortInfo input1[] = {
        {"vnet21", 11, "2.2.2.1", "00:00:00:01:02:01", 2, 11},
        {"vnet22", 12, "2.2.2.2", "00:00:00:01:02:02", 2, 12},
    };
    IpamInfo ipam_info1[] = {
        {"2.2.2.0", 24, "2.2.2.254", true},
    };

    AddIPAM("vn1", ipam_info, 1);
    AddIPAM("vn2", ipam_info1, 1);
    client->WaitForIdle();
    CreateVmportEnv(input, 2);
    CreateVmportEnv(input1, 2);
    client->WaitForIdle();

    // usage is 48 usage should be set to TableLimit
    WAIT_FOR(100, 100, (agent_->nexthop_table()->NhIndexCount() >= 48));
    EXPECT_EQ(vr_limit_itr->second, "TableLimit");

    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();

    // TableLimit is not reset unless, count goes below 43 (95% of table size)
    WAIT_FOR(100, 100, (agent_->nexthop_table()->NhIndexCount() >= 43));
    EXPECT_EQ(vr_limit_itr->second, "TableLimit");

    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();

    // high watermark is 29 entires, Exceeded is set
    WAIT_FOR(100, 100, (agent_->nexthop_table()->NhIndexCount() >= 34));
    EXPECT_EQ(vr_limit_itr->second, "Exceeded");

    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();

    // usage not below low watermark (23), Exceeded is set
    WAIT_FOR(100, 100, (agent_->nexthop_table()->NhIndexCount() >= 24));
    EXPECT_EQ(vr_limit_itr->second, "Exceeded");

    DeleteVmportEnv(input1, 2, true);
    client->WaitForIdle();

    DelIPAM("vn1");
    client->WaitForIdle();

    DelIPAM("vn2");
    client->WaitForIdle();

    // below low watermark , nh usage is Normal
    WAIT_FOR(100, 100, (agent_->nexthop_table()->NhIndexCount() < 23));
    EXPECT_EQ(vr_limit_itr->second, "Normal");

    // Restore defaults
    agent_->set_vrouter_max_nexthops(default_nh_count);
    agent_->set_vr_limit_high_watermark(default_high_watermark);
    agent_->set_vr_limit_low_watermark(default_low_watermark);
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
