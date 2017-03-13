/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_init.h"
#include "test/test_cmn_util.h"
#include "kstate/test/test_kstate.h"
#include "oper/mirror_table.h"
#include "oper/tunnel_nh.h"
#include "xmpp/test/xmpp_test_util.h"
#include "ksync/ksync_sock_user.h"

#define vm1_ip "1.1.1.1"
#define vm2_ip "2.1.1.1"
#define vm3_ip "3.1.1.1"
#define vm4_ip "4.1.1.1"
#define vm5_ip "5.1.1.1"
#define SIP  0x01010101
#define DIP  0x01010102
#define SPORT 20130
#define DPORT 20131


#define MAX_VNET 1
int fd_table[MAX_VNET];
#define MAX_TEST_FD 3
int test_fd[MAX_TEST_FD];

TestIfKState *TestIfKState::singleton_;
TestNHKState *TestNHKState::singleton_;
TestMplsKState *TestMplsKState::singleton_;
TestMirrorKState *TestMirrorKState::singleton_;
TestRouteKState *TestRouteKState::singleton_;
TestFlowKState *TestFlowKState::singleton_;
int TestKStateBase::handler_count_;
int TestKStateBase::fetched_count_;

struct PortInfo input[] = {
    {"test0", 5, vm1_ip, "00:00:00:01:01:01", 3, 1},
    {"test1", 6, vm2_ip, "00:00:00:02:02:02", 3, 2},
    {"test2", 7, vm3_ip, "00:00:00:01:01:03", 3, 3},
    {"test3", 8, vm4_ip, "00:00:00:02:02:04", 3, 4},
    {"test4", 9, vm5_ip, "00:00:00:02:02:05", 3, 5},
};
IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
    {"2.1.1.0", 24, "2.1.1.10"},
    {"3.1.1.0", 24, "3.1.1.10"},
    {"4.1.1.0", 24, "4.1.1.10"},
    {"5.1.1.0", 24, "5.1.1.10"},
};

std::string analyzer = "TestAnalyzer";

void RouterIdDepInit(Agent *agent) {
}

class KStateTest : public ::testing::Test {
public:

    static void TestSetup(bool ksync_init) {
        ksync_init_ = ksync_init;
        //Physical interface name specified in default config file is vnet0.
        //Create this if it is in ksync_init mode
        if (ksync_init_) {
            CreateTapInterfaces("vnet", MAX_VNET, fd_table);
            client->WaitForIdle(2);

            CreateTapInterfaces("test", MAX_TEST_FD, test_fd);
            client->WaitForIdle(2);
        }
        //To disable flow aging set the flow age time to high value
        Agent::GetInstance()->flow_stats_manager()->
            default_flow_stats_collector_obj()->
            SetFlowAgeTime(1000000 * 60 * 10);

        VxLanNetworkIdentifierMode(false);
        client->WaitForIdle();
    }

    static void TestTearDown() {
        DelNode("global-vrouter-config", "vrouter-config");
        client->WaitForIdle();
        if (ksync_init_) {
            DeleteTapIntf(fd_table, MAX_VNET);
            client->WaitForIdle(2);
            DeleteTapIntf(test_fd, MAX_TEST_FD);
            client->WaitForIdle(2);
        }
    }

    void WaitForVrf(struct PortInfo *input, int idx, bool created) {
        char vrf_name[80];
        sprintf(vrf_name, "vrf%d", input[idx].vn_id);
        WAIT_FOR(1000, 1000, (VrfFind(vrf_name) == created));
    }

    void CreateVmPorts(struct PortInfo *input, int count) {
        CreateVmportEnv(input, count);
    }
    void CreatePorts(uint32_t if_count, uint32_t nh_count, uint32_t rt_count,
                     uint32_t num_ports = MAX_TEST_FD) {
        int idx;
        client->Reset();
        CreateVmPorts(input, num_ports);
        client->WaitForIdle(10);

        for (uint32_t i = 0; i < num_ports; i++) {
            idx = i;
            WAIT_FOR(1000, 1000, (VmPortActive(input, idx) == true));
        }
        WAIT_FOR(1000, 1000, ((unsigned int)num_ports == Agent::GetInstance()->vm_table()->Size()));
        WAIT_FOR(1000, 1000, (1 == Agent::GetInstance()->vn_table()->Size()));
        WaitForVrf(input, 0, true);
        if (if_count) {
	    unsigned int oper_if_count = num_ports + if_count;
            WAIT_FOR(1000, 1000, ((oper_if_count) ==
                                Agent::GetInstance()->interface_table()->Size()));
        }
        //One EVPN label for VN, One for Vrf, 4 per VM port
        WAIT_FOR(1000, 1000, (((num_ports * 4) + 2 ==
                            (Agent::GetInstance()->mpls_table()->Size()))));
        if (!ksync_init_) {
            //One EVPN label for VN, One for Vrf, 4 per VM port
            WAIT_FOR(1000, 1000, ((num_ports * 4) + 2 ==
                                  (uint32_t)(KSyncSockTypeMap::MplsCount())));
            if (if_count) {
                WAIT_FOR(1000, 1000, ((num_ports + if_count) ==
                                    (uint32_t)KSyncSockTypeMap::IfCount()));
            }
            if (nh_count) {
                //4 interface nexthops get created for each interface
                //(l2 with policy, l2 without policy, l3 with policy, l3
                // without policy)
                //plus 4 Nexthops for each VRF (1 VRF NH and 3 Composite NHs
                //i.e. TOR CNH, EVPN CNH, Fabric CNH)
                WAIT_FOR(1000, 1000, ((nh_count + (num_ports * 4) + 4) ==
                                    (uint32_t)KSyncSockTypeMap::NHCount()));
            }
            if (rt_count) {
                WAIT_FOR(1000, 1000, ((rt_count + (num_ports * 2) + 1) ==
                                    (uint32_t)KSyncSockTypeMap::RouteCount()));
            }
        }
    }

    void DeletePorts(int num_ports = MAX_TEST_FD) {
        DeleteVmportEnv(input, num_ports, true);
        client->WaitForIdle(10);
        WAIT_FOR(1000, 1000, (0 == Agent::GetInstance()->vm_table()->Size()));
        WAIT_FOR(1000, 1000, (0 == Agent::GetInstance()->vn_table()->Size()));
        WaitForVrf(input, 0, false);
        WAIT_FOR(1000, 1000, (0 == Agent::GetInstance()->mpls_table()->Size()));
    }

    void CreatePortsWithPolicy() {
        int idx;
        client->Reset();
        CreateVmportEnv(input, MAX_TEST_FD, 1);
        client->WaitForIdle(10);

        for (int i = 0; i < MAX_TEST_FD; i++) {
            idx = i;
            WAIT_FOR(1000, 1000, (VmPortActive(input, idx) == true));
            WAIT_FOR(1000, 1000, (VmPortPolicyEnable(input, idx) == true));
        }
        WAIT_FOR(1000, 1000, (MAX_TEST_FD == Agent::GetInstance()->vm_table()->Size()));
        WAIT_FOR(1000, 1000, (1 == Agent::GetInstance()->vn_table()->Size()));
        WaitForVrf(input, 0, true);
        WAIT_FOR(1000, 1000, ((MAX_TEST_FD + 3) == Agent::GetInstance()->interface_table()->Size()));
        if (!ksync_init_) {
            WAIT_FOR(1000, 1000, ((MAX_TEST_FD + 3) == KSyncSockTypeMap::IfCount()));
        }
    }

    void DeletePortsWithPolicy() {
        int idx;
        client->Reset();
        DeleteVmportEnv(input, MAX_TEST_FD, true, 1);
        client->WaitForIdle(5);
        client->PortDelNotifyWait(MAX_TEST_FD);
        for (int i = 0; i < MAX_TEST_FD; i++) {
            idx = i;
            WAIT_FOR(1000, 1000, (VmPortFind(input, idx) == false));
        }
        WAIT_FOR(1000, 1000, (0 == Agent::GetInstance()->vm_table()->Size()));
        WAIT_FOR(1000, 1000, (0 == Agent::GetInstance()->vn_table()->Size()));
        WaitForVrf(input, 0, false);
        WAIT_FOR(1000, 1000, (3 == Agent::GetInstance()->interface_table()->Size()));
    }

    void CreateMirrorEntry() {
        Ip4Address sip(SIP);
        Ip4Address dip(DIP);

        //Create Mirror entry
        MirrorTable::AddMirrorEntry(analyzer, Agent::GetInstance()->fabric_vrf_name(),
                                    sip, SPORT, dip, DPORT);
        client->WaitForIdle(2);

        //Verify mirror NH is created
        MirrorNHKey key(Agent::GetInstance()->fabric_vrf_name(), sip, SPORT, dip, DPORT);
        WAIT_FOR(1000, 1000, (Agent::GetInstance()->nexthop_table()->FindActiveEntry(&key) != NULL));
    }

    void DeleteMirrorEntry() {
        Ip4Address sip(SIP);
        Ip4Address dip(DIP);

        MirrorTable::DelMirrorEntry(analyzer);
        client->WaitForIdle(2);

        //Verify mirror NH is deleted
        MirrorNHKey key(Agent::GetInstance()->fabric_vrf_name(), sip, SPORT, dip, DPORT);
        WAIT_FOR(1000, 1000, (Agent::GetInstance()->nexthop_table()->FindActiveEntry(&key) == NULL));
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        AddIPAM("vn3", ipam_info, MAX_TEST_FD);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DelIPAM("vn3");
        client->WaitForIdle();
    }

    Agent *agent_;
    static bool ksync_init_;
};

bool KStateTest::ksync_init_;
TEST_F(KStateTest, IfDumpTest) {
    int if_count = 0;
    TestIfKState::Init();
    client->WaitForIdle();
    client->KStateResponseWait(1);
    if_count = TestKStateBase::fetched_count_;
    LOG(DEBUG, "if count " << if_count);

    CreatePorts(if_count, 0, 0);
    TestIfKState::Init(-1, true, if_count + MAX_TEST_FD);
    client->WaitForIdle();
    client->KStateResponseWait(1);
    DeletePorts();
}

TEST_F(KStateTest, IfGetTest) {
    int if_count = 0;
    TestIfKState::Init();
    client->WaitForIdle();
    client->KStateResponseWait(1);
    if_count = TestKStateBase::fetched_count_;
    LOG(DEBUG, "if count " << if_count);

    CreatePorts(if_count, 0, 0);
    for (int i = 0; i < MAX_TEST_FD; i++) {
        TestIfKState::Init(if_count + i);
        client->WaitForIdle();
        client->KStateResponseWait(1);
    }
    DeletePorts();
}
/* TODO: enable the below UT. We need to handle multiple responses coming from
 * vrouter when we cannot accomodate all the nexthops in single response */
TEST_F(KStateTest, DISABLED_NHDumpTest) {
    int nh_count = 0;
    TestNHKState::Init();
    client->WaitForIdle();
    client->KStateResponseWait(1);
    nh_count = TestKStateBase::fetched_count_;
    LOG(DEBUG, "nh count " << nh_count);
    int max_ports = 2;

    CreatePorts(0, nh_count, 0, max_ports);
    //4 interface nexthops get created for each interface
    //(l2 with policy, l2 without policy, l3 with policy, l3 without policy)
    //plus 4 Nexthops for each VRF (1 VRF NH and 3 Composite NHs)
    TestNHKState::Init(-1, true, nh_count + (max_ports * 4) + 4);
    client->WaitForIdle();
    client->KStateResponseWait(1);

    DeletePorts(max_ports);
}

TEST_F(KStateTest, NHGetTest) {
    int nh_count = 0;
    TestNHKState::Init();
    client->WaitForIdle();
    client->KStateResponseWait(1);
    nh_count = TestKStateBase::fetched_count_;
    LOG(DEBUG, "nh count " << nh_count);

    CreatePorts(0, nh_count, 0);
    int count = 0;
    //Two interface nexthops get created for each interface (with and without policy)
    for (int i = 0; count < (MAX_TEST_FD * 2); i++) {
        uint32_t nh_id = nh_count + i;
        /* Check whether nexthop-index is valid by checking it in oper db before doing
         * Get from mock kernel */
        NextHop *nh = Agent::GetInstance()->nexthop_table()->FindNextHop(nh_id);
        if (nh) {
            TestNHKState::Init(nh_id);
            client->WaitForIdle();
            client->KStateResponseWait(1);
            count++;
        }
    }
    DeletePorts();
}

TEST_F(KStateTest, MplsDumpTest) {
    int mpls_count = 0;
    TestMplsKState::Init();
    client->WaitForIdle();
    client->KStateResponseWait(1);
    mpls_count = TestKStateBase::fetched_count_;

    CreatePorts(0, 0, 0);
    TestMplsKState::Init(-1, true, mpls_count + (4 * MAX_TEST_FD) + 2);
    client->WaitForIdle(3);
    client->KStateResponseWait(1);

    DeletePorts();
}

TEST_F(KStateTest, MplsGetTest) {
    int mpls_count = 0;
    TestMplsKState::Init();
    client->WaitForIdle();
    client->KStateResponseWait(1);
    mpls_count = TestKStateBase::fetched_count_;

    CreatePorts(0, 0, 0);
    //Total labels = 2*No of VM(1 for L2 and 1 for L3) +
    //               1(Multicast EVPN label)
    for (int i = 0; i <= (2 * MAX_TEST_FD); i++) {
        TestMplsKState::Init(MplsTable::kStartLabel + mpls_count + i);
        client->WaitForIdle();
        client->KStateResponseWait(1);
    }

    DeletePorts();
}

TEST_F(KStateTest, MirrorNHGetTest) {
    unsigned int nh_count = 0;
    TestNHKState::Init();
    client->WaitForIdle();
    client->KStateResponseWait(1);
    nh_count = TestKStateBase::fetched_count_;
    LOG(DEBUG, "nh count " << nh_count);

    //Create mirror entry
    CreateMirrorEntry();

    //Verify that NH table size is increased by 1
    EXPECT_EQ(Agent::GetInstance()->nexthop_table()->Size(), nh_count + 1);

    //Verify the get of Mirror NH
    TestNHKState::Init(nh_count);
    client->WaitForIdle();
    client->KStateResponseWait(1);

    DeleteMirrorEntry();
}

TEST_F(KStateTest, MirrorDumpTest) {
    int mirror_count = 0;
    TestMirrorKState::Init();
    client->WaitForIdle();
    client->KStateResponseWait(1);
    mirror_count = TestKStateBase::fetched_count_;

    CreateMirrorEntry();
    TestMirrorKState::Init(-1, true, mirror_count + 1);
    client->WaitForIdle();
    client->KStateResponseWait(1);
    DeleteMirrorEntry();
}

TEST_F(KStateTest, MirrorGetTest) {
    int mirror_count = 0;
    TestMirrorKState::Init();
    client->WaitForIdle();
    client->KStateResponseWait(1);
    mirror_count = TestKStateBase::fetched_count_;

    CreateMirrorEntry();
    TestMirrorKState::Init(mirror_count);
    client->WaitForIdle();
    client->KStateResponseWait(1);
    DeleteMirrorEntry();
}

TEST_F(KStateTest, RouteDumpTest) {
    if (!ksync_init_) {
        int rt_count = 0, prev_rt_count;
        TestRouteKState::Init(false);
        client->WaitForIdle();
        client->KStateResponseWait(1);
        rt_count = 0;

        prev_rt_count = KSyncSockTypeMap::RouteCount();
        CreatePorts(0, 0, rt_count);
        //Default
        //Addition of 2 vm ports in a new VN (VRF) will result in the following routes
        // 2 routes corresponding to the addresses of VM
        // 2 routes corresponding to VN ipam, i.e., gate-way & subnet routes
        // l2 broadcast
        // 3 - v6 host route for new vrf addition
        TestRouteKState::Init(true, prev_rt_count + (MAX_TEST_FD * 4) + 4);
        client->WaitForIdle();
        client->KStateResponseWait(1);
        DeletePorts();
    }
}

TEST_F(KStateTest, FlowDumpTest_invalid_index) {
    /* send invalid index to flowkstate */
    TestFlowKState::Init(true, -2, 0);
    client->WaitForIdle();
    client->KStateResponseWait(0);
}

TEST_F(KStateTest, DISABLED_FlowDumpTest) {
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->get_flow_proto()->FlowCount());
    TestFlowKState::Init(true, -1, 0);
    client->WaitForIdle();
    client->KStateResponseWait(0);

    CreatePortsWithPolicy();

    VmInterface *test0, *test1;
    test0 = VmInterfaceGet(input[0].intf_id);
    assert(test0);
    test1 = VmInterfaceGet(input[1].intf_id);
    assert(test1);

    int hash_id = 1;
    //Flow creation using IP packet
    TxIpPacketUtil(test0->id(), vm1_ip, vm2_ip, 0, hash_id);
    client->WaitForIdle(2);
    EXPECT_TRUE(FlowGet("vrf3", vm1_ip, vm2_ip, 0, 0, 0, false,
                        "vn3", "vn3", hash_id++,
                        test0->flow_key_nh()->id()));

    //Create flow in reverse direction
    TxIpPacketUtil(test1->id(), vm2_ip, vm1_ip, 0, hash_id);
    client->WaitForIdle(2);
    EXPECT_TRUE(FlowGet("vrf3", vm2_ip, vm1_ip, 0, 0, 0, true,
                        "vn3", "vn3", hash_id++,
                        test1->flow_key_nh()->id(),
                        test0->flow_key_nh()->id()));

    //Flow creation using TCP packet
    TxTcpPacketUtil(test0->id(), vm1_ip, vm2_ip, 1000, 200,
                    hash_id);
    client->WaitForIdle(2);
    EXPECT_TRUE(FlowGet("vrf3", vm1_ip, vm2_ip, 6, 1000, 200, false,
                        "vn3", "vn3", hash_id++,
                        test0->flow_key_nh()->id()));

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(test1->id(), vm2_ip, vm1_ip, 200, 1000,
                    hash_id);
    client->WaitForIdle(2);
    EXPECT_TRUE(FlowGet("vrf3", vm2_ip, vm1_ip, 6, 200, 1000, true,
                        "vn3", "vn3", hash_id++,
                        test1->flow_key_nh()->id(),
                        test0->flow_key_nh()->id()));
    EXPECT_EQ(4U, Agent::GetInstance()->pkt()->get_flow_proto()->FlowCount());

    TestFlowKState::Init(true, -1, 6);
    client->WaitForIdle();
    client->KStateResponseWait(1);

    //cleanup
    client->EnqueueFlowFlush();
    client->WaitForIdle(2);
    WAIT_FOR(1000, 1000, 
             (0 == Agent::GetInstance()->pkt()->get_flow_proto()->FlowCount()));
    DeletePortsWithPolicy();
}

int main(int argc, char *argv[]) {
    int ret;
    GETUSERARGS();

    /* Supported only with non-ksync mode for now */
    ksync_init = false;

    client = TestInit(init_file, ksync_init, true, true);
    KStateTest::TestSetup(ksync_init);

    ::testing::InitGoogleTest(&argc, argv);
    ret = RUN_ALL_TESTS();
    KStateTest::TestTearDown();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
