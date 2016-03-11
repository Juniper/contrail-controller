/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <netinet/in.h>
#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "pkt/flow_mgmt.h"
#include "pkt/flow_mgmt_dbclient.h"
#include <algorithm>

#define vm1_ip "1.1.1.1"
#define vm2_ip "1.1.1.2"

struct PortInfo input[] = {
        {"vif0", 1, vm1_ip, "00:00:00:01:01:01", 1, 1},
        {"vif1", 2, vm2_ip, "00:00:00:01:01:02", 1, 2},
};

class FlowMgmtRouteTest : public ::testing::Test {
public:
    FlowMgmtRouteTest() : peer_(NULL), agent_(Agent::GetInstance()) {
        flow_proto_ = agent_->pkt()->get_flow_proto();
        flow_mgmt_ = agent_->pkt()->flow_mgmt_manager();
        eth = EthInterfaceGet("vnet0");
        EXPECT_TRUE(eth != NULL);
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        WAIT_FOR(100, 100, (flow_proto_->FlowCount() == 0));
    }

protected:
    virtual void SetUp() {
        WAIT_FOR(100, 100, (flow_proto_->FlowCount() == 0));
        client->Reset();

        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(5);

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));

        vif0 = VmInterfaceGet(input[0].intf_id);
        assert(vif0);
        vif1 = VmInterfaceGet(input[1].intf_id);
        assert(vif1);

        client->WaitForIdle();
        peer_ = CreateBgpPeer(Ip4Address(1), "BGP Peer 1");

        client->WaitForIdle();
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();

        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle(3);

        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));

        EXPECT_EQ(0U, agent()->vm_table()->Size());
        EXPECT_EQ(0U, agent()->vn_table()->Size());
        EXPECT_EQ(0U, agent()->acl_table()->Size());
        DeleteBgpPeer(peer_);
    }

    int CreateMpls(int tag) {
        int label = agent_->mpls_table()->AllocLabel();
        MplsLabel::CreateVlanNh(agent_, label, MakeUuid(100), tag);
        return label;
    }

    void DeleteMpls(unsigned long label) {
        MplsLabel::DeleteReq(agent_, label);
    }

    void CreateTunnelNH(unsigned long addr) {
        TunnelType::TypeBmap bmap = TunnelType::AllType();
        DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
        req.key.reset(new TunnelNHKey(agent_->fabric_vrf_name(),
                                      agent_->router_id(),
                                      Ip4Address(addr),
                                      true, TunnelType::ComputeType(bmap)));
        req.data.reset(new TunnelNHData());
        Agent::GetInstance()->nexthop_table()->Enqueue(&req);
    }

    void DeleteTunnelNH(unsigned long addr) {
        TunnelType::TypeBmap bmap = TunnelType::AllType();
        DBRequest req(DBRequest::DB_ENTRY_DELETE);
        req.key.reset(new TunnelNHKey(agent_->fabric_vrf_name(),
                                      agent_->router_id(),
                                      Ip4Address(addr),
                                      true, TunnelType::ComputeType(bmap)));
        req.data.reset(new TunnelNHData());
        Agent::GetInstance()->nexthop_table()->Enqueue(&req);
    }

    Agent *agent() {return agent_;}

protected:
    BgpPeer *peer_;
    Agent *agent_;
    FlowProto *flow_proto_;
    FlowMgmtManager *flow_mgmt_;
    VmInterface *vif0;
    VmInterface *vif1;
    PhysicalInterface *eth;
};

TEST_F(FlowMgmtRouteTest, RouteDelete_1) {
    EXPECT_EQ(0U, flow_proto_->FlowCount());

    boost::system::error_code ec;
    Ip4Address remote_subnet = Ip4Address::from_string("10.10.10.0", ec);
    Ip4Address remote_ip = Ip4Address::from_string("10.10.10.1", ec);
    Ip4Address remote_compute = Ip4Address::from_string("1.1.1.1", ec);

    string vrf_name = vif0->vrf()->GetName();
    string vn_name = vif0->vn()->GetName();
    Inet4TunnelRouteAdd(peer_, vrf_name, remote_subnet, 24, remote_compute,
                        TunnelType::AllType(), 10, vn_name, SecurityGroupList(),
                        PathPreference());
    client->WaitForIdle();

    char router_id[80];
    strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());
    TxIpMplsPacket(eth->id(), remote_compute.to_string().c_str(),
                   router_id, vif0->label(), remote_ip.to_string().c_str(),
                   vm1_ip, 1, 1);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow = FlowGet(vrf_id, remote_ip.to_string().c_str(),
                              vm1_ip, 1, 0, 0, vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);

    InetUnicastRouteKey key(peer_, vrf_name, remote_subnet, 24);
    InetUnicastAgentRouteTable *table =
        vif0->vrf()->GetInet4UnicastRouteTable();
    AgentRoute *rt = table->FindRoute(remote_ip);

    flow_mgmt_->DeleteEvent(rt, 0xFFFFFFFF);
    client->WaitForIdle();

    DeleteRoute(vrf_name.c_str(), remote_subnet.to_string().c_str(), 24, peer_);
}

TEST_F(FlowMgmtRouteTest, RouteDelete_2) {
    EXPECT_EQ(0U, flow_proto_->FlowCount());

    boost::system::error_code ec;
    Ip4Address remote_subnet = Ip4Address::from_string("10.10.10.0", ec);
    Ip4Address remote_ip = Ip4Address::from_string("10.10.10.1", ec);
    Ip4Address remote_compute = Ip4Address::from_string("1.1.1.1", ec);

    string vrf_name = vif0->vrf()->GetName();
    string vn_name = vif0->vn()->GetName();
    Inet4TunnelRouteAdd(peer_, vrf_name, remote_subnet, 24, remote_compute,
                        TunnelType::AllType(), 10, vn_name, SecurityGroupList(),
                        PathPreference());
    client->WaitForIdle();

    char router_id[80];
    strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());
    TxIpMplsPacket(eth->id(), remote_compute.to_string().c_str(),
                   router_id, vif0->label(), remote_ip.to_string().c_str(),
                   vm1_ip, 1, 1);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow = FlowGet(vrf_id, remote_ip.to_string().c_str(),
                              vm1_ip, 1, 0, 0, vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);

    InetUnicastRouteKey key(peer_, vrf_name, remote_subnet, 24);
    InetUnicastAgentRouteTable *table =
        vif0->vrf()->GetInet4UnicastRouteTable();
    AgentRoute *rt = table->FindRoute(remote_ip);

    flow_mgmt_->DeleteEvent(flow);
    client->WaitForIdle();

    flow_mgmt_->DeleteEvent(rt, 0xFFFFFFFF);
    client->WaitForIdle();

    DeleteRoute(vrf_name.c_str(), remote_subnet.to_string().c_str(), 24, peer_);
}

TEST_F(FlowMgmtRouteTest, RouteDelete_3) {
    EXPECT_EQ(0U, flow_proto_->FlowCount());

    boost::system::error_code ec;
    Ip4Address remote_subnet = Ip4Address::from_string("10.10.10.0", ec);
    Ip4Address remote_ip = Ip4Address::from_string("10.10.10.1", ec);
    Ip4Address remote_compute = Ip4Address::from_string("1.1.1.1", ec);

    string vrf_name = vif0->vrf()->GetName();
    string vn_name = vif0->vn()->GetName();
    Inet4TunnelRouteAdd(peer_, vrf_name, remote_subnet, 24, remote_compute,
                        TunnelType::AllType(), 10, vn_name, SecurityGroupList(),
                        PathPreference());
    client->WaitForIdle();

    char router_id[80];
    strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());
    TxIpMplsPacket(eth->id(), remote_compute.to_string().c_str(),
                   router_id, vif0->label(), remote_ip.to_string().c_str(),
                   vm1_ip, 1, 1);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow = FlowGet(vrf_id, remote_ip.to_string().c_str(),
                              vm1_ip, 1, 0, 0, vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);

    flow_mgmt_->DeleteEvent(flow);
    client->WaitForIdle();

    DeleteRoute(vrf_name.c_str(), remote_subnet.to_string().c_str(), 24, peer_);
    client->WaitForIdle();
}

TEST_F(FlowMgmtRouteTest, RouteDelete_4) {
    EXPECT_EQ(0U, flow_proto_->FlowCount());

    boost::system::error_code ec;
    Ip4Address remote_subnet = Ip4Address::from_string("10.10.10.0", ec);
    Ip4Address remote_ip = Ip4Address::from_string("10.10.10.1", ec);
    Ip4Address remote_compute = Ip4Address::from_string("1.1.1.1", ec);
    char router_id[80];
    strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());

    string vrf_name = vif0->vrf()->GetName();
    string vn_name = vif0->vn()->GetName();
    Inet4TunnelRouteAdd(peer_, vrf_name, remote_subnet, 24, remote_compute,
                        TunnelType::AllType(), 10, vn_name, SecurityGroupList(),
                        PathPreference());
    client->WaitForIdle();

    TxIpMplsPacket(eth->id(), remote_compute.to_string().c_str(),
                   router_id, vif0->label(), remote_ip.to_string().c_str(),
                   vm1_ip, 1, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vif0->vrf_id(), remote_ip.to_string().c_str(),
                              vm1_ip, 1, 0, 0, vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);

    FlowMgmtManager *mgr = agent_->pkt()->flow_mgmt_manager();
    flow_proto_->DisableFlowEventQueue(0, true);

    VrfDelReq("vrf1");
    client->WaitForIdle(10);

    flow_proto_->DisableFlowMgmtQueue(true);
    flow_mgmt_->DeleteEvent(flow);
    flow_mgmt_->DeleteEvent(flow->reverse_flow_entry());
    flow_mgmt_->AddEvent(flow);
    flow_mgmt_->AddEvent(flow->reverse_flow_entry());
    client->WaitForIdle();

    DeleteVmportEnv(input, 3, true, 1);
    client->WaitForIdle(3);

    flow_proto_->DisableFlowMgmtQueue(false);
    mgr->DisableWorkQueue(false);
    client->WaitForIdle(10);

    flow_proto_->DisableFlowEventQueue(0, false);
    client->WaitForIdle(10);
}

TEST_F(FlowMgmtRouteTest, RouteDelete_5) {
    VrfAddReq("vrf10");
    client->WaitForIdle();

    boost::system::error_code ec;
    Ip4Address remote_ip1 = Ip4Address::from_string("0.0.0.0", ec);
    Ip4Address remote_compute = Ip4Address::from_string("1.1.1.1", ec);
    char router_id[80];
    strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());

    string vn_name = "vn10";
    Inet4TunnelRouteAdd(agent_->local_peer(), "vrf10", remote_ip1, 0,
                        remote_compute, TunnelType::AllType(), 10,
                        vn_name, SecurityGroupList(),
                        PathPreference());
    Inet4TunnelRouteAdd(agent_->local_peer(), "vrf10", remote_compute, 24,
                        remote_compute, TunnelType::AllType(), 10,
                        vn_name, SecurityGroupList(), PathPreference());
    client->WaitForIdle();

    VrfDelReq("vrf10");
    client->WaitForIdle();
    DeleteRoute("vrf10", "1.1.1.1",  24, agent_->local_peer());
    client->WaitForIdle();
    DeleteRoute("vrf10", "0.0.0.0",  0, agent_->local_peer());
    client->WaitForIdle();
}

////////////////////////////////////////////////////////////////////////////
// UT for bug 1551577
// Simulate the following scenario,
// 1. Delete a DBEntry and enqueue DELETE_DBENTRY event
// 2. Renew the DBEntry and enqueue ADD_DBENTRY
// 3. Delete the DBEntry and not notify DELETE
// 4. Process the DELETE_DBENTRY in (1)
// 5. Free DBState
// 6. Process the DELETE in (3) and let the DBEntry get freed
// 7. Process the ADD_DBENTRY from (2).
//    Agent asserts since its trying to process deleted DBEntry
////////////////////////////////////////////////////////////////////////////
TEST_F(FlowMgmtRouteTest, DB_Entry_Reuse) {
#define vm3_ip  "1.1.1.3"
    unsigned long addr = Ip4Address::from_string("100.100.100.1").to_ulong();

    // Create TunnelNH
    CreateTunnelNH(addr);
    client->WaitForIdle();

    // Create some MPLS Labels. They will be used to delay the DB-notification
    // in later stages
    ConcurrencyScope scope("db::DBTable");
    int label_list[2000];
    // Enqueue requests into flow-mgmt queue
    for (int i = 100; i < 1000; i++) {
        label_list[i] = CreateMpls(i);
    }
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (flow_mgmt_->FlowUpdateQueueLength() == 0));

    // Disable flow-management queue
    flow_mgmt_->FlowUpdateQueueDisable(true);

    // Delete the NH. This should trigger clear of DBState
    DeleteTunnelNH(addr);
    client->WaitForIdle();

    // Make dummy enqueues so that subsequent operation on tunnel-nh are
    // delayed
    for (int i = 0; i < 100000; i++) {
        flow_mgmt_->DummyEvent();
    }

    // Revoke the tunnel-nh and delete it again
    CreateTunnelNH(addr);
    client->WaitForIdle();

    DeleteTunnelNH(addr);

    // Delay notification tunnel-nh creation above to ensure the DBState
    // for NH is cleared by flow-mgmt module
    for (int i = 100; i < 1000; i++) {
        DeleteMpls(label_list[i]);
    }

    // Enable flow-mgmt queue
    flow_mgmt_->FlowUpdateQueueDisable(false);
    WAIT_FOR(1000, 1000, (flow_mgmt_->FlowUpdateQueueLength() == 0));
}

int main(int argc, char *argv[]) {
    int ret = 0;

    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    ret = RUN_ALL_TESTS();
    usleep(100000);
    TestShutdown();
    delete client;
    return ret;
}
