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

    void InterfaceGet(int id) {
        KInterfaceReq *req = new KInterfaceReq();
        req->set_if_id(id);
        Sandesh::set_response_callback(
            boost::bind(&KStateSandeshTest::InterfaceResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }
    void InterfaceResponse(Sandesh *sandesh) {
        response_count_++;
        KInterfaceResp *response = dynamic_cast<KInterfaceResp *>(sandesh);
        if (response != NULL) {
            type_specific_response_count_++;
            num_entries_ += response->get_if_list().size();
        }
    }

    void NHGet(int id) {
        KNHReq *req = new KNHReq();
        req->set_nh_id(id);
        Sandesh::set_response_callback(
            boost::bind(&KStateSandeshTest::NHResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void NHResponse(Sandesh *sandesh) {
        response_count_++;
        KNHResp *response = dynamic_cast<KNHResp *>(sandesh);
        if (response != NULL) {
            type_specific_response_count_++;
            num_entries_ += response->get_nh_list().size();
        }
    }

    void MplsGet(int id) {
        KMplsReq *req = new KMplsReq();
        req->set_mpls_label(id);
        Sandesh::set_response_callback(
            boost::bind(&KStateSandeshTest::MplsResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void MplsResponse(Sandesh *sandesh) {
        response_count_++;
        KMplsResp *response = dynamic_cast<KMplsResp *>(sandesh);
        if (response != NULL) {
            type_specific_response_count_++;
            num_entries_ += response->get_mpls_list().size();
        }
    }

    void MirrorGet(int id) {
        KMirrorReq *req = new KMirrorReq();
        req->set_mirror_id(id);
        Sandesh::set_response_callback(
            boost::bind(&KStateSandeshTest::MirrorResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void MirrorResponse(Sandesh *sandesh) {
        response_count_++;
        KMirrorResp *response = dynamic_cast<KMirrorResp *>(sandesh);
        if (response != NULL) {
            type_specific_response_count_++;
            num_entries_ += response->get_mirror_list().size();
        }
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

TEST_F(KStateSandeshTest, InterfaceTest_1) {
    //Send Interface DUMP request and store the number of
    //interface entries that we have
    ClearCount();
    InterfaceGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));
    unsigned int num_ifs = num_entries_;

    //Create 2 interfaces
    KSyncSockTypeMap::InterfaceAdd(10);
    KSyncSockTypeMap::InterfaceAdd(11);

    //Send Interface GET request for interface id 10
    ClearCount();
    InterfaceGet(10);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send Interface GET request for interface id 11
    ClearCount();
    InterfaceGet(11);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send Interface DUMP request
    ClearCount();
    InterfaceGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ((2U + num_ifs) , num_entries_);

    //cleanup
    KSyncSockTypeMap::InterfaceDelete(10);
    KSyncSockTypeMap::InterfaceDelete(11);
}

TEST_F(KStateSandeshTest, InterfaceTest_2) {
    //Send Interface DUMP request and store the number of
    //interface entries that we have
    ClearCount();
    InterfaceGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));
    unsigned int num_ifs = num_entries_;

    //Create 2 interfaces
    struct PortInfo input[] = {
        {"vnet1", 3, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 4, "2.2.2.2", "00:00:00:02:02:02", 2, 2},
    };
    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));

    //Send Interface DUMP request
    ClearCount();
    InterfaceGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ((2U + num_ifs) , num_entries_);

    //cleanup
    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
}

TEST_F(KStateSandeshTest, InterfaceTest_MultiResponse) {
    //Send Interface DUMP request and store the number of
    //interface entries that we have
    ClearCount();
    InterfaceGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));
    unsigned int num_ifs = num_entries_;

    //Create 50 interfaces in mock kernel
    for(int i = 30; i < 80; i++) {
        KSyncSockTypeMap::InterfaceAdd(i);
    }
    
    //Send Interface DUMP request
    ClearCount();
    InterfaceGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 4));

    //verify the response
    EXPECT_EQ(4U, type_specific_response_count_);
    EXPECT_EQ((50U + num_ifs) , num_entries_);

    //cleanup
    for(int i = 30; i < 80; i++) {
        KSyncSockTypeMap::InterfaceDelete(i);
    }
}

TEST_F(KStateSandeshTest, NhTest) {
    //Send NH DUMP request and store the number of
    //NH entries that we have
    ClearCount();
    NHGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));
    unsigned int num_nexthops = num_entries_;
    
    //Create 2 vrfs in mock Kernel
    KSyncSockTypeMap::NHAdd(18);
    KSyncSockTypeMap::NHAdd(19);

    //Send NH GET request for nh-id 18
    ClearCount();
    NHGet(18);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send NH GET request for nh-id 19
    ClearCount();
    NHGet(19);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send NH DUMP request
    ClearCount();
    NHGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ((2U + num_nexthops), num_entries_);

    //cleanup
    KSyncSockTypeMap::NHDelete(18);
    KSyncSockTypeMap::NHDelete(19);
}

TEST_F(KStateSandeshTest, NhTest_MultiResponse) {
    //Send NH DUMP request and store the number of
    //NH entries that we have
    ClearCount();
    NHGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));
    unsigned int num_nexthops = num_entries_;

    //Create 100 vrfs in mock Kernel
    for(int i = 20; i < 100; i++) {
        KSyncSockTypeMap::NHAdd(i);
    }
    //Send VrfStats DUMP request
    ClearCount();
    NHGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 4));

    //verify the response
    EXPECT_EQ(4U, type_specific_response_count_);
    EXPECT_EQ((80U + num_nexthops), num_entries_);

    //cleanup
    for(int i = 20; i < 100; i++) {
        KSyncSockTypeMap::NHDelete(i);
    }
}

TEST_F(KStateSandeshTest, MplsTest) {
    //Create 2 mpls labels in mock Kernel
    KSyncSockTypeMap::MplsAdd(9);
    KSyncSockTypeMap::MplsAdd(10);

    //Send Mpls GET request for label 9 
    ClearCount();
    MplsGet(9);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send Mpls GET request for label 10
    ClearCount();
    MplsGet(10);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send Mpls DUMP request
    ClearCount();
    MplsGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(2U, num_entries_);

    //cleanup
    KSyncSockTypeMap::MplsDelete(9);
    KSyncSockTypeMap::MplsDelete(10);
}

TEST_F(KStateSandeshTest, MplsTest_MultiResponse) {
    //Create 90 labels in mock Kernel
    for(int i = 11; i <= 100; i++) {
        KSyncSockTypeMap::MplsAdd(i);
    }
    //Send Mpls DUMP request
    ClearCount();
    MplsGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 2));

    //verify the response
    EXPECT_EQ(2U, type_specific_response_count_);
    EXPECT_EQ(90U, num_entries_);

    //cleanup
    for(int i = 11; i <= 100; i++) {
        KSyncSockTypeMap::MplsDelete(i);
    }
}

TEST_F(KStateSandeshTest, MirrorTest) {
    //Create 2 mirror entries in mock Kernel
    KSyncSockTypeMap::MirrorAdd(9);
    KSyncSockTypeMap::MirrorAdd(10);

    //Send Mpls GET request for label 9 
    ClearCount();
    MirrorGet(9);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send Mpls GET request for label 10
    ClearCount();
    MirrorGet(10);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send Mpls DUMP request
    ClearCount();
    MirrorGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(2U, num_entries_);

    //cleanup
    KSyncSockTypeMap::MirrorDelete(9);
    KSyncSockTypeMap::MirrorDelete(10);
}

TEST_F(KStateSandeshTest, MirrorTest_MultiResponse) {
    //Create 100 vrfs in mock Kernel
    for(int i = 11; i <= 100; i++) {
        KSyncSockTypeMap::MirrorAdd(i);
    }
    //Send Mpls DUMP request
    ClearCount();
    MirrorGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ >= 2));

    //verify the response
    EXPECT_EQ(2U, type_specific_response_count_);
    EXPECT_EQ(90U, num_entries_);

    //cleanup
    for(int i = 11; i <= 100; i++) {
        KSyncSockTypeMap::MirrorDelete(i);
    }
}

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
