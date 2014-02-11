/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "test/test_init.h"
#include "test/test_cmn_util.h"
#include "oper/mirror_table.h"
#include "oper/tunnel_nh.h"
#include "xmpp/test/xmpp_test_util.h"
#include "ksync/ksync_sock_user.h"

void RouterIdDepInit() {
}

class KStateSandeshTest : public ::testing::Test {
public:
    KStateSandeshTest() : response_count_(0), type_specific_response_count_(0), 
    num_entries_(0) {
    }
    void VxlanGet(int id) {
        KVxLanReq *req = new KVxLanReq();
        req->set_vxlan_label(id);
        Sandesh::set_response_callback(
            boost::bind(&KStateSandeshTest::VxlanResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }
    void VxlanResponse(Sandesh *sandesh) {
        response_count_++;
        KVxLanResp *response = dynamic_cast<KVxLanResp *>(sandesh);
        if (response != NULL) {
            type_specific_response_count_++;
            num_entries_ += response->get_vxlan_list().size();
        }
    }

    void VrfAssignGet(int id) {
        KVrfAssignReq *req = new KVrfAssignReq();
        req->set_vif_index(id);
        Sandesh::set_response_callback(
            boost::bind(&KStateSandeshTest::VrfAssignResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void VrfAssignResponse(Sandesh *sandesh) {
        response_count_++;
        KVrfAssignResp *response = dynamic_cast<KVrfAssignResp *>(sandesh);
        if (response != NULL) {
            type_specific_response_count_++;
            num_entries_ += response->get_vrf_assign_list().size();
        }
    }
    void VrfStatsGet(int vrf_id) {
        KVrfStatsReq *req = new KVrfStatsReq();
        req->set_vrf_index(vrf_id);
        Sandesh::set_response_callback(
            boost::bind(&KStateSandeshTest::VrfStatsResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }
    void VrfStatsResponse(Sandesh *sandesh) {
        response_count_++;
        KVrfStatsResp *response = dynamic_cast<KVrfStatsResp *>(sandesh);
        if (response != NULL) {
            type_specific_response_count_++;
            num_entries_ += response->get_vrf_stats_list().size();
        }
    }
    void DropStatsGet() {
        KDropStatsReq *req = new KDropStatsReq();
        Sandesh::set_response_callback(
            boost::bind(&KStateSandeshTest::DropStatsResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }
    void DropStatsResponse(Sandesh *sandesh) {
        response_count_++;
        KDropStatsResp *response = dynamic_cast<KDropStatsResp *>(sandesh);
        if (response != NULL) {
            type_specific_response_count_++;
            num_entries_ = 1;
        }
    }
    void ClearCount() {
        response_count_ = type_specific_response_count_ = num_entries_ = 0;
    }
    uint32_t response_count_;
    uint32_t type_specific_response_count_;
    uint32_t num_entries_;

};

TEST_F(KStateSandeshTest, VxlanTest) {
    //Create 2 vxlan objects in mock Kernel
    KSyncSockTypeMap::VxlanAdd(1);
    KSyncSockTypeMap::VxlanAdd(2);

    //Send Vxlan GET request for label 1
    ClearCount();
    VxlanGet(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send Vxlan GET request for label 2
    ClearCount();
    VxlanGet(2);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send Vxlan DUMP request
    ClearCount();
    VxlanGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(2U, num_entries_);

    //cleanup
    KSyncSockTypeMap::VxlanDelete(1);
    KSyncSockTypeMap::VxlanDelete(2);
}

TEST_F(KStateSandeshTest, VxlanTest_MultiResponse) {
    //Create 100 vrfs in mock Kernel
    for(int i = 1; i <= 100; i++) {
        KSyncSockTypeMap::VxlanAdd(i);
    }
    //Send Vxlan DUMP request
    ClearCount();
    VxlanGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 2));

    //verify the response
    EXPECT_EQ(2U, type_specific_response_count_);
    EXPECT_EQ(100U, num_entries_);

    //cleanup
    for(int i = 1; i <= 100; i++) {
        KSyncSockTypeMap::VxlanDelete(i);
    }
}

TEST_F(KStateSandeshTest, VrfAssignTest) {
    //Create 2 vrf_assign objects in mock Kernel
    vr_vrf_assign_req req1, req2;
    req1.set_var_vif_index(1);
    req1.set_var_vlan_id(1);
    req2.set_var_vif_index(2);
    req2.set_var_vlan_id(2);
    KSyncSockTypeMap::VrfAssignAdd(req1);
    KSyncSockTypeMap::VrfAssignAdd(req2);

    //Send vrf_assign DUMP request for entries starting with index 1
    ClearCount();
    VrfAssignGet(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(2U, num_entries_);

    //Send vrf_assign DUMP request for entries starting with index 2
    ClearCount();
    VrfAssignGet(2);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send vrf_assign DUMP request for entries starting with index -1
    ClearCount();
    VrfAssignGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(2U, num_entries_);

    //cleanup
    KSyncSockTypeMap::VrfAssignDelete(req1);
    KSyncSockTypeMap::VrfAssignDelete(req2);
}

TEST_F(KStateSandeshTest, VrfAssignTest_MultiResponse) {
    //Create 100 vrfs in mock Kernel
    vr_vrf_assign_req req;
    req.set_var_vif_index(1);
    for(int i = 1; i <= 100; i++) {
        req.set_var_vlan_id(i);
        KSyncSockTypeMap::VrfAssignAdd(req);
    }
    //Send Vxlan DUMP request
    ClearCount();
    VrfAssignGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 2));

    //verify the response
    EXPECT_EQ(2U, type_specific_response_count_);
    EXPECT_EQ(100U, num_entries_);

    //cleanup
    req.set_var_vif_index(1);
    for(int i = 1; i <= 100; i++) {
        req.set_var_vlan_id(i);
        KSyncSockTypeMap::VrfAssignDelete(req);
    }
}

TEST_F(KStateSandeshTest, VrfStatsTest) {
    //Create 2 vrfs in mock Kernel
    KSyncSockTypeMap::VrfStatsAdd(1);
    KSyncSockTypeMap::VrfStatsAdd(2);

    //Send VrfStats GET request for vrf-id 1
    ClearCount();
    VrfStatsGet(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send VrfStats GET request for vrf-id 2
    ClearCount();
    VrfStatsGet(2);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send VrfStats DUMP request
    ClearCount();
    VrfStatsGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(2U, num_entries_);

    //cleanup
    KSyncSockTypeMap::VrfStatsDelete(1);
    KSyncSockTypeMap::VrfStatsDelete(2);
}

TEST_F(KStateSandeshTest, VrfStatsTest_MultiResponse) {
    //Create 100 vrfs in mock Kernel
    for(int i = 1; i <= 100; i++) {
        KSyncSockTypeMap::VrfStatsAdd(i);
    }
    //Send VrfStats DUMP request
    ClearCount();
    VrfStatsGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 5));

    //verify the response
    EXPECT_EQ(5U, type_specific_response_count_);
    EXPECT_EQ(100U, num_entries_);

    //cleanup
    for(int i = 1; i <= 100; i++) {
        KSyncSockTypeMap::VrfStatsDelete(i);
    }
}

TEST_F(KStateSandeshTest, DropStatsTest) {
    //Send Drop Stats request 
    ClearCount();
    DropStatsGet();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
}

int main(int argc, char *argv[]) {
    int ret;
    GETUSERARGS();
    
    /* Supported only with non-ksync mode for now */
    ksync_init = false;

    client = TestInit(init_file, ksync_init, true, false);

    ::testing::InitGoogleTest(&argc, argv);
    ret = RUN_ALL_TESTS();
    Agent::GetInstance()->GetEventManager()->Shutdown();
    AsioStop();
    return ret;
}
