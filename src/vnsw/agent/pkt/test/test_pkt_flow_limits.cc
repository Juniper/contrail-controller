/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include <base/os.h>
#include <base/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "pkt/flow_table.h"

#define MAX_VNET 4
int fd_table[MAX_VNET];

struct TestFlowKey {
    uint32_t        vrfid_;
    const char      *sip_;
    const char      *dip_;
    uint8_t         proto_;
    uint16_t        sport_;
    uint16_t        dport_;
};

#define vm1_ip "11.1.1.1"
#define vm2_ip "11.1.1.2"
#define vm3_ip "12.1.1.1"
#define vm4_ip "13.1.1.1"

#define vhost_ip_addr "10.1.2.1"
#define linklocal_ip "169.254.1.10"
#define linklocal_port 4000
#define fabric_port 8000

#define vm_a_ip "16.1.1.1"
#define vm_b_ip "16.1.1.2"

const Interface *vhost;
struct PortInfo input[] = {
        {"vmi_0", 6, vm1_ip, "00:00:00:01:01:01", 5, 1},
        {"vmi_1", 7, vm2_ip, "00:00:00:01:01:02", 5, 2},
        {"vmi_2", 8, vm3_ip, "00:00:00:01:01:03", 5, 3},
        {"vmi_3", 9, vm4_ip, "00:00:00:01:01:04", 3, 4},
};
IpamInfo ipam_info[] = {
    {"11.1.1.0", 24, "11.1.1.10"},
    {"12.1.1.0", 24, "12.1.1.10"},
};
IpamInfo ipam_info2[] = {
    {"13.1.1.0", 24, "13.1.1.10"},
};

typedef enum {
    INGRESS = 0,
    EGRESS = 1,
    BIDIRECTION = 2
} AclDirection;

int hash_id;
VmInterface *vmi_0;
VmInterface *vmi_1;
VmInterface *vmi_2;
VmInterface *vmi_3;
std::string eth_itf;

class FlowTest : public ::testing::Test {
public:
    FlowTest() : agent_(Agent::GetInstance()) {
        flow_proto_ = agent_->pkt()->get_flow_proto();
        vhost = agent_->vhost_interface();
    }

    bool FlowTableWait(size_t count) {
        int i = 1000;
        while (i > 0) {
            i--;
            if (get_flow_proto()->FlowCount() == count) {
                break;
            }
            client->WaitForIdle();
            usleep(1);
        }
        return (get_flow_proto()->FlowCount() == count);
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    }

    void CreateLocalRoute(const char *vrf, const char *ip,
                          VmInterface *intf, int label) {
        Ip4Address addr = Ip4Address::from_string(ip);
        VnListType vn_list;
        vn_list.insert(intf->vn()->GetName());
        agent()->fabric_inet4_unicast_table()->
            AddLocalVmRouteReq(agent()->local_peer(), vrf, addr, 32,
                               intf->GetUuid(), vn_list, label,
                               SecurityGroupList(), TagList(),
                               CommunityList(), false,
                               PathPreference(), Ip4Address(0),
                               EcmpLoadBalance(), false, false,
                               false);
        client->WaitForIdle();
        EXPECT_TRUE(RouteFind(vrf, addr, 32));
    }

    void DeleteRoute(const char *vrf, const char *ip) {
        Ip4Address addr = Ip4Address::from_string(ip);
        agent()->fabric_inet4_unicast_table()->DeleteReq(agent()->local_peer(),
                                                vrf, addr, 32, NULL);
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFind(vrf, addr, 32) == false));
    }

    static void TestTearDown() {
        client->Reset();
        if (ksync_init_) {
            DeleteTapIntf(fd_table, MAX_VNET);
        }
        client->WaitForIdle();
    }

    static void TestSetup(bool ksync_init) {
        ksync_init_ = ksync_init;
        if (ksync_init_) {
            CreateTapInterfaces("flow", MAX_VNET, fd_table);
            client->WaitForIdle();
        }
    }

protected:
    virtual void SetUp() {
        agent_->flow_stats_manager()->set_delete_short_flow(false);
        Ip4Address rid = Ip4Address::from_string(vhost_ip_addr);
        agent_->set_router_id(rid);
        agent_->set_compute_node_ip(rid);
        hash_id = 1;

        EXPECT_EQ(0U, get_flow_proto()->FlowCount());
        client->Reset();
        CreateVmportEnv(input, 4, 1);
        client->WaitForIdle(5);
        AddIPAM("vn5", ipam_info, 2);
        AddIPAM("vn3", ipam_info2, 1);
        client->WaitForIdle();

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        vmi_0 = VmInterfaceGet(input[0].intf_id);
        assert(vmi_0);

        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));
        vmi_1 = VmInterfaceGet(input[1].intf_id);
        assert(vmi_1);

        EXPECT_TRUE(VmPortActive(input, 2));
        EXPECT_TRUE(VmPortPolicyEnable(input, 2));
        vmi_2 = VmInterfaceGet(input[2].intf_id);
        assert(vmi_2);

        EXPECT_TRUE(VmPortActive(input, 3));
        EXPECT_TRUE(VmPortPolicyEnable(input, 3));
        vmi_3 = VmInterfaceGet(input[3].intf_id);
        assert(vmi_3);

        EXPECT_EQ(8U, agent()->interface_table()->Size());
        EXPECT_EQ(4U, PortSubscribeSize(agent()));
        EXPECT_EQ(4U, agent()->vm_table()->Size());
        EXPECT_EQ(2U, agent()->vn_table()->Size());
        client->WaitForIdle();
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();

        DeleteVmportEnv(input, 4, true, 1);
        client->WaitForIdle(3);
        DelIPAM("vn5");
        DelIPAM("vn3");
        client->WaitForIdle();
        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));
        EXPECT_FALSE(VmPortFind(input, 2));
        EXPECT_FALSE(VmPortFind(input, 3));

        EXPECT_EQ(4U, agent()->interface_table()->Size());
        EXPECT_EQ(0U, PortSubscribeSize(agent()));
        EXPECT_EQ(0U, agent()->vm_table()->Size());
        EXPECT_EQ(0U, agent()->vn_table()->Size());
        EXPECT_EQ(0U, agent()->acl_table()->Size());
        EXPECT_EQ(2U, agent()->vrf_table()->Size());

        EXPECT_EQ(flow_proto_->linklocal_flow_count(), 0U);
        EXPECT_EQ(flow_proto_->linklocal_flow_count(), 0U);
        FlowTable *table = flow_proto_->GetTable(0);
        EXPECT_EQ(table->linklocal_flow_info_map().size(), 0U);
    }

    Agent *agent() {return agent_;}
    FlowProto *get_flow_proto() const { return flow_proto_; }

protected:
    static bool ksync_init_;
public:
    Agent *agent_;
    FlowProto *flow_proto_;
};

bool FlowTest::ksync_init_;

TEST_F(FlowTest, LinkLocalFlow_1) {
    std::string fabric_ip("1.2.3.4");
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back("1.2.3.4");
    TestLinkLocalService service = {
        "test_service", linklocal_ip,linklocal_port, "", fabric_ip_list,
        fabric_port
    };
    AddLinkLocalConfig(&service, 1);
    client->WaitForIdle();

    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3000,
                        linklocal_port, "vrf5", vmi_0->id(), 1),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP,
                              fabric_port, 0)
            }
        }
    };

    CreateFlow(nat_flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());
    const FlowEntry *fe = nat_flow[0].pkt_.FlowFetch();
    uint16_t linklocal_src_port = fe->linklocal_src_port();

    EXPECT_TRUE(FlowGet(0, fabric_ip.c_str(), vhost_ip_addr, IPPROTO_TCP,
                        fabric_port, linklocal_src_port,
                        vhost->flow_key_nh()->id()) != NULL);
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, linklocal_ip,
                        IPPROTO_TCP, 3000, linklocal_port,
                        GetFlowKeyNH(input[0].intf_id)) != NULL);

    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Delete forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowFail(0, fabric_ip.c_str(), vhost_ip_addr, IPPROTO_TCP,
                         fabric_port, linklocal_src_port,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm1_ip, linklocal_ip,
                         IPPROTO_TCP, 3000, linklocal_port,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowTableWait(0));

    DeleteGlobalVrouterConfig();
    client->WaitForIdle();
}

// Linklocal flow with fabric-ip as loopback IP
TEST_F(FlowTest, LinkLocalFlow_loopback_1) {
    std::string mdata_ip = vmi_0->mdata_ip_addr().to_string();
    std::string fabric_ip("127.0.0.1");
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back(fabric_ip);
    TestLinkLocalService service = {
        "test_service", linklocal_ip, linklocal_port, "", fabric_ip_list,
        fabric_port
    };
    AddLinkLocalConfig(&service, 1);
    client->WaitForIdle();

    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3000,
                        linklocal_port, "vrf5", vmi_0->id(), 1),
            {
                new VerifyNat(vhost_ip_addr, mdata_ip, IPPROTO_TCP, fabric_port,
                              3000)
            }
        }
    };

    CreateFlow(nat_flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    EXPECT_TRUE(FlowGet(0, vhost_ip_addr, mdata_ip.c_str(), IPPROTO_TCP,
                        fabric_port, 3000,
                        vhost->flow_key_nh()->id()) != NULL);
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, linklocal_ip,
                        IPPROTO_TCP, 3000, linklocal_port,
                        GetFlowKeyNH(input[0].intf_id)) != NULL);

    //Delete forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));

    DelLinkLocalConfig();
    DeleteGlobalVrouterConfig();
    client->WaitForIdle();
}

//l2 linklocal flow and verify NAT is done
TEST_F(FlowTest, linklocal_l2) {
    std::string fabric_ip("1.2.3.4");
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back("1.2.3.4");
    TestLinkLocalService service = {
        "test_service", linklocal_ip,linklocal_port, "", fabric_ip_list,
        fabric_port
    };
    AddLinkLocalConfig(&service, 1);
    client->WaitForIdle();

    TxL2Packet(vmi_0->id(), input[0].mac, input[1].mac, input[0].addr,
               linklocal_ip, IPPROTO_UDP, 1, -1, 12345, linklocal_port);
    client->WaitForIdle();

    uint32_t nh_id = InterfaceTable::GetInstance()->
                     FindInterface(vmi_0->id())->flow_key_nh()->id();
    FlowEntry *fe = FlowGet(VrfGet("vrf5")->vrf_id(), input[0].addr,
                            linklocal_ip, IPPROTO_UDP, 12345, linklocal_port,
                            nh_id);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::NatFlow));
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::LinkLocalFlow));
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::LinkLocalBindLocalSrcPort));
    EXPECT_TRUE(FlowGet(0, fabric_ip, vhost_ip_addr,
                        IPPROTO_UDP, fabric_port, fe->linklocal_src_port(),
                        vhost->flow_key_nh()->id()) != NULL);

    FlushFlowTable();
    client->WaitForIdle();

    DelLinkLocalConfig();
    DeleteGlobalVrouterConfig();
    client->WaitForIdle();
}

TEST_F(FlowTest, LinkLocalFlow_Fail1) {
    std::string fabric_ip("1.2.3.4");
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back("1.2.3.4");
    TestLinkLocalService services[3] = {
        { "test_service1", linklocal_ip, linklocal_port, "", fabric_ip_list,
            fabric_port },
        { "test_service2", linklocal_ip, linklocal_port+1, "", fabric_ip_list,
            fabric_port+1 },
        { "test_service3", linklocal_ip, linklocal_port+2, "", fabric_ip_list,
            fabric_port+2 }
    };
    AddLinkLocalConfig(services, 3);
    client->WaitForIdle();

    // Only two link local flows are allowed simultaneously from a VM;
    // try creating 3 and check that 2 flows are created along with reverse
    // flows, while one flow along with its reverse is a short flow
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3000,
                        linklocal_port, "vrf5", vmi_0->id(), 1),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP,
                              fabric_port, 0)
            }
        },
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3001,
                        linklocal_port+1, "vrf5", vmi_0->id(), 2),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP,
                              fabric_port+1, 0)
            }
        },
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3002,
                        linklocal_port+2, "vrf5", vmi_0->id(), 3),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP,
                              fabric_port+2, 0)
            }
        }
    };

    FlowStatsTimerStartStop(agent_, true);
    CreateFlow(nat_flow, 3);
    client->WaitForIdle();
    EXPECT_EQ(6U, get_flow_proto()->FlowCount());
    uint16_t linklocal_src_port[3];
    for (uint32_t i = 0; i < 3; i++) {
        const FlowEntry *fe = nat_flow[i].pkt_.FlowFetch();
        if (fe->linklocal_src_port()) {
            linklocal_src_port[i] = fe->linklocal_src_port();
        } else {
            EXPECT_EQ(fe->data().in_vm_entry.fd(), VmFlowRef::kInvalidFd);
            linklocal_src_port[i] = fe->key().src_port;
        }

        EXPECT_TRUE(FlowGet(0, fabric_ip.c_str(), vhost_ip_addr,
                            IPPROTO_TCP, fabric_port+i, linklocal_src_port[i],
                            vhost->flow_key_nh()->id()) != NULL);
        EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, linklocal_ip,
                            IPPROTO_TCP, 3000+i, linklocal_port+i,
                            GetFlowKeyNH(input[0].intf_id)) != NULL);
        if (i == 2) {
            EXPECT_TRUE(fe->is_flags_set(FlowEntry::ShortFlow));
            EXPECT_TRUE(fe->short_flow_reason() ==
                        FlowEntry::SHORT_LINKLOCAL_SRC_NAT);
            EXPECT_EQ(fe->data().in_vm_entry.fd(), VmFlowRef::kInvalidFd);
            EXPECT_EQ(fe->data().in_vm_entry.port(), 0);
        }
    }
    FlowStatsTimerStartStop(agent_, false);

    //Delete forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    DeleteFlow(nat_flow + 1, 1);
    DeleteFlow(nat_flow + 2, 1);
    client->WaitForIdle();
    for (uint32_t i = 0; i < 3; i++) {
        EXPECT_TRUE(FlowFail(0, fabric_ip.c_str(), vhost_ip_addr,
                             IPPROTO_TCP, fabric_port+i,
                             linklocal_src_port[i],
                             vhost->flow_key_nh()->id()));
        EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm1_ip,
                             linklocal_ip, IPPROTO_TCP, 3000+i,
                             linklocal_port+i, GetFlowKeyNH(input[0].intf_id)));
    }
    EXPECT_TRUE(FlowTableWait(0));

    DelLinkLocalConfig();
    DeleteGlobalVrouterConfig();
    AddLinkLocalConfig(services, 3);
    client->WaitForIdle();
}

TEST_F(FlowTest, LinkLocalFlow_Fail2) {
    std::string fabric_ip("1.2.3.4");
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back("1.2.3.4");
    TestLinkLocalService services[4] = {
        { "test_service1", linklocal_ip, linklocal_port, "", fabric_ip_list,
            fabric_port },
        { "test_service2", linklocal_ip, linklocal_port+1, "", fabric_ip_list,
            fabric_port+1 },
        { "test_service3", linklocal_ip, linklocal_port+2, "", fabric_ip_list,
            fabric_port+2 },
        { "test_service4", linklocal_ip, linklocal_port+3, "", fabric_ip_list,
            fabric_port+3 }
    };
    AddLinkLocalConfig(services, 4);
    client->WaitForIdle();

    // Only three link local flows are allowed simultaneously in the agent;
    // try creating 4 and check that 3 flows are created along with reverse
    // flows, while one flow along with its reverse is a short flow
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3000,
                        linklocal_port, "vrf5", vmi_0->id(), 1),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP,
                              fabric_port, 0)
            }
        },
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3001,
                        linklocal_port+1, "vrf5", vmi_0->id(), 2),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP,
                              fabric_port+1, 0)
            }
        },
        {
            TestFlowPkt(Address::INET, vm2_ip, linklocal_ip, IPPROTO_TCP, 3002,
                        linklocal_port+2, "vrf5", vmi_1->id(), 3),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP,
                              fabric_port+2, 0)
            }
        },
        {
            TestFlowPkt(Address::INET, vm2_ip, linklocal_ip, IPPROTO_TCP, 3003,
                        linklocal_port+3, "vrf5", vmi_1->id(), 4),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP,
                              fabric_port+3, 0)
            }
        }
    };

    FlowStatsTimerStartStop(agent_, true);
    CreateFlow(nat_flow, 4);
    client->WaitForIdle();
    EXPECT_EQ(8U, get_flow_proto()->FlowCount());
    uint16_t linklocal_src_port[4];
    for (uint32_t i = 0; i < 4; i++) {
        const FlowEntry *fe = nat_flow[i].pkt_.FlowFetch();
        if (fe->linklocal_src_port()) {
            linklocal_src_port[i] = fe->linklocal_src_port();
        } else {
            EXPECT_EQ(fe->data().in_vm_entry.fd(), VmFlowRef::kInvalidFd);
            linklocal_src_port[i] = fe->key().src_port;
        }

        EXPECT_TRUE(FlowGet(0, fabric_ip.c_str(), vhost_ip_addr, IPPROTO_TCP,
                            fabric_port+i, linklocal_src_port[i],
                            vhost->flow_key_nh()->id()) != NULL);
        if (i <= 1)
            EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, linklocal_ip,
                                IPPROTO_TCP, 3000+i, linklocal_port+i,
                                GetFlowKeyNH(input[0].intf_id)) != NULL);
        else
            EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm2_ip, linklocal_ip,
                                IPPROTO_TCP, 3000+i, linklocal_port+i,
                                GetFlowKeyNH(input[1].intf_id)) != NULL);
        if (i == 3) {
            EXPECT_TRUE(fe->is_flags_set(FlowEntry::ShortFlow));
            EXPECT_TRUE(fe->short_flow_reason() ==
                        FlowEntry::SHORT_LINKLOCAL_SRC_NAT);
            EXPECT_EQ(fe->data().in_vm_entry.fd(), VmFlowRef::kInvalidFd);
            EXPECT_EQ(fe->data().in_vm_entry.port(), 0);
        }
    }
    FlowStatsTimerStartStop(agent_, false);

    //Delete forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    DeleteFlow(nat_flow + 1, 1);
    DeleteFlow(nat_flow + 2, 1);
    DeleteFlow(nat_flow + 3, 1);
    client->WaitForIdle();
    for (uint32_t i = 0; i < 4; i++) {
        EXPECT_TRUE(FlowFail(0, fabric_ip.c_str(), vhost_ip_addr,
                             IPPROTO_TCP, fabric_port+i,
                             linklocal_src_port[i],
                             vhost->flow_key_nh()->id()));
        if (i <= 1)
            EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm1_ip,
                                 linklocal_ip, IPPROTO_TCP, 3000+i,
                                 linklocal_port+i,
                                 GetFlowKeyNH(input[0].intf_id)));
        else
            EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm2_ip, linklocal_ip,
                                 IPPROTO_TCP, 3000+i, linklocal_port+i,
                                 GetFlowKeyNH(input[0].intf_id)));
    }
    EXPECT_TRUE(FlowTableWait(0));

    DelLinkLocalConfig();
    DeleteGlobalVrouterConfig();
    client->WaitForIdle();
}

// Check that flow limit per VM works
TEST_F(FlowTest, FlowLimit_1) {
    uint32_t vm_flows = agent_->max_vm_flows();
    agent_->set_max_vm_flows(3);

    /* Add Local VM route of vrf3 to vrf5 */
    CreateLocalRoute("vrf5", vm4_ip, vmi_3, 19);
    /* Add Local VM route of vrf5 to vrf3 */
    CreateLocalRoute("vrf3", vm1_ip, vmi_0, 16);

    TestFlow flow[] = {
        //Send a ICMP request from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, 1, 0, 0, "vrf5",
                    vmi_0->id(), 1),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an ICMP reply from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, 1, 0, 0, "vrf3",
                    vmi_3->id(), 2),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 200, 300,
                        "vrf5", vmi_0->id(), 3),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 300, 200,
                        "vrf3", vmi_3->id(), 4),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };

    FlowStatsTimerStartStop(agent_, true);
    CreateFlow(flow, 4);
    client->WaitForIdle();
    VmInterface *intf = static_cast<VmInterface*>(agent_->interface_table()->\
                                                  FindInterface(vmi_3->id()));
    int nh_id = intf->flow_key_nh()->id();
    EXPECT_EQ(4U, get_flow_proto()->FlowCount());
    // Validate interface is set to drop new flows after flow limit is reached
    EXPECT_TRUE(intf->drop_new_flows());
    FlowEntry *fe = FlowGet(VrfGet("vrf3")->vrf_id(), vm4_ip, vm1_ip,
                            IPPROTO_TCP, 300, 200, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_FLOW_LIMIT);
    EXPECT_TRUE(agent()->stats()->flow_drop_due_to_max_limit() > 0);
    FlowStatsTimerStartStop(agent_, false);

    //1. Remove remote VM routes
    DeleteRoute("vrf5", vm4_ip);
    DeleteRoute("vrf3", vm1_ip);
    client->WaitForIdle();
    FlushFlowTable();
    client->WaitForIdle();
    // Validate interface is not dropping new flows after flows age-out
    EXPECT_FALSE(intf->drop_new_flows());
    client->WaitForIdle();
    agent_->set_max_vm_flows(vm_flows);
}

// Check that flow limit per VM includes short flows in the system
TEST_F(FlowTest, FlowLimit_2) {
    uint32_t vm_flows = agent_->max_vm_flows();
    agent_->set_max_vm_flows(3);

    TestFlow short_flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm1_ip, "115.115.115.115", 1, 0, 0, "vrf5",
                    vmi_0->id()),
            {
                new ShortFlow()
            }
        }
    };
    FlowStatsTimerStartStop(agent_, true);
    CreateFlow(short_flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());
    int nh_id = agent_->interface_table()->FindInterface
        (vmi_0->id())->flow_key_nh()->id();
    FlowEntry *fe = FlowGet(1, vm1_ip, "115.115.115.115", 1,
                            0, 0, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_NO_DST_ROUTE);
    FlowStatsTimerStartStop(agent_, false);

    /* Add Local VM route of vrf3 to vrf5 */
    CreateLocalRoute("vrf5", vm4_ip, vmi_3, 19);
    /* Add Local VM route of vrf5 to vrf3 */
    CreateLocalRoute("vrf3", vm1_ip, vmi_0, 16);

    TestFlow flow[] = {
        //Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 100, 101,
                        "vrf5", vmi_0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 101, 100,
                        "vrf3", vmi_3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 200, 300,
                        "vrf5", vmi_0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 300, 200,
                        "vrf3", vmi_3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };

    FlowStatsTimerStartStop(agent_, true);
    CreateFlow(flow, 4);
    client->WaitForIdle();
    EXPECT_EQ(6U, get_flow_proto()->FlowCount());

    nh_id = agent_->interface_table()->FindInterface
        (vmi_3->id())->flow_key_nh()->id();
    fe = FlowGet(VrfGet("vrf3")->vrf_id(), vm4_ip, vm1_ip,
                 IPPROTO_TCP, 300, 200, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_FLOW_LIMIT);
    fe = FlowGet(VrfGet("vrf3")->vrf_id(), vm4_ip, vm1_ip,
                 IPPROTO_TCP, 101, 100, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true);
    nh_id = agent_->interface_table()->FindInterface
        (vmi_0->id())->flow_key_nh()->id();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm4_ip,
                 IPPROTO_TCP, 100, 101, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true);
    FlowStatsTimerStartStop(agent_, false);

    //1. Remove remote VM routes
    DeleteRoute("vrf5", vm4_ip);
    DeleteRoute("vrf3", vm1_ip);
    client->WaitForIdle();
    client->WaitForIdle();
    agent_->set_max_vm_flows(vm_flows);
}

// Configure max-flows on vn level and expect vmi's to match it
TEST_F(FlowTest, MaxFlows_1) {
    // Set max-flows=3 for vn3
    SetVnMaxFlows("vn3", 3, 3);

    // Set max-flows=3 for vn5
    SetVnMaxFlows("vn5", 5, 3);

    client->WaitForIdle();

    // Add Local VM route of vrf3 to vrf5
    CreateLocalRoute("vrf5", vm4_ip, vmi_3, 19);
    // Add Local VM route of vrf5 to vrf3
    CreateLocalRoute("vrf3", vm1_ip, vmi_0, 16);

    VmInterface *intf_0 = static_cast<VmInterface*>(agent_->interface_table()->\
                                                  FindInterface(vmi_0->id()));

    VmInterface *intf_1 = static_cast<VmInterface*>(agent_->interface_table()->\
                                                  FindInterface(vmi_1->id()));

    // Check both interfaces in vn5 have max_flows=3 since for vn5 max-flows=3
    EXPECT_EQ(3U, intf_0->max_flows());
    EXPECT_EQ(3U, intf_1->max_flows());

    TestFlow flow[] = {
        //Send a ICMP request from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, 1, 0, 0, "vrf5",
                    vmi_0->id(), 1),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an ICMP reply from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, 1, 0, 0, "vrf3",
                    vmi_3->id(), 2),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 200, 300,
                        "vrf5", vmi_0->id(), 3),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 300, 200,
                        "vrf3", vmi_3->id(), 4),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };

    FlowStatsTimerStartStop(agent_, true);
    client->WaitForIdle();
    CreateFlow(flow, 4);
    client->WaitForIdle();
    VmInterface *intf = static_cast<VmInterface*>(agent_->interface_table()->\
                                                  FindInterface(vmi_3->id()));
    int nh_id = intf->flow_key_nh()->id();
    EXPECT_EQ(4U, get_flow_proto()->FlowCount());
    //Validate interface is set to drop new flows after flow limit is reached
    EXPECT_EQ(3U, intf->max_flows());
    EXPECT_TRUE(intf->drop_new_flows());
    FlowEntry *fe = FlowGet(VrfGet("vrf3")->vrf_id(), vm4_ip, vm1_ip,
                            IPPROTO_TCP, 300, 200, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_FLOW_LIMIT);
    EXPECT_TRUE(agent()->stats()->flow_drop_due_to_max_limit() > 0);
    FlowStatsTimerStartStop(agent_, false);

    //1. Remove remote VM routes
    DeleteRoute("vrf5", vm4_ip);
    DeleteRoute("vrf3", vm1_ip);
    client->WaitForIdle();
    FlushFlowTable();
    client->WaitForIdle();
    // Validate interface is not dropping new flows after flows age-out
    EXPECT_FALSE(intf->drop_new_flows());
    client->WaitForIdle();

    // Disable max-flows for vmi0 and vmi3
    SetVmiMaxFlows("vmi_3", 9, 0);
    SetVmiMaxFlows("vmi_0", 6, 0);
    client->WaitForIdle();

    // Expect max flows from vn level as max_flow=0 at vmi
    EXPECT_EQ(3U, intf_0->max_flows());
    EXPECT_EQ(3U, intf_1->max_flows());

    // Disable max-flows for vn3
    SetVnMaxFlows("vn3", 3, 0);
    // Disable max-flows for vn5
    SetVnMaxFlows("vn5", 5, 0);
    client->WaitForIdle();

    // Expect max flows disabled on vmi
    EXPECT_EQ(0U, intf_0->max_flows());
    EXPECT_EQ(0U, intf_1->max_flows());
    client->WaitForIdle();
}

// Verify that max_flows limit on vmi, includes short flows
TEST_F(FlowTest, MaxFlows_2) {

    // Set max-flows=3 for vn3
    SetVnMaxFlows("vn3", 3, 3);

    // Set max-flows=3 for vn5
    SetVnMaxFlows("vn5", 5, 3);

    client->WaitForIdle();

    EXPECT_EQ(3U, vmi_0->max_flows());
    EXPECT_EQ(3U, vmi_1->max_flows());

    TestFlow short_flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm1_ip, "115.115.115.115", 1, 0, 0, "vrf5",
                    vmi_0->id()),
            {
                new ShortFlow()
            }
        }
    };
    FlowStatsTimerStartStop(agent_, true);
    CreateFlow(short_flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());
    int nh_id = agent_->interface_table()->FindInterface
        (vmi_0->id())->flow_key_nh()->id();
    FlowEntry *fe = FlowGet(1, vm1_ip, "115.115.115.115", 1,
                            0, 0, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_NO_DST_ROUTE);
    FlowStatsTimerStartStop(agent_, false);

    /* Add Local VM route of vrf3 to vrf5 */
    CreateLocalRoute("vrf5", vm4_ip, vmi_3, 19);
    /* Add Local VM route of vrf5 to vrf3 */
    CreateLocalRoute("vrf3", vm1_ip, vmi_0, 16);

    TestFlow flow[] = {
        // Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 100, 101,
                        "vrf5", vmi_0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        // Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 101, 100,
                        "vrf3", vmi_3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        // Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 200, 300,
                        "vrf5", vmi_0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        // Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 300, 200,
                        "vrf3", vmi_3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };

    FlowStatsTimerStartStop(agent_, true);
    CreateFlow(flow, 4);
    client->WaitForIdle();
    EXPECT_EQ(6U, get_flow_proto()->FlowCount());

    nh_id = agent_->interface_table()->FindInterface
        (vmi_3->id())->flow_key_nh()->id();
    fe = FlowGet(VrfGet("vrf3")->vrf_id(), vm4_ip, vm1_ip,
                 IPPROTO_TCP, 300, 200, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_FLOW_LIMIT);
    fe = FlowGet(VrfGet("vrf3")->vrf_id(), vm4_ip, vm1_ip,
                 IPPROTO_TCP, 101, 100, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true);
    nh_id = agent_->interface_table()->FindInterface
        (vmi_0->id())->flow_key_nh()->id();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm4_ip,
                 IPPROTO_TCP, 100, 101, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true);
    FlowStatsTimerStartStop(agent_, false);

    // 1. Remove remote VM routes
    DeleteRoute("vrf5", vm4_ip);
    DeleteRoute("vrf3", vm1_ip);
    client->WaitForIdle();

    // Disable max-flow for vn3
    SetVnMaxFlows("vn3", 3, 0);
    // Disable max-flows for vn5
    SetVnMaxFlows("vn5", 5, 0);
    client->WaitForIdle();
}

//Configure max flows both at vn and vmi, verify value at vmi is used
TEST_F(FlowTest, MaxFlows_3) {

    // Set max-flows=10 for vn3 20 for vn5
    SetVnMaxFlows("vn3", 3, 10);
    SetVnMaxFlows("vn5", 5, 20);

    client->WaitForIdle();

    // Set max-flows=3 for vmi_0 and vmi_5
    SetVmiMaxFlows("vmi_3", 9, 3);
    SetVmiMaxFlows("vmi_0", 6, 3);

    client->WaitForIdle();

    // vmi_0 and vmi_3 have max-flows set to 3
    EXPECT_EQ(3U, vmi_0->max_flows());
    EXPECT_EQ(3U, vmi_3->max_flows());

    // vmi_1 has max_flows set to 20, same as vn5
    EXPECT_EQ(20U, vmi_1->max_flows());

    // Add Local VM route of vrf3 to vrf5
    CreateLocalRoute("vrf5", vm4_ip, vmi_3, 19);

    // Add Local VM route of vrf5 to vrf3
    CreateLocalRoute("vrf3", vm1_ip, vmi_0, 16);

    TestFlow flow[] = {
        //Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 100, 101,
                        "vrf5", vmi_0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 101, 100,
                        "vrf3", vmi_3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 200, 300,
                        "vrf5", vmi_0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 300, 200,
                        "vrf3", vmi_3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };

    FlowStatsTimerStartStop(agent_, true);
    CreateFlow(flow, 4);
    client->WaitForIdle();

    VmInterface *intf = static_cast<VmInterface*>(agent_->interface_table()->\
                                                  FindInterface(vmi_3->id()));
    int nh_id = intf->flow_key_nh()->id();
    EXPECT_EQ(4U, get_flow_proto()->FlowCount());
    // Validate interface is set to drop new flows after flow limit is reached
    EXPECT_EQ(3U, intf->max_flows());
    EXPECT_TRUE(intf->drop_new_flows());
    FlowEntry *fe = FlowGet(VrfGet("vrf3")->vrf_id(), vm4_ip, vm1_ip,
                            IPPROTO_TCP, 300, 200, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_FLOW_LIMIT);
    EXPECT_TRUE(agent()->stats()->flow_drop_due_to_max_limit() > 0);
    FlowStatsTimerStartStop(agent_, false);

    // Remove remote VM routes
    DeleteRoute("vrf5", vm4_ip);
    DeleteRoute("vrf3", vm1_ip);
    client->WaitForIdle();
    FlushFlowTable();
    client->WaitForIdle();
    // Validate interface is not dropping new flows after flows age-out
    EXPECT_FALSE(intf->drop_new_flows());
    client->WaitForIdle();

    // Restore max-flows for vn3 and vn5
    SetVnMaxFlows("vn3", 3, 0);
    SetVnMaxFlows("vn5", 5, 0);

    // vmi_0 and vmi_3 have max-flows set to 3
    // since  max_flow=3 on vmi was set by config message
    EXPECT_EQ(3U, vmi_0->max_flows());
    EXPECT_EQ(3U, vmi_3->max_flows());

    // max_flows for vmi_1 was set from vn5 and not from config message
    // since vn5 max_flows=0 expect vmi_1 max_flows=0
    EXPECT_EQ(0U, vmi_1->max_flows());
    client->WaitForIdle();

    // Restore max-flows for vmi0 and vmi3
    SetVmiMaxFlows("vmi_3", 9, 0);
    SetVmiMaxFlows("vmi_0", 6, 0);
    client->WaitForIdle();

    // vmi_0 and vmi_3 have max-flows now set to 0, as max_flow=0 at vn level
    EXPECT_EQ(0U, vmi_0->max_flows());
    EXPECT_EQ(0U, vmi_3->max_flows());
    client->WaitForIdle();
}

TEST_F(FlowTest, MaxFlowsPreference) {

    // Set flow limit to 3 for each vm on compute
    uint32_t vm_flows = agent_->max_vm_flows();
    agent_->set_max_vm_flows(3);

    // Set max-flows=3 for vn3
    SetVnMaxFlows("vn3", 3, 5);

    // Set max-flows=3 for vn5
    SetVnMaxFlows("vn5", 5, 5);

    client->WaitForIdle();

    // vmi_0 and vmi1 should have max_flows set to 5 as vn3 and vn5
    EXPECT_EQ(5U, vmi_0->max_flows());
    EXPECT_EQ(5U, vmi_1->max_flows());

    // Add Local VM route of vrf3 to vrf5
    CreateLocalRoute("vrf5", vm4_ip, vmi_3, 19);
    // Add Local VM route of vrf5 to vrf3
    CreateLocalRoute("vrf3", vm1_ip, vmi_0, 16);

    TestFlow flow[] = {
        // Send a ICMP request from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, 1, 0, 0, "vrf5",
                    vmi_0->id(), 1),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        // Send an ICMP reply from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, 1, 0, 0, "vrf3",
                    vmi_3->id(), 2),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };

    client->WaitForIdle();
    CreateFlow(flow, 2);
    client->WaitForIdle();
    VmInterface *intf = static_cast<VmInterface*>(agent_->interface_table()->\
                                                  FindInterface(vmi_3->id()));
    EXPECT_FALSE(intf->drop_new_flows());
    TestFlow flow1[] = {
        // Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 200, 300,
                        "vrf5", vmi_0->id(), 3),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        // Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 300, 200,
                        "vrf3", vmi_3->id(), 4),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };
    FlowStatsTimerStartStop(agent_, true);
    client->WaitForIdle();
    CreateFlow(flow1, 2);
    client->WaitForIdle();

    int nh_id = intf->flow_key_nh()->id();
    EXPECT_EQ(4U, get_flow_proto()->FlowCount());
    // Validate interface is set to drop new flows after flow limit is reached
    EXPECT_EQ(5U, intf->max_flows());
    EXPECT_TRUE(intf->drop_new_flows());
    FlowEntry *fe = FlowGet(VrfGet("vrf3")->vrf_id(), vm4_ip, vm1_ip,
                            IPPROTO_TCP, 300, 200, nh_id);
    // Expect 4th flow to be short flow, only 3 flows allowed per vmi(preffered), 4 flows at vm level.
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_FLOW_LIMIT);
    EXPECT_TRUE(agent()->stats()->flow_drop_due_to_max_limit() > 0);
    FlowStatsTimerStartStop(agent_, false);

    // 1. Remove remote VM routes
    DeleteRoute("vrf5", vm4_ip);
    DeleteRoute("vrf3", vm1_ip);
    client->WaitForIdle();
    FlushFlowTable();
    client->WaitForIdle();

    // Validate interface is not dropping new flows after flows age-out
    EXPECT_FALSE(intf->drop_new_flows());
    client->WaitForIdle();

    agent_->set_max_vm_flows(vm_flows);

    // Disable max-flow for vn3
    SetVnMaxFlows("vn3", 3, 0);
    // Disable max-flows for vn5
    SetVnMaxFlows("vn5", 5, 0);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client =
        TestInit(init_file, ksync_init, true, true, true, (1000000 * 60 * 10),
                 (3 * 60 * 1000));
    if (vm.count("config")) {
        eth_itf = Agent::GetInstance()->fabric_interface_name();
    } else {
        eth_itf = "eth0";
        PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                                eth_itf,
                                Agent::GetInstance()->fabric_vrf_name(),
                                PhysicalInterface::FABRIC,
                                PhysicalInterface::ETHERNET, false,
                                boost::uuids::nil_uuid(), Ip4Address(0),
                                Interface::TRANSPORT_ETHERNET);
        client->WaitForIdle();
    }

    FlowTest::TestSetup(ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
