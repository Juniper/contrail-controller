/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include <base/os.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <base/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "pkt/flow_table.h"
#include "services/services_init.h"

#define MAX_VNET 4
int fd_table[MAX_VNET];

#define vm1_ip "11.1.1.1"
#define vm2_ip "11.1.1.2"
#define fabric_ip "1.2.3.4"

#define remote_server_ip "10.1.2.10"
#define vhost_ip_addr "10.1.2.1"
#define linklocal_ip "169.254.1.10"
#define linklocal_port 4000
#define fabric_port 8000

const Interface *vhost;
struct PortInfo input[] = {
        {"vmi0", 1, vm1_ip, "00:00:00:01:01:01", 1, 1},
        {"vmi1", 2, vm2_ip, "00:00:00:01:01:02", 1, 2},
};
IpamInfo ipam_info[] = {
    {"11.1.1.0", 24, "11.1.1.10"},
};

typedef enum {
    INGRESS = 0,
    EGRESS = 1,
    BIDIRECTION = 2
} AclDirection;

int hash_id;
VmInterface *vmi0;
VmInterface *vmi1;
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

protected:
    virtual void SetUp() {
        agent_->flow_stats_manager()->set_delete_short_flow(false);
        Ip4Address rid = Ip4Address::from_string(vhost_ip_addr);
        agent_->set_router_id(rid);
        agent_->set_compute_node_ip(rid);
        hash_id = 1;

        EXPECT_EQ(0U, get_flow_proto()->FlowCount());
        client->Reset();
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(5);

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        vmi0 = VmInterfaceGet(input[0].intf_id);
        assert(vmi0);

        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));
        vmi1 = VmInterfaceGet(input[1].intf_id);
        assert(vmi1);

        EXPECT_EQ(5U, agent()->interface_table()->Size());
        EXPECT_EQ(2U, PortSubscribeSize(agent_));
        EXPECT_EQ(2U, agent()->vm_table()->Size());
        EXPECT_EQ(1U, agent()->vn_table()->Size());
        client->WaitForIdle();

        std::vector<std::string> fabric_ip_list;
        fabric_ip_list.push_back(fabric_ip);
        TestLinkLocalService service = {
            "test_service", linklocal_ip, linklocal_port, "", fabric_ip_list,
            fabric_port
        };
        AddLinkLocalConfig(&service, 1);
        client->WaitForIdle();

        vmi0_flow_nh_ = vmi0->flow_key_nh()->id();
        vmi1_flow_nh_ = vmi1->flow_key_nh()->id();
        vhost0_flow_nh_ = vhost->flow_key_nh()->id();
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();

        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle(3);
        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));
        DelIPAM("vn1");
        client->WaitForIdle();

        EXPECT_EQ(3U, agent()->interface_table()->Size());
        EXPECT_EQ(0U, PortSubscribeSize(agent_));
        EXPECT_EQ(0U, agent()->vm_table()->Size());
        EXPECT_EQ(0U, agent()->vn_table()->Size());
        EXPECT_EQ(0U, agent()->acl_table()->Size());
        EXPECT_EQ(2U, agent()->vrf_table()->Size());

        EXPECT_EQ(flow_proto_->linklocal_flow_count(), 0U);
        EXPECT_EQ(flow_proto_->linklocal_flow_count(), 0U);
        FlowTable *table = flow_proto_->GetTable(0);
        EXPECT_EQ(table->linklocal_flow_info_map().size(), 0U);

        DeleteGlobalVrouterConfig();
        client->WaitForIdle();
    }

    Agent *agent() {return agent_;}
    FlowProto *get_flow_proto() const { return flow_proto_; }

public:
    Agent *agent_;
    FlowProto *flow_proto_;
    uint32_t vmi0_flow_nh_;
    uint32_t vmi1_flow_nh_;
    uint32_t vhost0_flow_nh_;
};

TEST_F(FlowTest, LinkLocalFlow_1) {
    TxTcpPacket(vmi0->id(), vm1_ip, linklocal_ip, 3000, linklocal_port, false,
                vmi0->vrf_id());
    client->WaitForIdle();
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    const FlowEntry *fe = FlowGet(VrfGet("vrf1")->vrf_id(), vm1_ip,
                                  linklocal_ip, IPPROTO_TCP, 3000,
                                  linklocal_port, vmi0_flow_nh_);
    uint16_t linklocal_src_port = fe->linklocal_src_port();

    EXPECT_TRUE(FlowGet(0, fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port,
                        linklocal_src_port, vhost0_flow_nh_) != NULL);

    //Delete forward flow and expect both flows to be deleted
    FlowDelete("vrf1", vm1_ip, linklocal_ip, IPPROTO_TCP, 3000, linklocal_port,
               vmi0_flow_nh_);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, LinkLocalFlow_NoVm_1) {
    // Delete VMI - VM link
    DelLink("virtual-machine-interface", "vmi0", "virtual-machine", "vm1");
    client->WaitForIdle();

    TxTcpPacket(vmi0->id(), vm1_ip, linklocal_ip, 3000, linklocal_port, false,
                vmi0->vrf_id());
    client->WaitForIdle();
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    const FlowEntry *fe = FlowGet(VrfGet("vrf1")->vrf_id(), vm1_ip,
                                  linklocal_ip, IPPROTO_TCP, 3000,
                                  linklocal_port, vmi0_flow_nh_);
    uint16_t linklocal_src_port = fe->linklocal_src_port();

    EXPECT_TRUE(FlowGet(0, fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port,
                        linklocal_src_port, vhost0_flow_nh_) != NULL);

    //Delete forward flow and expect both flows to be deleted
    FlowDelete("vrf1", vm1_ip, linklocal_ip, IPPROTO_TCP, 3000, linklocal_port,
               vmi0_flow_nh_);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, LinkLocalFlow_VmiDel_1) {
    TxTcpPacket(vmi0->id(), vm1_ip, linklocal_ip, 3000, linklocal_port, false,
                vmi0->vrf_id());
    client->WaitForIdle();
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    const FlowEntry *fe = FlowGet(VrfGet("vrf1")->vrf_id(), vm1_ip,
                                  linklocal_ip, IPPROTO_TCP, 3000,
                                  linklocal_port, vmi0_flow_nh_);
    uint16_t linklocal_src_port = fe->linklocal_src_port();

    EXPECT_TRUE(FlowGet(0, fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port,
                        linklocal_src_port, vhost0_flow_nh_) != NULL);

    DeleteVmportEnv(input, 2, true, 1);
    client->WaitForIdle(3);
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, LinkLocalFlow_Eviction_1) {
    TxTcpPacket(vmi0->id(), vm1_ip, linklocal_ip, 3000, linklocal_port, false,
                vmi0->vrf_id());
    client->WaitForIdle();
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    const FlowEntry *flow1 = FlowGet(VrfGet("vrf1")->vrf_id(), vm1_ip,
                                     linklocal_ip, IPPROTO_TCP, 3000,
                                     linklocal_port, vmi0_flow_nh_);

    uint16_t linklocal_src_port = flow1->linklocal_src_port();
    const FlowEntry *flow2 = FlowGet(0, fabric_ip, vhost_ip_addr, IPPROTO_TCP,
                                     fabric_port, linklocal_src_port,
                                     vhost0_flow_nh_);
    EXPECT_TRUE(flow2 != NULL);

    // Send packet again to simulate eviction
    // flow2 will be unlinked with flow1
    // flow3 will be created and linked to flow1
    TxTcpPacket(vmi0->id(), vm1_ip, linklocal_ip, 3000, linklocal_port, false,
                vmi0->vrf_id());
    client->WaitForIdle();
    EXPECT_EQ(3U, get_flow_proto()->FlowCount());

    flow1 = FlowGet(VrfGet("vrf1")->vrf_id(), vm1_ip, linklocal_ip,
                    IPPROTO_TCP, 3000, linklocal_port, vmi0_flow_nh_);
    EXPECT_TRUE(flow1 != NULL);

    flow2 = FlowGet(0, fabric_ip, vhost_ip_addr, IPPROTO_TCP,
                    fabric_port, linklocal_src_port, vhost0_flow_nh_);
    EXPECT_TRUE(flow2 != NULL);
    EXPECT_TRUE(flow2->reverse_flow_entry() == NULL);

    uint16_t linklocal_src_port1 = flow1->linklocal_src_port();
    const FlowEntry *flow3 = FlowGet(0, fabric_ip, vhost_ip_addr, IPPROTO_TCP,
                                     fabric_port, linklocal_src_port1,
                                     vhost0_flow_nh_);
    EXPECT_TRUE(flow3 != NULL);
    EXPECT_TRUE(flow3->reverse_flow_entry() == flow1);
    EXPECT_TRUE(flow1->reverse_flow_entry() == flow3);

    FlowDelete("vrf1", vm1_ip, linklocal_ip, IPPROTO_TCP, 3000, linklocal_port,
               vmi0_flow_nh_);
    FlowDelete("vrf1", fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port,
               linklocal_src_port, vhost0_flow_nh_);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, LinkLocalFlow_RevEviction_1) {
    TxTcpPacket(vmi0->id(), vm1_ip, linklocal_ip, 3000, linklocal_port, false,
                vmi0->vrf_id());
    client->WaitForIdle();
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    const FlowEntry *flow1 = FlowGet(VrfGet("vrf1")->vrf_id(), vm1_ip,
                                  linklocal_ip, IPPROTO_TCP, 3000,
                                  linklocal_port, vmi0_flow_nh_);
    EXPECT_TRUE(flow1 != NULL);
    uint16_t linklocal_src_port = flow1->linklocal_src_port();

    const FlowEntry *flow2 = FlowGet(0, fabric_ip, vhost_ip_addr, IPPROTO_TCP,
                                     fabric_port, linklocal_src_port,
                                     vhost0_flow_nh_);
    EXPECT_TRUE(flow2 != NULL);

    // Send reverse packet to simulate eviction
    // flow2 will be unlinked with flow1
    // flow3 will be created and linked to flow2
    TxTcpPacket(vhost->id(), fabric_ip, vhost_ip_addr, fabric_port,
                linklocal_src_port, false, 0);
    client->WaitForIdle();
    EXPECT_EQ(3U, get_flow_proto()->FlowCount());

    // flow1 unlinked from flow2
    flow1 = FlowGet(VrfGet("vrf1")->vrf_id(), vm1_ip, linklocal_ip,
                    IPPROTO_TCP, 3000, linklocal_port, vmi0_flow_nh_);
    EXPECT_TRUE(flow1 != NULL);
    EXPECT_TRUE(flow1->reverse_flow_entry() == NULL);

    flow2 = FlowGet(0, fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port,
                    linklocal_src_port, vhost->flow_key_nh()->id());
    EXPECT_TRUE(flow1 != NULL);


    const FlowEntry *flow3 = FlowGet(0, vhost_ip_addr, fabric_ip, IPPROTO_TCP,
                                     linklocal_src_port, fabric_port,
                                     vhost->flow_key_nh()->id());
    EXPECT_TRUE(flow3 != NULL);

    // Delete forward flow and expect both flows to be deleted
    FlowDelete("vrf1", vm1_ip, linklocal_ip, IPPROTO_TCP, 3000, linklocal_port,
               vmi0_flow_nh_);
    FlowDelete("vrf1", fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port,
               linklocal_src_port, vhost0_flow_nh_);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

// Linklocal flow with fabric-ip as loopback IP
TEST_F(FlowTest, LinkLocalFlow_loopback_1) {
    std::string mdata_ip = vmi0->mdata_ip_addr().to_string();
    std::string loopback_ip("127.0.0.1");
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back(loopback_ip);
    TestLinkLocalService service = {
        "test_service", linklocal_ip, linklocal_port, "", fabric_ip_list,
        fabric_port
    };
    AddLinkLocalConfig(&service, 1);
    client->WaitForIdle();

    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3000,
                        linklocal_port, "vrf1", vmi0->id(), 1),
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
    EXPECT_TRUE(FlowGet(VrfGet("vrf1")->vrf_id(), vm1_ip, linklocal_ip,
                        IPPROTO_TCP, 3000, linklocal_port,
                        GetFlowKeyNH(input[0].intf_id)) != NULL);

    //Delete forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));

    DelLinkLocalConfig();
    client->WaitForIdle();
}


//l2 linklocal flow and verify NAT is done
TEST_F(FlowTest, linklocal_l2) {
    TxL2Packet(vmi0->id(), input[0].mac, input[1].mac, input[0].addr,
               linklocal_ip, IPPROTO_UDP, 1, -1, 12345, linklocal_port);
    client->WaitForIdle();

    uint32_t nh_id = InterfaceTable::GetInstance()->
                     FindInterface(vmi0->id())->flow_key_nh()->id();
    FlowEntry *fe = FlowGet(VrfGet("vrf1")->vrf_id(), input[0].addr,
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
}

TEST_F(FlowTest, LinkLocalFlow_update) {
    // delete linklocal config first to create route without being marked linklocal
    DelLinkLocalConfig();
    client->WaitForIdle();

    boost::system::error_code ec;
    BgpPeer *peer = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                  "xmpp channel");
    client->WaitForIdle();

    Ip4Address addr = Ip4Address::from_string("0.0.0.0");
    Ip4Address gw = Ip4Address::from_string(remote_server_ip);
    Inet4TunnelRouteAdd(peer, "vrf1", addr, 0, gw, TunnelType::MplsType(),
                        20, vmi0->vn()->GetName(), SecurityGroupList(),
                        TagList(), PathPreference());
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (RouteFind("vrf1", addr, 0) == true));

    TxTcpPacket(vmi0->id(), vm1_ip, linklocal_ip, 3000, linklocal_port, false,
                1, vmi0->vrf_id());
    client->WaitForIdle();
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    FlowEntryPtr fe = FlowGet(VrfGet("vrf1")->vrf_id(), vm1_ip,
                              linklocal_ip, IPPROTO_TCP, 3000,
                              linklocal_port, vmi0_flow_nh_);

    // Enable link local service, after the packet trap resulted in
    // flow creation
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back(fabric_ip);
    TestLinkLocalService service = {
        "test_service", linklocal_ip, linklocal_port, "", fabric_ip_list,
        fabric_port
    };
    AddLinkLocalConfig(&service, 1);
    EXPECT_TRUE(fe->IsShortFlow() == false);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, fe->is_flags_set(FlowEntry::LinkLocalFlow));
    fe = NULL;
    FlushFlowTable();
    client->WaitForIdle();

    agent()->fabric_inet4_unicast_table()->
        DeleteReq(peer, "vrf1", addr, 0, new ControllerVmRoute(peer));
    client->WaitForIdle();
    WAIT_FOR(1000, 1, (RouteFind("vrf1", addr, 0) == false));
    DeleteBgpPeer(peer);
    client->WaitForIdle();

    EXPECT_TRUE(FlowTableWait(0));
}
// test case to check that binding to reversed ports
// is not allowed.
TEST_F(FlowTest, LinkLocalFlow_reserved_port) {
    int fd;
    struct sockaddr_in address;
    int status;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(MPLS_OVER_UDP_OLD_DEST_PORT);
    status = ::bind(fd, (struct sockaddr*) &address, sizeof(address));
    EXPECT_EQ(-1, status);
    EXPECT_EQ(EADDRINUSE, errno);
    close(fd);

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    address.sin_port = htons(MPLS_OVER_UDP_NEW_DEST_PORT);
    status = ::bind(fd, (struct sockaddr*) &address, sizeof(address));
    EXPECT_EQ(-1, status);
    EXPECT_EQ(EADDRINUSE, errno);
    close(fd);

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    address.sin_port = htons(VXLAN_UDP_DEST_PORT);
    status = ::bind(fd, (struct sockaddr*) &address, sizeof(address));
    EXPECT_EQ(-1, status);
    EXPECT_EQ(EADDRINUSE, errno);
    close(fd);

}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
