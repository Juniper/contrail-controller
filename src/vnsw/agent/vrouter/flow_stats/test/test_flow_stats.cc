/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include "base/os.h"
#include <boost/array.hpp>
#include "test/test_init.h"
#include "test/test_cmn_util.h"
#include "oper/mirror_table.h"
#include "oper/tunnel_nh.h"
#include "xmpp/test/xmpp_test_util.h"
#include "pkt/test/test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include <vrouter/flow_stats/flow_stats_types.h>
#include "uve/test/test_uve_util.h"

struct PortInfo input[] = {
        {"flow0", 6, "1.1.1.1", "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, "1.1.1.2", "00:00:00:01:01:02", 5, 2},
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

VmInterface *flow0;
VmInterface *flow1;

void RouterIdDepInit(Agent *agent) {
}

class FlowStatsTest : public ::testing::Test {
public:
    FlowStatsTest() : response_count_(0), type_specific_response_count_(0), 
    num_entries_(0), agent_(Agent::GetInstance()), util_() {
        flow_proto_ = agent_->pkt()->get_flow_proto();
    }
    void FlowParamsResponse(Sandesh *sandesh) {
        response_count_++;
        FlowStatsCollectionParamsResp *resp =
            dynamic_cast<FlowStatsCollectionParamsResp *>(sandesh);
        if (resp != NULL) {
            type_specific_response_count_++;
        }
    }
    void FlowParamsGet() {
        FlowStatsCollectionParamsReq *req = new FlowStatsCollectionParamsReq();
        Sandesh::set_response_callback(
            boost::bind(&FlowStatsTest::FlowParamsResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }
    void FlowEntriesResponse(Sandesh *sandesh) {
        response_count_++;
        FlowStatsRecordsResp *resp =
            dynamic_cast<FlowStatsRecordsResp *>(sandesh);
        if (resp != NULL) {
            type_specific_response_count_++;
            num_entries_ = resp->get_records_list().size();
        }
    }
    void FlowEntriesGet() {
        FlowStatsRecordsReq *req = new FlowStatsRecordsReq();
        Sandesh::set_response_callback(
            boost::bind(&FlowStatsTest::FlowEntriesResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }
    void ClearCount() {
        response_count_ = type_specific_response_count_ = num_entries_ = 0;
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
    }

    uint32_t response_count_;
    uint32_t type_specific_response_count_;
    uint32_t num_entries_;
    Agent *agent_;
    FlowProto *flow_proto_;
    TestUveUtil util_;
};

TEST_F(FlowStatsTest, SandeshFlowParams) {
    ClearCount();
    FlowParamsGet();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
}

TEST_F(FlowStatsTest, SandeshFlowEntries) {
    FlowSetup();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf5",
                        flow0->id()),
            {
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    CreateFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(rfe != NULL);
    
    ClearCount();
    FlowEntriesGet();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(2U, num_entries_);

    DeleteFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(0U, flow_proto_->FlowCount());
    FlowTeardown();
}

//Verify that flow_tree maitained by FlowStatsCollector is inserted/erased on
//flow add/delete 
TEST_F(FlowStatsTest, FlowTreeSize) {
    FlowSetup();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf5",
                        flow0->id()),
            {
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    CreateFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(rfe != NULL);
    FlowStatsCollector *col = fe->fsc();
    EXPECT_TRUE(col != NULL);

    FlowExportInfo *info = col->FindFlowExportInfo(fe);
    FlowExportInfo *rinfo = col->FindFlowExportInfo(rfe);
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);
    EXPECT_EQ(2U, col->Size());
    
    ClearCount();
    FlowEntriesGet();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(2U, num_entries_);

    DeleteFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(0U, flow_proto_->FlowCount());
    WAIT_FOR(1000, 1000, (col->Size() == 0));
    FlowTeardown();
}

//Verify that after Flow Add request is processed by FlowStats Module, the flow
//is present in both the trees of FlowStats Module
TEST_F(FlowStatsTest, FlowAddVerify) {
    FlowStatsCollectorObject *fsc_obj = agent_->flow_stats_manager()->
        default_flow_stats_collector_obj();
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = fsc_obj->GetFlowAgeTime();
    //Set the flow age time to 100 microsecond
    fsc_obj->SetFlowAgeTime(tmp_age_time);
    FlowSetup();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf5",
                        flow0->id()),
            {
            }
        }
    };

    //Create short flow by having unknown destination IP
    CreateFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(rfe != NULL);
    FlowStatsCollector *col = fe->fsc();
    EXPECT_TRUE(col != NULL);

    //Verify that flows are present in flow_tree_ of FlowStatsCollector
    FlowExportInfo *info = col->FindFlowExportInfo(fe);
    FlowExportInfo *rinfo = col->FindFlowExportInfo(rfe);
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);
    EXPECT_EQ(2U, col->Size());

    //Verify that sizes of both flow_tree_ and flow_export_info_list_ are same
    WAIT_FOR(5000, 1000, (col->Size() == col->AgeTreeSize()));

    //Disable flow delete queue
    flow_proto_->DisableFlowDeleteQueue(0, true);

    usleep(tmp_age_time + 10);

    //Enqueue Flow Aging request
    util_.EnqueueFlowStatsCollectorTask();

    WAIT_FOR(5000, 1000, (col->AgeTreeSize() == (col->Size() - 2)));

    //Send requests to create flow again
    CreateFlow(flow, 1);

    //Verify that sizes of both flow_tree_ and flow_export_info_list_ are same
    WAIT_FOR(5000, 1000, (col->Size() == col->AgeTreeSize()));

    //cleanup
    flow_proto_->DisableFlowDeleteQueue(0, false);

    fsc_obj->SetFlowAgeTime(bkp_age_time);
    DeleteFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(0U, flow_proto_->FlowCount());
    WAIT_FOR(1000, 1000, (col->Size() == 0));
    FlowTeardown();
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
