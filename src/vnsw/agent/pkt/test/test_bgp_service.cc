/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include "pkt/flow_mgmt.h"
#include "pkt/pkt_handler.h"
#include <base/task.h>
#include <base/test/task_test_util.h>
#include <oper/bgp_as_service.h>

VmInterface *vnet[16];
InetInterface *vhost;
char vhost_addr[32];
char vnet_addr[16][32];

PhysicalInterface *eth;
int hash_id;

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:01", 1, 1},
    {"vnet2", 2, "2.2.2.20", "00:00:02:02:02:02", 2, 2},
    {"vnet3", 3, "3.3.3.30", "00:00:03:03:03:03", 3, 3},
    {"vnet4", 4, "4.4.4.40", "00:00:04:04:04:04", 4, 4},
    {"vnet5", 5, "5.5.5.50", "00:00:05:05:05:05", 5, 5},
    {"vnet6", 6, "6.6.6.60", "00:00:06:06:06:06", 6, 6},
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.1", true},
    {"2.2.2.0", 24, "2.2.2.2", true},
    {"3.3.3.0", 24, "3.3.3.3", true},
    {"4.4.4.0", 24, "4.4.4.4", true},
    {"5.5.5.0", 24, "5.5.5.5", true},
    {"6.6.6.0", 24, "6.6.6.6", true},
};

class BgpServiceTest : public ::testing::Test {
public:
    void AddAap(std::string intf_name, int intf_id, Ip4Address ip,
                const std::string &mac) {
        std::ostringstream buf;
        buf << "<virtual-machine-interface-allowed-address-pairs>";
        buf << "<allowed-address-pair>";
        buf << "<ip>";
        buf << "<ip-prefix>" << ip.to_string() <<"</ip-prefix>";
        buf << "<ip-prefix-len>"<< 32 << "</ip-prefix-len>";
        buf << "</ip>";
        buf << "<mac>" << mac << "</mac>";
        buf << "<flag>" << "act-stby" << "</flag>";
        buf << "</allowed-address-pair>";
        buf << "</virtual-machine-interface-allowed-address-pairs>";
        char cbuf[10000];
        strcpy(cbuf, buf.str().c_str());
        AddNode("virtual-machine-interface", intf_name.c_str(),
                intf_id, cbuf);
        client->WaitForIdle();
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
        AgentParam *params = agent_->params();
        params->set_bgpaas_max_shared_sessions(4);
        AddBgpaasPortRange(50000, 50512);
        client->WaitForIdle();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        CreateVmportEnv(input, 6);
        AddIPAM("vn1", &ipam_info[0], 1);
        AddIPAM("vn2", &ipam_info[1], 1);
        AddIPAM("vn3", &ipam_info[2], 1);
        AddIPAM("vn4", &ipam_info[3], 1);
        AddIPAM("vn5", &ipam_info[4], 1);
        AddIPAM("vn6", &ipam_info[5], 1);
        client->WaitForIdle();
        SendBgpServiceConfig("1.1.1.10", 50000, 1, "vnet1",
                             "vrf1", "bgpaas-client",
                             false, true);
        SendBgpServiceConfig("2.2.2.20", 50000, 2, "vnet2",
                             "vrf2", "bgpaas-client",
                             false, true);
        SendBgpServiceConfig("3.3.3.30", 50000, 3, "vnet3",
                             "vrf3", "bgpaas-client",
                             false, true);
        SendBgpServiceConfig("4.4.4.40", 50001, 4, "vnet4",
                             "vrf4", "bgpaas-client",
                             false, true);
        SendBgpServiceConfig("5.5.5.50", 50001, 5, "vnet5",
                             "vrf5", "bgpaas-client",
                             false, true);
        SendBgpServiceConfig("6.6.6.60", 50001, 6, "vnet6",
                             "vrf6", "bgpaas-client",
                             false, true);
        client->WaitForIdle();
        EXPECT_TRUE(agent_->oper_db()->bgp_as_a_service()->IsConfigured());
    }

    virtual void TearDown() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        client->WaitForIdle();
        SendBgpServiceConfig("1.1.1.10", 50000, 1, "vnet1",
                             "vrf1", "bgpaas-server",
                             true, true);
        SendBgpServiceConfig("2.2.2.20", 50000, 2, "vnet2",
                             "vrf2", "bgpaas-server",
                             true, true);
        SendBgpServiceConfig("3.3.3.30", 50000, 3, "vnet3",
                             "vrf3", "bgpaas-client",
                             true, true);
        SendBgpServiceConfig("4.4.4.40", 50001, 4, "vnet4",
                             "vrf4", "bgpaas-server",
                             true, true);
        SendBgpServiceConfig("5.5.5.50", 50001, 5, "vnet5",
                             "vrf5", "bgpaas-server",
                             true, true);
        SendBgpServiceConfig("6.6.6.60", 50001, 6, "vnet6",
                             "vrf6", "bgpaas-client",
                             true, true);
        DelIPAM("vn1");
        DelIPAM("vn2");
        DelIPAM("vn3");
        DelIPAM("vn4");
        DelIPAM("vn5");
        DelIPAM("vn6");
        DeleteVmportEnv(input, 6, true);
        DelBgpaasPortRange();
        client->WaitForIdle();
        EXPECT_FALSE(agent_->oper_db()->bgp_as_a_service()->IsConfigured());
    }

    Agent *agent_;
    FlowProto *flow_proto_;
};

//TTL 1
TEST_F(BgpServiceTest, Test_ttl_1) {
    AddAap("vnet1", 1, Ip4Address::from_string("10.10.10.10"), "00:00:01:01:01:01");

    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(1)->id(), "10.10.10.10", "1.1.1.1", 10000, 179,
                false, 1, 1, 1);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "10.10.10.10", "1.1.1.1", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_port() == 50000);
    EXPECT_TRUE(fe->data().ttl == BGP_SERVICE_TTL_FWD_FLOW);
    EXPECT_TRUE(fe->reverse_flow_entry()->data().ttl ==
                BGP_SERVICE_TTL_REV_FLOW);

    client->WaitForIdle();
}

//TTL 64
TEST_F(BgpServiceTest, Test_ttl_2) {
    AddAap("vnet1", 1, Ip4Address::from_string("10.10.10.10"), "00:00:01:01:01:01");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(1)->id(), "10.10.10.10", "1.1.1.1", 10000, 179,
                false, 1, 1, 64);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "10.10.10.10", "1.1.1.1", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    // flow ttl is set to pkt ttl
    EXPECT_TRUE(fe->data().ttl == 64);
    EXPECT_TRUE(fe->reverse_flow_entry()->data().ttl == 0);

    client->WaitForIdle();
}

TEST_F(BgpServiceTest, Test_1) {
    TxTcpPacket(VmInterfaceGet(1)->id(), "1.1.1.10", "1.1.1.1", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "1.1.1.10", "1.1.1.1", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));


    TxTcpPacket(VmInterfaceGet(2)->id(), "2.2.2.20", "2.2.2.2", 20000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe1 = FlowGet(VmInterfaceGet(2)->flow_key_nh()->id(),
                            "2.2.2.20", "2.2.2.2", 6, 20000, 179);
    EXPECT_TRUE(fe1 != NULL);
    EXPECT_TRUE(fe1->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe1->is_flags_set(FlowEntry::BgpRouterService));

    TxTcpPacket(VmInterfaceGet(3)->id(), "3.3.3.30", "3.3.3.3", 30000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe2 = FlowGet(VmInterfaceGet(3)->flow_key_nh()->id(),
                            "3.3.3.30", "3.3.3.3", 6, 30000, 179);
    EXPECT_TRUE(fe2 != NULL);
    EXPECT_TRUE(fe2->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe2->is_flags_set(FlowEntry::BgpRouterService));

    TxTcpPacket(VmInterfaceGet(4)->id(), "4.4.4.40", "4.4.4.4", 11000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe3 = FlowGet(VmInterfaceGet(4)->flow_key_nh()->id(),
                            "4.4.4.40", "4.4.4.4", 6, 11000, 179);
    EXPECT_TRUE(fe3 != NULL);
    EXPECT_TRUE(fe3->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe3->is_flags_set(FlowEntry::BgpRouterService));


    TxTcpPacket(VmInterfaceGet(5)->id(), "5.5.5.50", "5.5.5.5", 21000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe4 = FlowGet(VmInterfaceGet(5)->flow_key_nh()->id(),
                            "5.5.5.50", "5.5.5.5", 6, 21000, 179);
    EXPECT_TRUE(fe4 != NULL);
    EXPECT_TRUE(fe4->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe4->is_flags_set(FlowEntry::BgpRouterService));

    TxTcpPacket(VmInterfaceGet(6)->id(), "6.6.6.60", "6.6.6.6", 31000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe5 = FlowGet(VmInterfaceGet(6)->flow_key_nh()->id(),
                            "6.6.6.60", "6.6.6.6", 6, 31000, 179);
    EXPECT_TRUE(fe5 != NULL);
    EXPECT_TRUE(fe5->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe5->is_flags_set(FlowEntry::BgpRouterService));

}

TEST_F(BgpServiceTest, Test_2) {
    TxTcpPacket(VmInterfaceGet(1)->id(), "1.1.1.10", "1.1.1.1", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "1.1.1.10", "1.1.1.1", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_port() == 50000);

    //Explicitly call deleteall on bgp service tree.
    //agent_->pkt()->flow_mgmt_manager()->ControllerNotify(0);
    //agent_->pkt()->flow_mgmt_manager()->ControllerNotify(1);
    client->WaitForIdle();
}

TEST_F(BgpServiceTest, Test_3) {
    AddAap("vnet1", 1, Ip4Address::from_string("10.10.10.10"), "00:00:01:01:01:01");
    AddAap("vnet2", 2, Ip4Address::from_string("20.20.20.20"), "00:00:02:02:02:02");
    AddAap("vnet3", 3, Ip4Address::from_string("30.30.30.30"), "00:00:03:03:03:03");
    AddAap("vnet4", 4, Ip4Address::from_string("40.40.40.40"), "00:00:04:04:04:04");
    AddAap("vnet5", 5, Ip4Address::from_string("50.50.50.50"), "00:00:05:05:05:05");
    AddAap("vnet6", 6, Ip4Address::from_string("60.60.60.60"), "00:00:06:06:06:06");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(1)->id(), "10.10.10.10", "1.1.1.1", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "10.10.10.10", "1.1.1.1", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_port() == 50000);
    client->WaitForIdle();
}

TEST_F(BgpServiceTest, Test_4) {
    IFMapTable *table = IFMapTable::FindTable(agent_->db(),
                                              "virtual-machine-interface");
    IFMapNode *node = table->FindNode("vnet1");
    if (node == NULL) {
        assert(0);
    }

    InterfaceTable *intf_table = agent_->interface_table();

    //simulate the same session add for the same vmi
    DBRequest request1;
    boost::uuids::uuid u;
    intf_table->IFNodeToUuid(node, u);
    intf_table->VmiProcessConfig(node, request1, u);

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    intf_table->Enqueue(&request1);

    scheduler->Start();
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(1)->id(), "1.1.1.10", "1.1.1.1", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "1.1.1.10", "1.1.1.1", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_port() == 50000);
    client->WaitForIdle();
}

TEST_F(BgpServiceTest, Test_5) {
    client->WaitForIdle();

    std::stringstream bgp_router_name;
    bgp_router_name << "bgp-router-50500";
    std::stringstream bgp_as_service_name;
    bgp_as_service_name << "bgp-router-50000-1.1.1.10";
    std::stringstream str;
    str << "<bgp-router-parameters><identifier>100.100.100.100</identifier>"
        "<address>100.100.100.100</address>"
        "<source-port>50500</source-port>"
        "<router-type>bgpaas-client</router-type>"
        "</bgp-router-parameters>" << endl;

    DelLink("virtual-machine-interface", "vnet1",
            "bgp-router", bgp_as_service_name.str().c_str());
    AddNode("bgp-router", bgp_router_name.str().c_str(), 100,
            str.str().c_str());
    AddLink("bgp-router", bgp_router_name.str().c_str(),
            "routing-instance", "vrf1");
    AddLink("bgp-router", bgp_router_name.str().c_str(),
            "bgp-as-a-service", bgp_as_service_name.str().c_str());
    AddLink("virtual-machine-interface", "vnet1",
            "bgp-router", bgp_router_name.str().c_str());
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(1)->id(), "1.1.1.10", "1.1.1.1", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "1.1.1.10", "1.1.1.1", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_port() == 50500);

    //Delete
    DelLink("virtual-machine-interface", "vnet1",
            "bgp-router", bgp_router_name.str().c_str());
    DelLink("bgp-router", bgp_router_name.str().c_str(),
            "bgp-as-a-service", bgp_as_service_name.str().c_str());
    DelLink("bgp-router", bgp_router_name.str().c_str(),
            "routing-instance", "vrf1");
    DelNode("bgp-router", bgp_router_name.str().c_str());
    client->WaitForIdle();
}

TEST_F(BgpServiceTest, Test_6) {
    AddAap("vnet3", 3, Ip4Address::from_string("30.30.30.30"), "00:00:03:03:03:03");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(3)->id(), "30.30.30.30", "3.3.3.3", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(3)->flow_key_nh()->id(),
                            "30.30.30.30", "3.3.3.3", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    //DelBgpaasPortRange();
    AddBgpaasPortRange(50000, 52000);
    client->WaitForIdle();
    FlowEntry *fe1 = FlowGet(VmInterfaceGet(3)->flow_key_nh()->id(),
                            "30.30.30.30", "3.3.3.3", 6, 10000, 179);
    EXPECT_TRUE(fe1 == NULL);
    TxTcpPacket(VmInterfaceGet(3)->id(), "30.30.30.30", "3.3.3.3", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe2 = FlowGet(VmInterfaceGet(3)->flow_key_nh()->id(),
                            "30.30.30.30", "3.3.3.3", 6, 10000, 179);
    EXPECT_TRUE(fe2 != NULL);
    EXPECT_TRUE(fe2->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe2->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe2->bgp_as_a_service_port() == 54002);
    client->WaitForIdle();
}
int main(int argc, char *argv[]) {
    int ret = 0;
    BgpPeer *peer;
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    peer = CreateBgpPeer("127.0.0.1", "remote");
    client->WaitForIdle();
    ret = RUN_ALL_TESTS();
    usleep(100000);
    DeleteBgpPeer(peer);
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
