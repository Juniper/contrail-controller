/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include "base/os.h"
#include <array>
#include "test/test_init.h"
#include "test/test_cmn_util.h"
#include "oper/mirror_table.h"
#include "oper/tunnel_nh.h"
#include "xmpp/test/xmpp_test_util.h"
#include "pkt/test/test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include <vrouter/flow_stats/flow_stats_types.h>
#include <vrouter/flow_stats/session_stats_collector.h>
#include "uve/test/test_uve_util.h"

#define remote_vm1_ip "1.1.1.3"
#define remote_vm2_ip "1.1.1.4"
#define remote_router_ip "10.1.1.2"

struct PortInfo input[] = {
        {"flow0", 6, "1.1.1.1", "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, "1.1.1.2", "00:00:00:01:01:02", 5, 2},
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

VmInterface *flow0;
VmInterface *flow1;

class SessionHandleTask : public Task {
public:
    SessionHandleTask() :
        Task((TaskScheduler::GetInstance()->
              GetTaskId(kTaskSessionStatsCollector)), 0) {
    }
    virtual bool Run() {
        SessionStatsCollectorObject *obj = Agent::GetInstance()->
            flow_stats_manager()->session_stats_collector_obj();
        for (int i = 0; i < SessionStatsCollectorObject::kMaxSessionCollectors;
             i++) {
            obj->GetCollector(i)->Run();
        }
        return true;
    }
    std::string Description() const { return "SessionHandleTask"; }
};

class SessionStatsTest : public ::testing::Test {
public:
    SessionStatsTest() : agent_(Agent::GetInstance()), util_(), peer_(NULL) {
        flow_proto_ = agent_->pkt()->get_flow_proto();
    }
    void CreateRemoteRoute(const char *vrf, const char *remote_vm, uint8_t plen,
                           const char *serv, int label, const char *vn) {
        Ip4Address addr = Ip4Address::from_string(remote_vm);
        Ip4Address gw = Ip4Address::from_string(serv);
        Inet4TunnelRouteAdd(peer_, vrf, addr, plen, gw, TunnelType::MplsType(),
                            label, vn, SecurityGroupList(),
                            TagList(), PathPreference());
        client->WaitForIdle(2);
        WAIT_FOR(1000, 500, (RouteFind(vrf, addr, plen) == true));
    }
    void CreateRemoteRoute(const char *vrf, const char *remote_vm,
                           const char *serv, int label, const char *vn) {
        CreateRemoteRoute(vrf, remote_vm, 32, serv, label, vn);
    }
    void EnqueueSessionTask() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        SessionHandleTask *task = new SessionHandleTask();
        scheduler->Enqueue(task);
    }

    void FlowSetup() {
        unsigned int vn_count = 0;
        client->Reset();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(10);
        AddIPAM("vn5", ipam_info, 1);
        client->WaitForIdle();
        vn_count++;

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));
        EXPECT_EQ(5U, agent_->interface_table()->Size());
        EXPECT_EQ(2U, agent_->vm_table()->Size());
        EXPECT_EQ(vn_count, agent_->vn_table()->Size());
        EXPECT_EQ(2U, PortSubscribeSize(agent_));

        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmInterfaceGet(input[1].intf_id);
        assert(flow1);

        /* verify that there are no existing Flows */
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        boost::system::error_code ec;
        peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                              "xmpp channel");
    }
    void FlowTeardown() {
        client->Reset();
        DeleteVmportEnv(input, 2, 1, 1);
        client->WaitForIdle(10);
        DelIPAM("vn5");
        client->WaitForIdle();
        client->VnDelNotifyWait(1);
        client->PortDelNotifyWait(2);
        EXPECT_TRUE(client->AclNotifyWait(1));
        DeleteBgpPeer(peer_);
    }

    Agent *agent_;
    FlowProto *flow_proto_;
    TestUveUtil util_;
private:
    BgpPeer *peer_;
};

TEST_F(SessionStatsTest, FlowAddVerify) {
    SessionStatsCollectorObject *ssc_obj = agent_->flow_stats_manager()->
                                           session_stats_collector_obj();
    FlowSetup();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 6, 1000, 2000, "vrf5",
                        flow0->id()),
            {
            }
        },
        {
            TestFlowPkt(Address::INET, "1.1.1.2", "1.1.1.1", 6, 4000, 5000, "vrf5",
                        flow0->id()),
            {
            }
        }
    };

    CreateFlow(flow, 2);
    client->WaitForIdle();
    EXPECT_EQ(4U, flow_proto_->FlowCount());

    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_TRUE(fe != NULL);
    SessionStatsCollector *ssc = ssc_obj->FlowToCollector(fe);
    EXPECT_TRUE(ssc != NULL);
    EXPECT_EQ(3U, ssc->Size());

    DeleteFlow(flow, 2);
    client->WaitForIdle();
    FlowTeardown();
    EXPECT_EQ(0U, flow_proto_->FlowCount());
    EnqueueSessionTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (ssc->Size() == 0));
}

TEST_F(SessionStatsTest, RemoteFlowAddVerify) {
    SessionStatsCollectorObject *ssc_obj = agent_->flow_stats_manager()->
                                           session_stats_collector_obj();
    FlowSetup();
    CreateRemoteRoute("vrf5", remote_vm1_ip, remote_router_ip, 30, "vn5");
    CreateRemoteRoute("vrf5", remote_vm2_ip, remote_router_ip, 32, "vn5");
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", remote_vm1_ip, 6, 1000, 2000, "vrf5",
                        flow0->id()),
            {
            }
        },
        {

            TestFlowPkt(Address::INET, remote_vm1_ip, "1.1.1.1", 6, 4000, 5000, "vrf5",
                    remote_router_ip, flow0->label()),
            {
            }
        },
        {
            TestFlowPkt(Address::INET, "1.1.1.1", remote_vm2_ip, 6, 1001, 2000, "vrf5",
                        flow0->id()),
            {
            }
        },
        {

            TestFlowPkt(Address::INET, remote_vm2_ip, "1.1.1.1", 6, 4001, 5000, "vrf5",
                    remote_router_ip, flow0->label()),
            {
            }
        }
    };

    CreateFlow(flow, 4);
    client->WaitForIdle();
    EXPECT_EQ(8U, flow_proto_->FlowCount());

    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_TRUE(fe != NULL);
    SessionStatsCollector *ssc = ssc_obj->FlowToCollector(fe);
    EXPECT_TRUE(ssc != NULL);
    EXPECT_EQ(2U, ssc->Size());

    DeleteFlow(flow, 4);
    client->WaitForIdle();
    FlowTeardown();
    EXPECT_EQ(0U, flow_proto_->FlowCount());
    EnqueueSessionTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (ssc->Size() == 0));
}

int main(int argc, char *argv[]) {
    int ret;
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, false,
                      true, (10 * 60 * 1000), (10 * 60 * 1000),
                      true, true, (10 * 60 * 1000));
    ::testing::InitGoogleTest(&argc, argv);
    usleep(10000);
    ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
