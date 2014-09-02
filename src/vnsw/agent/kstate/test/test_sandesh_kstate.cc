/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "test/test_init.h"
#include "test/test_cmn_util.h"
#include "oper/mirror_table.h"
#include "oper/tunnel_nh.h"
#include "xmpp/test/xmpp_test_util.h"
#include "pkt/test/test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include <vector>

#define vm1_ip "11.1.1.1"
#define vm2_ip "11.1.1.2"
#define remote_vm4_ip "13.1.1.2"
#define remote_router_ip "10.1.1.2"

struct PortInfo input[] = {
        {"flow0", 6, vm1_ip, "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, vm2_ip, "00:00:00:01:01:02", 5, 2},
};

VmInterface *flow0;
VmInterface *flow1;

void RouterIdDepInit(Agent *agent) {
}

class KStateSandeshTest : public ::testing::Test {
public:
    KStateSandeshTest() : response_count_(0), type_specific_response_count_(0), 
    num_entries_(0), error_response_count_(0), internal_error_response_count_(0), 
    peer_(NULL), next_flow_handle_() {
    }

    static void TestSetup() {
    }
    void FlowSetUp() {
        unsigned int vn_count = 0;
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
        client->Reset();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(5);
        vn_count++;

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));

        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmInterfaceGet(input[1].intf_id);
        assert(flow1);
        boost::system::error_code ec;
        peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                              "xmpp channel");
    }

    void FlowTearDown() {
        client->EnqueueFlowFlush();
        client->Reset();
        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle(5);
        client->PortDelNotifyWait(2);
        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));
        DeleteBgpPeer(peer_);
    }

    void CreateRemoteRoute(const char *vrf, const char *remote_vm, 
                           const char *serv, int label, const char *vn) {
        boost::system::error_code ec;
        Ip4Address addr = Ip4Address::from_string(remote_vm, ec);
        Ip4Address gw = Ip4Address::from_string(serv, ec);
        Inet4TunnelRouteAdd(peer_, vrf, addr, 32, gw, TunnelType::AllType(), label, vn,
             SecurityGroupList(), PathPreference());
        client->WaitForIdle(5);
        WAIT_FOR(1000, 500, (RouteFind(vrf, addr, 32) == true));
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
        if (memcmp(sandesh->Name(), "ErrResp", strlen("ErrResp")) == 0) {
            error_response_count_++;
        }
        if (memcmp(sandesh->Name(), "InternalErrResp", strlen("InternalErrResp")) == 0) {
            internal_error_response_count_++;
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

    void RouteGet(int id) {
        KRouteReq *req = new KRouteReq();
        req->set_vrf_id(id);
        Sandesh::set_response_callback(
            boost::bind(&KStateSandeshTest::RouteResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void RouteResponse(Sandesh *sandesh) {
        response_count_++;
        KRouteResp *response = dynamic_cast<KRouteResp *>(sandesh);
        if (response != NULL) {
            type_specific_response_count_++;
            num_entries_ += response->get_rt_list().size();
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

    void FlowGet(int id) {
        KFlowReq *req = new KFlowReq();
        req->set_flow_idx(id);
        Sandesh::set_response_callback(
            boost::bind(&KStateSandeshTest::FlowResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void FlowGetNext() {
        NextKFlowReq *req = new NextKFlowReq();
        req->set_flow_handle(next_flow_handle_);
        Sandesh::set_response_callback(
            boost::bind(&KStateSandeshTest::FlowResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void FlowResponse(Sandesh *sandesh) {
        response_count_++;
        if (memcmp(sandesh->Name(), "KFlowResp", strlen("KFlowResp")) == 0) {
            KFlowResp *response = static_cast<KFlowResp *>(sandesh);
            type_specific_response_count_++;
            num_entries_ += response->get_flow_list().size();
            next_flow_handle_ = response->get_flow_handle();
        } else if (memcmp(sandesh->Name(), "ErrResp", strlen("ErrResp")) == 0) {
            error_response_count_++;
        }
    }

    void ClearCount() {
        response_count_ = type_specific_response_count_ = num_entries_ = 0;
        error_response_count_ = internal_error_response_count_ = 0;
    }
    uint32_t response_count_;
    uint32_t type_specific_response_count_;
    uint32_t num_entries_;
    uint32_t error_response_count_;
    uint32_t internal_error_response_count_;
private:
    BgpPeer *peer_;
    std::string next_flow_handle_;
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

TEST_F(KStateSandeshTest, InterfaceTest_3) {
    //Create 4 interfaces with different flags and mac-size
    int flags = VIF_FLAG_POLICY_ENABLED;
    KSyncSockTypeMap::InterfaceAdd(101, flags, 3);

    flags = VIF_FLAG_MIRROR_RX;
    KSyncSockTypeMap::InterfaceAdd(102, flags, 4);

    flags = VIF_FLAG_MIRROR_TX;
    KSyncSockTypeMap::InterfaceAdd(103, flags, 5);

    flags = VIF_FLAG_POLICY_ENABLED|VIF_FLAG_MIRROR_RX|VIF_FLAG_MIRROR_TX;
    KSyncSockTypeMap::InterfaceAdd(104, flags, 6);

    //Send Interface GET request for interface id 101
    ClearCount();
    InterfaceGet(101);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send Interface GET request for interface id 102
    ClearCount();
    InterfaceGet(102);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send Interface GET request for interface id 103
    ClearCount();
    InterfaceGet(103);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send Interface GET request for interface id 104
    ClearCount();
    InterfaceGet(104);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //cleanup
    KSyncSockTypeMap::InterfaceDelete(101);
    KSyncSockTypeMap::InterfaceDelete(102);
    KSyncSockTypeMap::InterfaceDelete(103);
    KSyncSockTypeMap::InterfaceDelete(104);
}

TEST_F(KStateSandeshTest, InterfaceTest_4) {
    //Send Interface GET request for non-existent interface
    ClearCount();
    InterfaceGet(501);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //Verify the response count
    EXPECT_EQ(1U, error_response_count_);
}

TEST_F(KStateSandeshTest, InterfaceTest_5) {
    //Verify how we behave when we get EBUSY as error for kstate GET request
    KSyncSockTypeMap::set_error_code(EBUSY);
    //Send Interface GET request for non-existent interface
    ClearCount();
    InterfaceGet(501);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //Verify the response count
    EXPECT_EQ(0U, error_response_count_);
    EXPECT_EQ(1U, internal_error_response_count_);

    KSyncSockTypeMap::set_error_code(0);
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

TEST_F(KStateSandeshTest, NhTest_flags) {
    //Send NH DUMP request and store the number of
    //NH entries that we have
    ClearCount();
    NHGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));
    unsigned int num_nexthops = num_entries_;
    
    //Create 18 nexthops in mock Kernel with different flags
    int flags = NH_FLAG_VALID|NH_FLAG_POLICY_ENABLED;
    KSyncSockTypeMap::NHAdd(201, flags);
    flags = NH_FLAG_POLICY_ENABLED;
    KSyncSockTypeMap::NHAdd(202, flags);

    flags = NH_FLAG_VALID|NH_FLAG_TUNNEL_GRE;
    KSyncSockTypeMap::NHAdd(203, flags);
    flags = NH_FLAG_TUNNEL_GRE;
    KSyncSockTypeMap::NHAdd(204, flags);

    flags = NH_FLAG_VALID|NH_FLAG_TUNNEL_UDP;
    KSyncSockTypeMap::NHAdd(205, flags);
    flags = NH_FLAG_TUNNEL_UDP;
    KSyncSockTypeMap::NHAdd(206, flags);

    flags = NH_FLAG_VALID|NH_FLAG_TUNNEL_UDP_MPLS;
    KSyncSockTypeMap::NHAdd(207, flags);
    flags = NH_FLAG_TUNNEL_UDP_MPLS;
    KSyncSockTypeMap::NHAdd(208, flags);

    flags = NH_FLAG_VALID|NH_FLAG_COMPOSITE_ECMP;
    KSyncSockTypeMap::NHAdd(209, flags);
    flags = NH_FLAG_COMPOSITE_ECMP;
    KSyncSockTypeMap::NHAdd(210, flags);

    flags = NH_FLAG_VALID|NH_FLAG_COMPOSITE_FABRIC;
    KSyncSockTypeMap::NHAdd(211, flags);
    flags = NH_FLAG_COMPOSITE_FABRIC;
    KSyncSockTypeMap::NHAdd(212, flags);

    flags = NH_FLAG_VALID|NH_FLAG_COMPOSITE_MULTI_PROTO;
    KSyncSockTypeMap::NHAdd(213, flags);
    flags = NH_FLAG_COMPOSITE_MULTI_PROTO;
    KSyncSockTypeMap::NHAdd(214, flags);

    flags = NH_FLAG_VALID|NH_FLAG_COMPOSITE_L2;
    KSyncSockTypeMap::NHAdd(215, flags);
    flags = NH_FLAG_COMPOSITE_L2;
    KSyncSockTypeMap::NHAdd(216, flags);

    flags = NH_FLAG_VALID|NH_FLAG_COMPOSITE_L3;
    KSyncSockTypeMap::NHAdd(217, flags);
    flags = NH_FLAG_COMPOSITE_L3;
    KSyncSockTypeMap::NHAdd(218, flags);

    //Send NH DUMP request
    ClearCount();
    NHGet(-1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ((18U + num_nexthops), num_entries_);

    //cleanup
    KSyncSockTypeMap::NHDelete(18);
    KSyncSockTypeMap::NHDelete(19);
    KSyncSockTypeMap::NHDelete(201);
    KSyncSockTypeMap::NHDelete(202);
    KSyncSockTypeMap::NHDelete(203);
    KSyncSockTypeMap::NHDelete(204);
    KSyncSockTypeMap::NHDelete(205);
    KSyncSockTypeMap::NHDelete(206);
    KSyncSockTypeMap::NHDelete(207);
    KSyncSockTypeMap::NHDelete(208);
    KSyncSockTypeMap::NHDelete(209);
    KSyncSockTypeMap::NHDelete(210);
    KSyncSockTypeMap::NHDelete(211);
    KSyncSockTypeMap::NHDelete(212);
    KSyncSockTypeMap::NHDelete(213);
    KSyncSockTypeMap::NHDelete(214);
    KSyncSockTypeMap::NHDelete(215);
    KSyncSockTypeMap::NHDelete(216);
    KSyncSockTypeMap::NHDelete(217);
    KSyncSockTypeMap::NHDelete(218);
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

TEST_F(KStateSandeshTest, RouteTest) {
    //Create 2 route objects in mock Kernel
    vr_route_req req1, req2;
    req1.set_rtr_vrf_id(1);
    req1.set_rtr_prefix(0x101010);
    req1.set_rtr_prefix_len(32);
    req1.set_rtr_vrf_id(2);
    req2.set_rtr_prefix(0x202020);
    req2.set_rtr_prefix_len(32);
    KSyncSockTypeMap::RouteAdd(req1);
    KSyncSockTypeMap::RouteAdd(req2);

    //Send route DUMP request for entries of vrf 1
    ClearCount();
    RouteGet(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Send route DUMP request for entries of vrf 2
    ClearCount();
    RouteGet(2);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ == 1));

    //verify the response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //cleanup
    KSyncSockTypeMap::RouteDelete(req1);
    KSyncSockTypeMap::RouteDelete(req2);
}

TEST_F(KStateSandeshTest, RouteTest_MultiResponse) {
    //Create 100 vrfs in mock Kernel
    uint32_t ip = 0x30303000;
    vr_route_req req;
    req.set_rtr_vrf_id(10);
    req.set_rtr_prefix_len(32);
    for(int i = 1; i <= 50; i++) {
        req.set_rtr_prefix((ip +i));
        KSyncSockTypeMap::RouteAdd(req);
    }
    //Send Route DUMP request
    ClearCount();
    RouteGet(10);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (response_count_ >= 2));

    //verify the response
    EXPECT_EQ(2U, type_specific_response_count_);
    EXPECT_EQ(50U, num_entries_);

    //cleanup
    for(int i = 1; i <= 50; i++) {
        req.set_rtr_prefix((ip +i));
        KSyncSockTypeMap::RouteDelete(req);
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
    WAIT_FOR(1000, 1000, (response_count_ == 7));

    //verify the response
    EXPECT_EQ(7U, type_specific_response_count_);
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

TEST_F(KStateSandeshTest, DISABLED_FlowTest_1) {
    FlowSetUp();
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(vm1_ip, vm2_ip, 1, 0, 0, "vrf5", 
                flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(vm2_ip, vm1_ip, 1, 0, 0, "vrf5", 
                flow1->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(vm1_ip, vm2_ip, IPPROTO_TCP, 1000, 200, 
                "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(vm2_ip, vm1_ip, IPPROTO_TCP, 200, 1000, 
                "vrf5", flow1->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 4);
    EXPECT_EQ(4U, Agent::GetInstance()->pkt()->flow_table()->Size());
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_TRUE((fe != NULL));

    uint32_t flow_handle = fe->flow_handle();
    //Fetch a flow using kstate
    ClearCount();
    FlowGet(flow_handle);
    client->WaitForIdle();

    //Verify kstate flow response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(1U, num_entries_);

    //Delete a flow
    DeleteFlow(flow, 1);
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Fetch a deleted flow using kstate
    ClearCount();
    FlowGet(flow_handle);
    client->WaitForIdle();

    //Verify kstate flow response
    EXPECT_EQ(0U, type_specific_response_count_);
    EXPECT_EQ(0U, num_entries_);
    EXPECT_EQ(1U, error_response_count_);

    FlowTearDown();
}

TEST_F(KStateSandeshTest, DISABLED_FlowTest_2) {
    FlowSetUp();
    int total_flows = 110;

    for (int i = 0; i < total_flows; i++) {
        Ip4Address dip(0x1010101 + i);
        //Add route for all of them
        CreateRemoteRoute("vrf5", dip.to_string().c_str(), remote_router_ip, 
                10, "vn5");
        TestFlow flow[]=  {
            {
                TestFlowPkt(vm1_ip, dip.to_string(), 1, 0, 0, "vrf5", 
                        flow0->id(), i),
                { }
            },
            {
                TestFlowPkt(dip.to_string(), vm1_ip, 1, 0, 0, "vrf5",
                        flow0->id(), i + 100),
                { }
            }
        };
        client->WaitForIdle(2);
        CreateFlow(flow, 2);
    }
    EXPECT_EQ((total_flows * 2), 
            Agent::GetInstance()->pkt()->flow_table()->Size());

    //Fetch all the flows using index as -1
    ClearCount();
    FlowGet(-1);
    client->WaitForIdle();

    //Verify kstate flow response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(100U, num_entries_);
    EXPECT_EQ(0U, error_response_count_);

    //Fetch the next set of flows 
    ClearCount();
    FlowGetNext();
    client->WaitForIdle();

    //Verify kstate flow response
    EXPECT_EQ(1U, type_specific_response_count_);
    EXPECT_EQ(100U, num_entries_);
    EXPECT_EQ(0U, error_response_count_);

    //cleanup
    FlowTearDown();
}

int main(int argc, char *argv[]) {
    int ret;
    GETUSERARGS();
    
    /* Supported only with non-ksync mode for now */
    ksync_init = false;

    client = TestInit(init_file, ksync_init, true, false);
    KStateSandeshTest::TestSetup();

    ::testing::InitGoogleTest(&argc, argv);
    ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
