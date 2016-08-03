/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include <algorithm>

#define MAX_VNET 4

void RouterIdDepInit(Agent *agent) {
}

#define vm1_ip "11.1.1.1"
#define vm2_ip "11.1.1.2"
#define vm3_ip "11.1.1.3"
#define remote_vm1_ip "12.1.1.3"
#define vn5_unused_ip "11.1.1.10"

int fd_table[MAX_VNET];

struct PortInfo input[] = {
        {"flow0", 6, vm1_ip, "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, vm2_ip, "00:00:00:01:01:02", 5, 2},
        {"flow2", 8, vm3_ip, "00:00:00:01:01:03", 5, 3},
};

int hash_id;
VmInterface *flow0;
VmInterface *flow1;
VmInterface *flow2;

static void NHNotify(DBTablePartBase *partition, DBEntryBase *entry) {
}

class FlowRpfTest : public ::testing::Test {
public:
    FlowRpfTest() : peer_(NULL), agent_(Agent::GetInstance()) {
        flow_proto_ = agent_->pkt()->get_flow_proto();
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
        unsigned int vn_count = 0;
        EXPECT_EQ(0U, get_flow_proto()->FlowCount());
        hash_id = 1;
        client->Reset();
        CreateVmportEnv(input, 3, 1);
        client->WaitForIdle(5);
        vn_count++;

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortActive(input, 2));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 2));
        EXPECT_EQ(6U, agent()->interface_table()->Size());
        EXPECT_EQ(3U, agent()->vm_table()->Size());
        EXPECT_EQ(vn_count, agent()->vn_table()->Size());
        EXPECT_EQ(3U, agent()->interface_config_table()->Size());

        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmInterfaceGet(input[1].intf_id);
        assert(flow1);
        flow2 = VmInterfaceGet(input[2].intf_id);
        assert(flow2);

        client->WaitForIdle();
        peer_ = CreateBgpPeer(Ip4Address(1), "BGP Peer 1");
        Ip4Address gw_ip = Ip4Address::from_string("11.1.1.254");
        //Add a gateway route pointing to pkt0
        VrfEntry *vrf = VrfGet("vrf5");
        static_cast<InetUnicastAgentRouteTable *>(
                vrf->GetInet4UnicastRouteTable())->AddHostRoute("vrf5",
                gw_ip, 32, "vn5", false);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();
        Ip4Address gw_ip = Ip4Address::from_string("11.1.1.254");
        Agent::GetInstance()->fabric_inet4_unicast_table()->DeleteReq(
            Agent::GetInstance()->local_peer(), "vrf5", gw_ip, 32, NULL);
        client->WaitForIdle();
        DeleteVmportEnv(input, 3, true, 1);
        client->WaitForIdle(3);
        client->PortDelNotifyWait(3);
        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));
        EXPECT_FALSE(VmPortFind(input, 2));
        EXPECT_EQ(3U, agent()->interface_table()->Size());

        EXPECT_EQ(0U, agent()->vm_table()->Size());
        EXPECT_EQ(0U, agent()->vn_table()->Size());
        EXPECT_EQ(0U, agent()->acl_table()->Size());
        DeleteBgpPeer(peer_);
    }

    Agent *agent() {return agent_;}
    FlowProto *get_flow_proto() const { return flow_proto_; }

private:
    static bool ksync_init_;
    BgpPeer *peer_;
    Agent *agent_;
    FlowProto *flow_proto_;
};

bool FlowRpfTest::ksync_init_;

TEST_F(FlowRpfTest, Flow_rpf_failure_missing_route) {
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());

    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, remote_vm1_ip, vm2_ip, 1, 0, 0, "vrf5", 
                    flow0->id()),
            {}
        }
    };

    CreateFlow(flow, 1);
    client->WaitForIdle();

    uint32_t vrf_id = VrfGet("vrf5")->vrf_id();
    FlowEntry *fe = FlowGet(vrf_id, remote_vm1_ip, vm2_ip, 1, 0, 0,
                            flow0->flow_key_nh()->id());

    EXPECT_TRUE(fe != NULL);
    if (fe != NULL) {
        WAIT_FOR(1000, 500, (fe->is_flags_set(FlowEntry::ShortFlow) == true));
        EXPECT_TRUE(fe->short_flow_reason() == FlowEntry::SHORT_NO_SRC_ROUTE);
        uint32_t fe_action = fe->match_p().action_info.action;
        EXPECT_TRUE(((fe_action) & (1 << TrafficAction::DENY)) != 0);
    }
    client->WaitForIdle();

    DeleteFlow(flow, 1);
    client->WaitForIdle();

    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowRpfTest, Flow_rpf_failure_subnet_discard_route) {
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());

    IpamInfo ipam_info[] = {
        {"11.1.1.0", 24, "11.1.1.200", true},
    };
    AddIPAM("vn5", ipam_info, 1);
    client->WaitForIdle();

    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vn5_unused_ip, vm2_ip, 1, 0, 0, "vrf5", 
                    flow0->id()),
            {}
        }
    };

    CreateFlow(flow, 1);
    client->WaitForIdle();

    uint32_t vrf_id = VrfGet("vrf5")->vrf_id();
    FlowEntry *fe = FlowGet(vrf_id, vn5_unused_ip, vm2_ip, 1, 0, 0,
                           flow0->flow_key_nh()->id());

    EXPECT_TRUE(fe != NULL);
    if (fe != NULL) {
        WAIT_FOR(1000, 500, (fe->is_flags_set(FlowEntry::ShortFlow) == true));
        EXPECT_TRUE(fe->short_flow_reason() == FlowEntry::SHORT_NO_SRC_ROUTE);
        uint32_t fe_action = fe->match_p().action_info.action;
        EXPECT_TRUE(((fe_action) & (1 << TrafficAction::DENY)) != 0);
    }
    client->WaitForIdle();

    DeleteFlow(flow, 1);
    DelIPAM("vn5");
    client->WaitForIdle();

    EXPECT_TRUE(FlowTableWait(0));
}


// Packets originates from flow0 interface with ip address of flow2 interface.
// this should result in programming vrouter to perform rpf check with flow2
// interface so all the packets coming from flow0 interface will be dropped in
// vrouter, with error invalid source.
TEST_F(FlowRpfTest, Flow_rpf_failure_invalid_source) {
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());

    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm3_ip, vm2_ip, 1, 0, 0, "vrf5", 
                    flow0->id()),
            {}
        }
    };

    CreateFlow(flow, 1);
    client->WaitForIdle();

    uint32_t vrf_id = VrfGet("vrf5")->vrf_id();
    FlowEntry *fe = FlowGet(vrf_id, vm3_ip, vm2_ip, 1, 0, 0,
                            flow0->flow_key_nh()->id());

    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->data().enable_rpf == true);
    if (fe != NULL) {
        WAIT_FOR(1000, 500, (fe->is_flags_set(FlowEntry::ShortFlow) != true));
        const NextHop *nh = fe->nh();
        EXPECT_TRUE(nh->GetType() == NextHop::INTERFACE);
        if (nh->GetType() == NextHop::INTERFACE) {
            const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
            const Interface *intf = intf_nh->GetInterface();
            EXPECT_TRUE(intf->name() == "flow2");
        }
    }
    client->WaitForIdle();

    DeleteFlow(flow, 1);
    client->WaitForIdle();

    EXPECT_TRUE(FlowTableWait(0));
}

//Disable RPF on a VN and verify that
//rpf check is disabled
TEST_F(FlowRpfTest, FlowDisableRpf) {
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DisableRpf("vn5", 5);

    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {new VerifyRpfEnable(false, false)}
        }
    };

    CreateFlow(flow, 1);
    client->WaitForIdle();

    DeleteFlow(flow, 1);
    client->WaitForIdle();

    EnableRpf("vn5", 5);
    EXPECT_TRUE(FlowTableWait(0));
}

//Setup flow with RPF enabled
//Change the rpf enable field in VN
//Verify that RPF is disabled on flow
TEST_F(FlowRpfTest, FlowEnableDisableRpf) {
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    EnableRpf("vn5", 5);
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {new VerifyRpfEnable(true, true)}
        }
    };

    CreateFlow(flow, 1);
    client->WaitForIdle();

    //Disable RPF
    DisableRpf("vn5", 5);
    uint32_t vrf_id = VrfGet("vrf5")->vrf_id();
    FlowEntry *fe = FlowGet(vrf_id, vm1_ip, vm2_ip, 1, 0, 0,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe->data().enable_rpf == false);

    FlowEntry *rfe = FlowGet(vrf_id, vm2_ip, vm1_ip, 1, 0, 0,
                             flow1->flow_key_nh()->id());
    EXPECT_TRUE(rfe->data().enable_rpf == false);

    DeleteFlow(flow, 1);
    client->WaitForIdle();

    EnableRpf("vn5", 5);
    EXPECT_TRUE(FlowTableWait(0));
}


//Add floating IP for a flow0 interface and disable
//rpf on the VN from where floating IP is enabled
//Verify rpf check is disabled
TEST_F(FlowRpfTest, FipDisableRpf) {
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());

    AddVn("default-project:vn2", 2);
    AddVrf("default-project:vn2:vn2", 2);
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    //Add floating IP for vnet1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
    client->WaitForIdle();
    Ip4Address floating_ip = Ip4Address::from_string("2.1.1.100");
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2", floating_ip, 32));
    EXPECT_TRUE(VmPortFloatingIpCount(6, 1));
    DisableRpf("default-project:vn2", 2);

    Ip4Address addr = Ip4Address::from_string("0.0.0.0");
    Ip4Address gw = Ip4Address::from_string("10.1.1.2");

    Inet4TunnelRouteAdd(NULL, "default-project:vn2:vn2", addr, 0, gw,
            TunnelType::AllType(), 8, "default-project:vn2",
            SecurityGroupList(), PathPreference());
    client->WaitForIdle();

    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vn5_unused_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {new VerifyRpfEnable(false, false),
             new VerifyNat(vn5_unused_ip, "2.1.1.100", 1, 0, 0),
            }
        }
    };

    CreateFlow(flow, 1);
    client->WaitForIdle();

    DeleteFlow(flow, 1);
    client->WaitForIdle();

    DeleteRoute("default-project:vn2:vn2", "0.0.0.0", 0);
    client->WaitForIdle();
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1",
            "virtual-network", "default-project:vn2");
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelVrf("default-project:vn2:vn2");
    DelVn("default-project:vn2");
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = 
        TestInit(init_file, ksync_init, true, true, true, (1000000 * 60 * 10), (3 * 60 * 1000));

    FlowRpfTest::TestSetup(ksync_init);
    int ret = RUN_ALL_TESTS();
    usleep(1000);
    Agent::GetInstance()->event_manager()->Shutdown();
    AsioStop();
    TaskScheduler::GetInstance()->Terminate();
    return ret;
}
