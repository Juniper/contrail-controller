/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <algorithm>
#include <net/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "pkt/flow_table.h"

#include "test_flow_base.cc"

TEST_F(FlowTest, FlowAge_1) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = flow_stats_collector_->flow_age_time_intvl();
    //Set the flow age time to 100 microsecond
    flow_stats_collector_->UpdateFlowAgeTime(tmp_age_time);

    //Create bidirectional flow
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        },
        {
            TestFlowPkt(Address::INET, vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                    flow1->id(), 2),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    client->WaitForIdle();

    // Sleep for age-time
    usleep(tmp_age_time + 10);

    //Trigger flow-aging and make sure they are not removed because 
    //of difference in stats between oper flow and Kernel flow
    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Update reverse-flow stats to postpone aging of forward-flow
    KSyncSockTypeMap::IncrFlowStats(2, 1, 30);

    //Trigger flow-aging
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    //Verify that forward-flow is not removed even though it is eligible to be removed
    //because reverse-flow is not aged
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    // Sleep for age-time
    usleep(tmp_age_time + 10);

    //Trigger to Age the flow
    client->EnqueueFlowAge();
    client->WaitForIdle();

    //Verify that both flows get removed after reverse-flow ages
    WAIT_FOR(100, 1, (0U == get_flow_proto()->FlowCount()));

    //Restore flow aging time
    flow_stats_collector_->UpdateFlowAgeTime(bkp_age_time);
}

// Aging with more than 2 entries
TEST_F(FlowTest, FlowAge_3) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = flow_stats_collector_->flow_age_time_intvl();
    //Set the flow age time to 100 microsecond
    flow_stats_collector_->UpdateFlowAgeTime(tmp_age_time);

    //Create bidirectional flow
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            { }
        },
        {
            TestFlowPkt(Address::INET, vm1_ip, vm3_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 2),
            { }
        },
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 3),
            { }
        },
        {
            TestFlowPkt(Address::INET, vm2_ip, vm3_ip, 1, 0, 0, "vrf5",
                    flow1->id(), 4),
            { }
        },
    };

    CreateFlow(flow, 4);
    EXPECT_EQ(8U, get_flow_proto()->FlowCount());

    // Flow entries are created with #pkts = 1. 
    // Do first sleep for aging to work correctly below
    client->EnqueueFlowAge();
    client->WaitForIdle();

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (0U == get_flow_proto()->FlowCount()));
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());

    // Delete of 2 linked flows
    CreateFlow(flow, 2);
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (0U == get_flow_proto()->FlowCount()));
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());

    // Delete 2 out of 4 linked entries
    CreateFlow(flow, 2);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (2U == get_flow_proto()->FlowCount()));
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());
    EXPECT_TRUE(FlowGet(1, vm1_ip, vm2_ip, 1, 0, 0, false, -1, -1,
                        GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowGet(1, vm2_ip, vm1_ip, 1, 0, 0, false, -1, -1,
                        GetFlowKeyNH(input[1].intf_id)));

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (0U == get_flow_proto()->FlowCount()));
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());

    //Restore flow aging time
    flow_stats_collector_->UpdateFlowAgeTime(bkp_age_time);
}

TEST_F(FlowTest, DISABLED_ScaleFlowAge_1) {
    int tmp_age_time = 200 * 1000;
    int bkp_age_time = flow_stats_collector_->flow_age_time_intvl();
    int total_flows = 200;

    for (int i = 0; i < total_flows; i++) {
        Ip4Address dip(0x1010101 + i);
        //Add route for all of them
        CreateRemoteRoute("vrf5", dip.to_string().c_str(), remote_router_ip, 
                10, "vn5");
        TestFlow flow[]=  {
            {
                TestFlowPkt(Address::INET, vm1_ip, dip.to_string(), 1, 0, 0, "vrf5", 
                        flow0->id(), i),
                { }
            },
            {
                TestFlowPkt(Address::INET, dip.to_string(), vm1_ip, 1, 0, 0, "vrf5",
                        flow0->id(), i + 100),
                { }
            }
        };
        CreateFlow(flow, 2);
    }
    EXPECT_EQ((total_flows * 2), 
            get_flow_proto()->FlowCount());
    //Set the flow age time to 200 milliseconds
    flow_stats_collector_->UpdateFlowAgeTime(tmp_age_time);

    flow_stats_collector_->run_counter_ = 0;

    int passes = GetFlowPassCount((total_flows * 2), tmp_age_time);
    client->EnqueueFlowAge();
    client->WaitForIdle(5);
    WAIT_FOR(5000, 1000, (flow_stats_collector_->run_counter_ >= passes));
    usleep(tmp_age_time + 1000);
        WAIT_FOR(5000, 1000, (flow_stats_collector_->run_counter_ >= (passes * 2)));
        client->WaitForIdle(2);

    WAIT_FOR(5000, 500, (0U == get_flow_proto()->FlowCount()));
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());

    //Restore flow aging time
    flow_stats_collector_->UpdateFlowAgeTime(bkp_age_time);
}

TEST_F(FlowTest, Flow_introspect_delete_all) {
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());

    CreateRemoteRoute("vrf5", remote_vm1_ip, remote_router_ip, 30, "vn5");
    client->WaitForIdle();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {}
        }
    };

    CreateFlow(flow, 1);

    uint32_t vrf_id = VrfGet("vrf5")->vrf_id();
    FlowEntry *fe = FlowGet(vrf_id, vm1_ip, remote_vm1_ip, 1, 0, 0,
                            GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe != NULL);

    DeleteAllFlowRecords *delete_all_sandesh = new DeleteAllFlowRecords();
    Sandesh::set_response_callback(boost::bind(&FlowTest::CheckSandeshResponse,
                                               this, _1, 0));
    delete_all_sandesh->HandleRequest();
    EXPECT_TRUE(FlowTableWait(0));
    delete_all_sandesh->Release();

    DeleteRemoteRoute("vrf5", remote_vm1_ip);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = 
        TestInit(init_file, ksync_init, true, true, true, (1000000 * 60 * 10), (3 * 60 * 1000));
    if (vm.count("config")) {
        eth_itf = Agent::GetInstance()->fabric_interface_name();
    } else {
        eth_itf = "eth0";
        PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                                eth_itf,
                                Agent::GetInstance()->fabric_vrf_name(),
                                PhysicalInterface::FABRIC,
                                PhysicalInterface::ETHERNET, false, nil_uuid(),
                                Ip4Address(0), Interface::TRANSPORT_ETHERNET);
        client->WaitForIdle();
    }

    FlowTest::TestSetup(ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
