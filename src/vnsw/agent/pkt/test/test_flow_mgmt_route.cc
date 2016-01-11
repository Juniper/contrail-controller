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

        DeleteVmportEnv(input, 3, true, 1);
        client->WaitForIdle(3);

        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));

        EXPECT_EQ(0U, agent()->vm_table()->Size());
        EXPECT_EQ(0U, agent()->vn_table()->Size());
        EXPECT_EQ(0U, agent()->acl_table()->Size());
        DeleteBgpPeer(peer_);
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
