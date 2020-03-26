/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "pkt/flow_proto.h"
#include "pkt/flow_mgmt.h"
#include "pkt/pkt_handler.h"
#include "oper/bgp_as_service.h"
#include "oper/bgp_router.h"
#include "oper/health_check.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"

VmInterface *vnet[16];
Interface *vhost;
char vhost_addr[32];
char vnet_addr[16][32];
std::string bgpaas[16];

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
    {"vnet7", 7, "7.7.7.70", "00:00:07:07:07:07", 7, 7},
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.1", true},
    {"2.2.2.0", 24, "2.2.2.2", true},
    {"3.3.3.0", 24, "3.3.3.3", true},
    {"4.4.4.0", 24, "4.4.4.4", true},
    {"5.5.5.0", 24, "5.5.5.5", true},
    {"6.6.6.0", 24, "6.6.6.6", true},
    {"7.7.7.0", 24, "7.7.7.7", true, 0, "7.7.7.8"},
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

    void AddBgpaasControlNodeZoneLink(const std::string &link_name,
                                      const std::string &bgpaas_name,
                                      const std::string &cnz_name,
                                      const std::string &bgpaas_cnz_type) {
        std::stringstream str;
        str << "<bgpaas-control-node-zone-type>"
            << bgpaas_cnz_type
            << "</bgpaas-control-node-zone-type>";
        AddLinkNode("bgpaas-control-node-zone",
                    link_name.c_str(),
                    str.str().c_str());
        AddLink("bgpaas-control-node-zone",
                link_name.c_str(),
                "bgp-as-a-service",
                bgpaas_name.c_str(),
                "bgpaas-control-node-zone");
        AddLink("bgpaas-control-node-zone",
                link_name.c_str(),
                "control-node-zone",
                cnz_name.c_str(),
                "bgpaas-control-node-zone");
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
        CreateVmportEnv(input, 7);
        AddIPAM("vn1", &ipam_info[0], 1);
        AddIPAM("vn2", &ipam_info[1], 1);
        AddIPAM("vn3", &ipam_info[2], 1);
        AddIPAM("vn4", &ipam_info[3], 1);
        AddIPAM("vn5", &ipam_info[4], 1);
        AddIPAM("vn6", &ipam_info[5], 1);
        AddIPAM("vn7", &ipam_info[6], 1);
        client->WaitForIdle();
        AddBgpRouterConfig("127.0.0.1", 0, 179,
                           64512, "ip-fabric", "control-node");
        bgpaas[0] = AddBgpServiceConfig("1.1.1.10", 50000, 179, 1, "vnet1",
                            "vrf1", "bgpaas-client", true);
        bgpaas[1] = AddBgpServiceConfig("2.2.2.20", 50000, 179, 2, "vnet2",
                            "vrf2", "bgpaas-client", true);
        bgpaas[2] = AddBgpServiceConfig("3.3.3.30", 50000, 500, 3, "vnet3",
                            "vrf3", "bgpaas-client", true);
        bgpaas[3] = AddBgpServiceConfig("4.4.4.40", 50001, 179, 4, "vnet4",
                            "vrf4", "bgpaas-client", true);
        bgpaas[4] = AddBgpServiceConfig("5.5.5.50", 50001, 179, 5, "vnet5",
                            "vrf5", "bgpaas-client", true);
        bgpaas[5] = AddBgpServiceConfig("6.6.6.60", 50001, 179, 6, "vnet6",
                            "vrf6", "bgpaas-client", true);
        bgpaas[6] = AddBgpServiceConfig("7.7.7.70", 50001, 0, 6, "vnet7",
                            "vrf7", "bgpaas-client", true);
        client->WaitForIdle();
        EXPECT_TRUE(agent_->oper_db()->bgp_as_a_service()->IsConfigured());
    }

    virtual void TearDown() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        DeleteBgpRouterConfig("127.0.0.1", 0, "ip-fabric");
        DeleteBgpServiceConfig("1.1.1.10", 50000, "vnet1", "vrf1");
        DeleteBgpServiceConfig("2.2.2.20", 50000, "vnet2", "vrf2");
        DeleteBgpServiceConfig("3.3.3.30", 50000, "vnet3", "vrf3");
        DeleteBgpServiceConfig("4.4.4.40", 50001, "vnet4", "vrf4");
        DeleteBgpServiceConfig("5.5.5.50", 50001, "vnet5", "vrf5");
        DeleteBgpServiceConfig("6.6.6.60", 50001, "vnet6", "vrf6");
        DeleteBgpServiceConfig("7.7.7.70", 50001, "vnet7", "vrf7");
        DelIPAM("vn1");
        DelIPAM("vn2");
        DelIPAM("vn3");
        DelIPAM("vn4");
        DelIPAM("vn5");
        DelIPAM("vn6");
        DelIPAM("vn7");
        DeleteVmportEnv(input, 7, true);
        DelBgpaasPortRange();
        client->WaitForIdle();
        EXPECT_FALSE(agent_->oper_db()->bgp_as_a_service()->IsConfigured());
    }

    Agent *agent_;
    FlowProto *flow_proto_;
};

//TTL 1
TEST_F(BgpServiceTest, Test_ttl_1) {
    AddAap("vnet1", 1,
        Ip4Address::from_string("10.10.10.10"), "00:00:01:01:01:01");

    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(1)->id(), "10.10.10.10", "1.1.1.1", 10000, 179,
                false, 1, 2, 1);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "10.10.10.10", "1.1.1.1", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_sport() == 50000);
    EXPECT_TRUE(fe->data().ttl == BGP_SERVICE_TTL_FWD_FLOW);
    EXPECT_TRUE(fe->reverse_flow_entry()->data().ttl ==
                BGP_SERVICE_TTL_REV_FLOW);
}

//TTL 64
TEST_F(BgpServiceTest, Test_ttl_2) {
    AddAap("vnet1", 1,
        Ip4Address::from_string("10.10.10.10"), "00:00:01:01:01:01");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(1)->id(), "10.10.10.10", "1.1.1.1", 10000, 179,
                false, 1, 2, 64);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "10.10.10.10", "1.1.1.1", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    // flow ttl is set to pkt ttl
    EXPECT_TRUE(fe->data().ttl == 64);
    EXPECT_TRUE(fe->reverse_flow_entry()->data().ttl == 0);
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
    EXPECT_TRUE(fe->bgp_as_a_service_sport() == 50000);

    //Explicitly call deleteall on bgp service tree.
    //agent_->pkt()->flow_mgmt_manager()->ControllerNotify(0);
    //agent_->pkt()->flow_mgmt_manager()->ControllerNotify(1);
}

TEST_F(BgpServiceTest, Test_3) {
    AddAap("vnet1", 1,
        Ip4Address::from_string("10.10.10.10"), "00:00:01:01:01:01");
    AddAap("vnet2", 2,
        Ip4Address::from_string("20.20.20.20"), "00:00:02:02:02:02");
    AddAap("vnet3", 3,
        Ip4Address::from_string("30.30.30.30"), "00:00:03:03:03:03");
    AddAap("vnet4", 4,
        Ip4Address::from_string("40.40.40.40"), "00:00:04:04:04:04");
    AddAap("vnet5", 5,
        Ip4Address::from_string("50.50.50.50"), "00:00:05:05:05:05");
    AddAap("vnet6", 6,
        Ip4Address::from_string("60.60.60.60"), "00:00:06:06:06:06");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(1)->id(), "10.10.10.10", "1.1.1.1", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "10.10.10.10", "1.1.1.1", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_sport() == 50000);
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
    EXPECT_TRUE(fe->bgp_as_a_service_sport() == 50000);
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
    EXPECT_TRUE(fe->bgp_as_a_service_sport() == 50500);

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
    AddAap("vnet3", 3,
        Ip4Address::from_string("30.30.30.30"), "00:00:03:03:03:03");
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
    EXPECT_TRUE(fe2->bgp_as_a_service_sport() == 54002);
}

//Test to verify dest port for nat flow
TEST_F(BgpServiceTest, Test_7) {
    AddAap("vnet3", 3,
        Ip4Address::from_string("30.30.30.30"), "00:00:03:03:03:03");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(3)->id(), "30.30.30.30", "3.3.3.3", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(3)->flow_key_nh()->id(),
                            "30.30.30.30", "3.3.3.3", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_dport() == 500);
}

//ControlNodeZone is associated to a BgpRouter
TEST_F(BgpServiceTest, Test_8) {
    BgpRouterConfig *bgp_router_config =
        agent_->oper_db()->bgp_router_config();
    std::string bgp_router_1 = AddBgpRouterConfig("127.0.0.10", 0, 179,
        1, "ip-fabric", "control-node");
    std::string bgp_router_2 = AddBgpRouterConfig("127.0.0.11", 0, 179,
        2, "ip-fabric", "control-node");
    std::string bgp_router_3 = AddBgpRouterConfig("127.0.0.12", 0, 179,
        3, "ip-fabric", "control-node");
    AddControlNodeZone("cnz-a", 1);
    AddControlNodeZone("cnz-b", 2);
    AddControlNodeZone("cnz-c", 3);
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 4);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 0);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-b") == 0);

    AddLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    AddLink("bgp-router", bgp_router_2.c_str(), "control-node-zone", "cnz-b");
    AddLink("bgp-router", bgp_router_3.c_str(), "control-node-zone", "cnz-c");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 4);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 3);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 1);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-b") == 1);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-c") == 1);

    AddAap("vnet7", 7,
        Ip4Address::from_string("70.70.70.70"), "00:00:07:07:07:07");
    TxTcpPacket(VmInterfaceGet(7)->id(), "70.70.70.70", "7.7.7.7", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(7)->flow_key_nh()->id(),
                            "70.70.70.70", "7.7.7.7", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_dport() == 179);
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe->key().src_addr == Ip4Address::from_string("127.0.0.1"));
    EXPECT_TRUE(rfe->key().dst_addr == Ip4Address::from_string("10.1.1.1"));

    DelLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    DelLink("bgp-router", bgp_router_2.c_str(), "control-node-zone", "cnz-b");
    DelLink("bgp-router", bgp_router_3.c_str(), "control-node-zone", "cnz-c");
    DeleteControlNodeZone("cnz-c");
    DeleteControlNodeZone("cnz-b");
    DeleteControlNodeZone("cnz-a");
    DeleteBgpRouterConfig("127.0.0.12", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.11", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.10", 0, "ip-fabric");
    client->WaitForIdle();
}

//BgpRouter listens in non-standard port
TEST_F(BgpServiceTest, Test_9) {
    BgpRouterConfig *bgp_router_config =
        agent_->oper_db()->bgp_router_config();
    std::string bgp_router_1 = AddBgpRouterConfig("127.0.0.10", 0, 5000,
        1, "ip-fabric", "control-node");
    AddControlNodeZone("cnz-a", 1);
    AddLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 2);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 1);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 1);

    AddBgpaasControlNodeZoneLink("link1", bgpaas[6], "cnz-a", "primary");
    AddAap("vnet7", 7,
        Ip4Address::from_string("70.70.70.70"), "00:00:07:07:07:07");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(7)->id(), "70.70.70.70", "7.7.7.7", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(7)->flow_key_nh()->id(),
                            "70.70.70.70", "7.7.7.7", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_dport() == 5000);
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe->key().src_addr == Ip4Address::from_string("127.0.0.10"));
    EXPECT_TRUE(rfe->key().dst_addr == Ip4Address::from_string("10.1.1.1"));
    EXPECT_TRUE(rfe->key().src_port == 5000);

    DelNode("bgpaas-control-node-zone", "link1");
    DelLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    DeleteControlNodeZone("cnz-a");
    DeleteBgpRouterConfig("127.0.0.10", 0, "ip-fabric");
    client->WaitForIdle();
}

//ControlNodeZone is associated to multiple BgpRouters
TEST_F(BgpServiceTest, Test_10) {
    BgpRouterConfig *bgp_router_config =
        agent_->oper_db()->bgp_router_config();
    std::string bgp_router_1 = AddBgpRouterConfig("127.0.0.10", 0, 5000,
        1, "ip-fabric", "control-node");
    std::string bgp_router_2 = AddBgpRouterConfig("127.0.0.11", 0, 5001,
        2, "ip-fabric", "control-node");
    std::string bgp_router_3 = AddBgpRouterConfig("127.0.0.12", 0, 5002,
        3, "ip-fabric", "control-node");
    AddControlNodeZone("cnz-a", 1);
    AddLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    AddLink("bgp-router", bgp_router_2.c_str(), "control-node-zone", "cnz-a");
    AddLink("bgp-router", bgp_router_3.c_str(), "control-node-zone", "cnz-a");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 4);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 1);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 3);

    AddBgpaasControlNodeZoneLink("link1", bgpaas[6], "cnz-a", "primary");
    AddAap("vnet7", 7,
        Ip4Address::from_string("70.70.70.70"), "00:00:07:07:07:07");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(7)->id(), "70.70.70.70", "7.7.7.7", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(7)->flow_key_nh()->id(),
                            "70.70.70.70", "7.7.7.7", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE((fe->bgp_as_a_service_dport() == 5000) ||
                (fe->bgp_as_a_service_dport() == 5001) ||
                (fe->bgp_as_a_service_dport() == 5002));
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(
        (rfe->key().src_addr == Ip4Address::from_string("127.0.0.10")) ||
        (rfe->key().src_addr == Ip4Address::from_string("127.0.0.11")) ||
        (rfe->key().src_addr == Ip4Address::from_string("127.0.0.12")));
    EXPECT_TRUE(rfe->key().dst_addr == Ip4Address::from_string("10.1.1.1"));
    EXPECT_TRUE(rfe->key().src_port == 5000 ||
                rfe->key().src_port == 5001 ||
                rfe->key().src_port == 5002);

    DelNode("bgpaas-control-node-zone", "link1");
    DelLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    DelLink("bgp-router", bgp_router_2.c_str(), "control-node-zone", "cnz-a");
    DelLink("bgp-router", bgp_router_3.c_str(), "control-node-zone", "cnz-a");
    DeleteControlNodeZone("cnz-a");
    DeleteBgpRouterConfig("127.0.0.10", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.11", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.12", 0, "ip-fabric");
    client->WaitForIdle();
}

//ControlNodeZone is not associated to a BgpRouter
TEST_F(BgpServiceTest, Test_11) {
    BgpRouterConfig *bgp_router_config =
        agent_->oper_db()->bgp_router_config();
    std::string bgp_router_1 = AddBgpRouterConfig("127.0.0.10", 0, 5000,
        1, "ip-fabric", "control-node");
    AddControlNodeZone("cnz-a", 1);
    AddControlNodeZone("cnz-b", 1);
    AddLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 2);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 1);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 1);

    AddBgpaasControlNodeZoneLink("link1", bgpaas[6], "cnz-b", "primary");
    AddAap("vnet7", 7,
        Ip4Address::from_string("70.70.70.70"), "00:00:07:07:07:07");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(7)->id(), "70.70.70.70", "7.7.7.7", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(7)->flow_key_nh()->id(),
                            "70.70.70.70", "7.7.7.7", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_FALSE(fe->bgp_as_a_service_dport() == 5000);
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_FALSE(rfe->key().src_addr == Ip4Address::from_string("127.0.0.10"));
    EXPECT_FALSE(rfe->key().dst_addr == Ip4Address::from_string("10.1.1.1"));
    EXPECT_FALSE(rfe->key().src_port == 5000);

    DelNode("bgpaas-control-node-zone", "link1");
    DelLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    DeleteControlNodeZone("cnz-b");
    DeleteControlNodeZone("cnz-a");
    DeleteBgpRouterConfig("127.0.0.10", 0, "ip-fabric");
    client->WaitForIdle();
}

//Bgpaas with "Secondary" ControlNodeZone
TEST_F(BgpServiceTest, Test_12) {
    BgpRouterConfig *bgp_router_config =
        agent_->oper_db()->bgp_router_config();
    std::string bgp_router_1 = AddBgpRouterConfig("127.0.0.10", 0, 5000,
        1, "ip-fabric", "control-node");
    AddControlNodeZone("cnz-a", 1);
    AddLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    client->WaitForIdle();
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount() == 2);
    EXPECT_TRUE(bgp_router_config->GetControlNodeZoneCount() == 1);
    EXPECT_TRUE(bgp_router_config->GetBgpRouterCount("cnz-a") == 1);

    AddBgpaasControlNodeZoneLink("link1", bgpaas[6], "cnz-a", "secondary");
    AddAap("vnet7", 7,
        Ip4Address::from_string("70.70.70.70"), "00:00:07:07:07:07");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(7)->id(), "70.70.70.70", "7.7.7.8", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(7)->flow_key_nh()->id(),
                            "70.70.70.70", "7.7.7.8", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_dport() == 5000);
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe->key().src_addr == Ip4Address::from_string("127.0.0.10"));
    EXPECT_TRUE(rfe->key().dst_addr == Ip4Address::from_string("10.1.1.1"));
    EXPECT_TRUE(rfe->key().src_port == 5000);

    DelNode("bgpaas-control-node-zone", "link1");
    DelLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    DeleteControlNodeZone("cnz-a");
    DeleteBgpRouterConfig("127.0.0.10", 0, "ip-fabric");
    client->WaitForIdle();
}

//Bgpaas with "Primary" and "Secondary" ControlNodeZone
TEST_F(BgpServiceTest, Test_13) {
    std::string bgp_router_1 = AddBgpRouterConfig("127.0.0.10", 0, 5000,
        1, "ip-fabric", "control-node");
    std::string bgp_router_2 = AddBgpRouterConfig("127.0.0.11", 0, 5000,
        2, "ip-fabric", "control-node");
    std::string bgp_router_3 = AddBgpRouterConfig("127.0.0.12", 0, 5000,
        3, "ip-fabric", "control-node");
    AddControlNodeZone("cnz-a", 1);
    AddControlNodeZone("cnz-b", 2);
    AddControlNodeZone("cnz-c", 3);
    AddLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    AddLink("bgp-router", bgp_router_2.c_str(), "control-node-zone", "cnz-b");
    AddLink("bgp-router", bgp_router_3.c_str(), "control-node-zone", "cnz-c");
    AddBgpaasControlNodeZoneLink("link1", bgpaas[6], "cnz-a", "primary");
    AddBgpaasControlNodeZoneLink("link2", bgpaas[6], "cnz-b", "secondary");
    AddAap("vnet7", 7,
        Ip4Address::from_string("70.70.70.70"), "00:00:07:07:07:07");

    TxTcpPacket(VmInterfaceGet(7)->id(), "70.70.70.70", "7.7.7.7", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(7)->flow_key_nh()->id(),
                            "70.70.70.70", "7.7.7.7", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_dport() == 5000);
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe->key().src_addr == Ip4Address::from_string("127.0.0.10"));
    EXPECT_TRUE(rfe->key().dst_addr == Ip4Address::from_string("10.1.1.1"));
    EXPECT_TRUE(rfe->key().src_port == 5000);

    TxTcpPacket(VmInterfaceGet(7)->id(), "70.70.70.70", "7.7.7.8", 10000, 179,
                false);
    client->WaitForIdle();
    fe = FlowGet(VmInterfaceGet(7)->flow_key_nh()->id(),
                 "70.70.70.70", "7.7.7.8", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_dport() == 5000);
    rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe->key().src_addr == Ip4Address::from_string("127.0.0.11"));
    EXPECT_TRUE(rfe->key().dst_addr == Ip4Address::from_string("10.1.1.1"));
    EXPECT_TRUE(rfe->key().src_port == 5000);

    //Change "Primary" and "Secondary" ControlNodeZone
    DelNode("bgpaas-control-node-zone", "link1");
    DelNode("bgpaas-control-node-zone", "link2");
    DelLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    DelLink("bgp-router", bgp_router_2.c_str(), "control-node-zone", "cnz-b");
    DelLink("bgp-router", bgp_router_3.c_str(), "control-node-zone", "cnz-c");
    DeleteControlNodeZone("cnz-a");
    DeleteControlNodeZone("cnz-b");
    DeleteControlNodeZone("cnz-c");
    DeleteBgpRouterConfig("127.0.0.10", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.11", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.12", 0, "ip-fabric");
    bgp_router_1 = AddBgpRouterConfig("127.0.0.13", 0, 5000,
        1, "ip-fabric", "control-node");
    bgp_router_2 = AddBgpRouterConfig("127.0.0.14", 0, 5000,
        2, "ip-fabric", "control-node");
    bgp_router_3 = AddBgpRouterConfig("127.0.0.15", 0, 5000,
        3, "ip-fabric", "control-node");
    AddControlNodeZone("cnz-a", 1);
    AddControlNodeZone("cnz-b", 2);
    AddControlNodeZone("cnz-c", 3);
    AddLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    AddLink("bgp-router", bgp_router_2.c_str(), "control-node-zone", "cnz-b");
    AddLink("bgp-router", bgp_router_3.c_str(), "control-node-zone", "cnz-c");
    AddBgpaasControlNodeZoneLink("link1", bgpaas[6], "cnz-c", "primary");
    AddBgpaasControlNodeZoneLink("link2", bgpaas[6], "cnz-a", "secondary");
    AddAap("vnet7", 7,
        Ip4Address::from_string("70.70.70.70"), "00:00:07:07:07:07");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(7)->id(), "70.70.70.70", "7.7.7.7", 10000, 179,
                false);
    client->WaitForIdle();
    fe = FlowGet(VmInterfaceGet(7)->flow_key_nh()->id(),
                 "70.70.70.70", "7.7.7.7", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_dport() == 5000);
    rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe->key().src_addr == Ip4Address::from_string("127.0.0.15"));
    EXPECT_TRUE(rfe->key().dst_addr == Ip4Address::from_string("10.1.1.1"));
    EXPECT_TRUE(rfe->key().src_port == 5000);

    TxTcpPacket(VmInterfaceGet(7)->id(), "70.70.70.70", "7.7.7.8", 10000, 179,
                false);
    client->WaitForIdle();
    fe = FlowGet(VmInterfaceGet(7)->flow_key_nh()->id(),
                 "70.70.70.70", "7.7.7.8", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));
    EXPECT_TRUE(fe->bgp_as_a_service_dport() == 5000);
    rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe->key().src_addr == Ip4Address::from_string("127.0.0.13"));
    EXPECT_TRUE(rfe->key().dst_addr == Ip4Address::from_string("10.1.1.1"));
    EXPECT_TRUE(rfe->key().src_port == 5000);

    DelNode("bgpaas-control-node-zone", "link1");
    DelNode("bgpaas-control-node-zone", "link2");
    DelLink("bgp-router", bgp_router_1.c_str(), "control-node-zone", "cnz-a");
    DelLink("bgp-router", bgp_router_2.c_str(), "control-node-zone", "cnz-b");
    DelLink("bgp-router", bgp_router_3.c_str(), "control-node-zone", "cnz-c");
    DeleteControlNodeZone("cnz-a");
    DeleteControlNodeZone("cnz-b");
    DeleteControlNodeZone("cnz-c");
    DeleteBgpRouterConfig("127.0.0.10", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.11", 0, "ip-fabric");
    DeleteBgpRouterConfig("127.0.0.12", 0, "ip-fabric");
    client->WaitForIdle();
}

TEST_F(BgpServiceTest, Test_14) {
    struct PortInfo input[] = {
        {"vnet100", 100, "1.1.1.100", "00:00:01:01:01:10", 1, 100},
    };

    client->Reset();
    AddBgpaasPortRange(50000, 50512);
    client->WaitForIdle();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    uint32_t old_bgp_service_count =
        agent_->oper_db()->bgp_as_a_service()->bgp_as_a_service_map().size();
    std::string bgpaas = AddBgpServiceConfig("1.1.1.100", 50005,
                                             179, 100, "vnet100", "vrf1",
                                             "bgpaas-client", false, true);
    client->WaitForIdle();
    uint32_t new_bgp_service_count =
        agent_->oper_db()->bgp_as_a_service()->bgp_as_a_service_map().size();
    EXPECT_TRUE(new_bgp_service_count == old_bgp_service_count+1);

    BgpAsAService::BgpAsAServiceEntryMap map_entry =
       Agent::GetInstance()->oper_db()->bgp_as_a_service()->bgp_as_a_service_map();
    BgpAsAService::BgpAsAServiceEntryMapIterator map_it =
       map_entry.begin();
    while (map_it != map_entry.end()) {
        EXPECT_TRUE(map_it->second->list_.size() == 1);
        BgpAsAService::BgpAsAServiceEntryListIterator it =
           map_it->second->list_.begin();
        while (it != map_it->second->list_.end()) {
            if (strcmp((*it).local_peer_ip_.to_string().c_str(),
                       "1.1.1.100") == 0) {
                EXPECT_TRUE((*it).health_check_configured_ == true);
            }
            it++;
        }
        map_it++;
    }

    DeleteBgpServiceConfig("1.1.1.100", 50005, "vnet100", "vrf1", true);
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    new_bgp_service_count =
        agent_->oper_db()->bgp_as_a_service()->bgp_as_a_service_map().size();
    EXPECT_TRUE(old_bgp_service_count == new_bgp_service_count);
    EXPECT_FALSE(VmPortActive(input, 0));
    DelBgpaasPortRange();
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
