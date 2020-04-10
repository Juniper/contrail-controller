/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include <fstream>

#include "base/task_annotations.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "control-node/control_node.h"
#include "sandesh/sandesh_trace.h"

#define    TEST_DORMANT_TRACE_BUFFER_SIZE        4
#define    TEST_DORMANT_TRACE_BUFFER_THRESHOLD   2

using namespace std;
using namespace boost;

class RoutingInstanceMgrTest : public ::testing::Test {
protected:
    RoutingInstanceMgrTest() : server_(&evm_) {
        ri_mgr_ = server_.routing_instance_mgr();
    }

    virtual void SetUp() {
        master_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            BgpConfigManager::kMasterInstance));
        CreateRoutingInstance(master_cfg_.get());
    }

    virtual void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
    }

    void CreateRoutingInstance(BgpInstanceConfigTest *cfg) {
        ConcurrencyScope scope("bgp::Config");

        TaskScheduler::GetInstance()->Stop();
        ri_mgr_->CreateRoutingInstance(cfg);
        TaskScheduler::GetInstance()->Start();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_NE(static_cast<const RoutingInstance *>(NULL),
                ri_mgr_->GetRoutingInstance(cfg->name()));
    }

    void UpdateRoutingInstance(BgpInstanceConfigTest *cfg) {
        ConcurrencyScope scope("bgp::Config");

        TaskScheduler::GetInstance()->Stop();
        RoutingInstance *rtinstance = ri_mgr_->GetRoutingInstance(cfg->name());
        assert(rtinstance);
        ri_mgr_->UpdateRoutingInstance(rtinstance, cfg);
        TaskScheduler::GetInstance()->Start();
        task_util::WaitForIdle();
    }

    void DeleteRoutingInstance(BgpInstanceConfigTest *cfg) {
        ConcurrencyScope scope("bgp::Config");

        TaskScheduler::GetInstance()->Stop();
        ri_mgr_->DeleteRoutingInstance(cfg->name());
        TaskScheduler::GetInstance()->Start();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(static_cast<const RoutingInstance *>(NULL),
                ri_mgr_->GetRoutingInstance(cfg->name()));
    }

    const RoutingInstance *FindRoutingInstance(const string name) {
        TASK_UTIL_EXPECT_NE(static_cast<const RoutingInstance *>(NULL),
                ri_mgr_->GetRoutingInstance(name));
        return ri_mgr_->GetRoutingInstance(name);
    }

    ExtCommunityPtr GetExtCommunity(const string target_str) {
        assert(!target_str.empty());
        vector<string> target_list;
        split(target_list, target_str, is_any_of(", "), token_compress_on);

        ExtCommunitySpec spec;
        BOOST_FOREACH(string target, target_list) {
            RouteTarget rtgt = RouteTarget::FromString(target);
            spec.communities.push_back(rtgt.GetExtCommunityValue());
        }

        // Add in a tunnel encap to exercise some more code.
        TunnelEncap tun_encap(TunnelEncapType::MPLS_O_GRE);
        spec.communities.push_back(tun_encap.GetExtCommunityValue());

        return server_.extcomm_db()->Locate(spec);
    }

    int GetVnIndexByExtCommunity(ExtCommunityPtr ext_community) {
        return ri_mgr_->GetVnIndexByExtCommunity(ext_community.get());
    }

    string GetVirtualNetworkByVnIndex(int vn_index) {
        return ri_mgr_->GetVirtualNetworkByVnIndex(vn_index);
    }

    size_t GetRoutingInstanceActiveTraceBufSize() {
        return ri_mgr_->GetRoutingInstanceActiveTraceBufSize();
    }

    size_t GetRoutingInstanceDormantTraceBufSize() {
        return ri_mgr_->GetRoutingInstanceDormantTraceBufSize();
    }

    size_t GetRoutingInstanceDormantTraceBufferCapacity() {
        return ri_mgr_->GetRoutingInstanceDormantTraceBufferCapacity();
    }

    size_t GetRoutingInstanceDormantTraceBufferThreshold() {
        return ri_mgr_->GetRoutingInstanceDormantTraceBufferThreshold();
    }

    bool HasRoutingInstanceActiveTraceBuf(const string &name) {
        return ri_mgr_->HasRoutingInstanceActiveTraceBuf(name);
    }
    bool HasRoutingInstanceDormantTraceBuf(const string &name) {
        return ri_mgr_->HasRoutingInstanceDormantTraceBuf(name);
    }

    SandeshTraceBufferPtr GetDormantTraceBuffer(const string &name) {
        return ri_mgr_->GetDormantTraceBuffer(name);
    }

    SandeshTraceBufferPtr GetActiveTraceBuffer(const string &name) {
        return ri_mgr_->GetActiveTraceBuffer(name);
    }

    EventManager evm_;
    BgpServer server_;
    RoutingInstanceMgr *ri_mgr_;
    scoped_ptr<BgpInstanceConfigTest> master_cfg_;
};

//
// One RI with one target, no VN.
// Lookup based on the vn index should fail.
// Adding a higher target should not change anything.
//
TEST_F(RoutingInstanceMgrTest, VirtualNetworkVnIndex01) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1", "target:100:1"));
    CreateRoutingInstance(ri1_cfg.get());

    // Lookup based on the vn index should fail.
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));

    // Adding a higher target should not change anything.
    BgpTestUtil::UpdateBgpInstanceConfig(ri1_cfg.get(),
            "target:100:1 target:100:99", "target:100:1 target:100:99");
    UpdateRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));

    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));
}

//
// One RI with one target, no VN.
// Lookup based on the vn index should fail.
// Adding a lower target should not change anything.
//
TEST_F(RoutingInstanceMgrTest, VirtualNetworkVnIndex02) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1", "target:100:1"));
    CreateRoutingInstance(ri1_cfg.get());

    // Lookup based on the vn index should fail.
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));

    // Adding a higher target should not change anything.
    BgpTestUtil::UpdateBgpInstanceConfig(ri1_cfg.get(),
            "target:1:99 target:100:1", "target:1:99 target:100:1");
    UpdateRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));

    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));
}

//
// One RI with one target, no VN.
// Lookup based on the vn index should fail.
// Adding the virtual network info should cause lookup to succeed.
//
TEST_F(RoutingInstanceMgrTest, VirtualNetworkVnIndex03) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1", "target:100:1"));
    CreateRoutingInstance(ri1_cfg.get());

    // Lookup based on the vn index should fail.
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));

    // Adding the virtual network info should cause lookup to succeed.
    BgpTestUtil::UpdateBgpInstanceConfig(ri1_cfg.get(), "vn1", 1);
    UpdateRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("vn1", GetVirtualNetworkByVnIndex(1));

    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));
}

//
// One RI with one target.
// Lookup based on the vn index should succeed.
// Removing the virtual network info should cause lookup to fail.
//
TEST_F(RoutingInstanceMgrTest, VirtualNetworkVnIndex04) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1", "target:100:1", "vn1", 1));
    CreateRoutingInstance(ri1_cfg.get());

    // Lookup based on the vn index should succeed.
    TASK_UTIL_EXPECT_EQ("vn1", GetVirtualNetworkByVnIndex(1));

    // Removing the virtual network info should cause lookup to fail.
    BgpTestUtil::UpdateBgpInstanceConfig(ri1_cfg.get(), "", 0);
    UpdateRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));

    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));
}

//
// One RI with one target.
// Lookup based on the vn index should succeed.
// Adding a higher target should not change anything.
//
TEST_F(RoutingInstanceMgrTest, VirtualNetworkVnIndex05) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1", "target:100:1", "vn1", 1));
    CreateRoutingInstance(ri1_cfg.get());

    // Lookup based on the vn index should succeed.
    TASK_UTIL_EXPECT_EQ("vn1", GetVirtualNetworkByVnIndex(1));
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(99));

    // Adding a higher target should not change anything.
    BgpTestUtil::UpdateBgpInstanceConfig(ri1_cfg.get(),
            "target:100:1 target:100:99", "target:100:1 target:100:99");
    UpdateRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("vn1", GetVirtualNetworkByVnIndex(1));

    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));
}

//
// One RI with one target.
// Lookup based on the vn index should succeed.
// Adding a lower target should not change anything.
//
TEST_F(RoutingInstanceMgrTest, VirtualNetworkVnIndex06) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1", "target:100:1", "vn1", 1));
    CreateRoutingInstance(ri1_cfg.get());

    // Lookup based on the vn index should succeed.
    TASK_UTIL_EXPECT_EQ("vn1", GetVirtualNetworkByVnIndex(1));
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(99));

    // Adding a higher target should not change anything.
    BgpTestUtil::UpdateBgpInstanceConfig(ri1_cfg.get(),
            "target:1:99 target:100:1", "target:1:99 target:100:1");
    UpdateRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("vn1", GetVirtualNetworkByVnIndex(1));

    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));
}

//
// One RI with two targets.
// Lookup based on the vn index should succeed.
// Removing the higher target should not change anything.
//
TEST_F(RoutingInstanceMgrTest, VirtualNetworkVnIndex07) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1 target:200:1",
            "target:100:1 target:200:1",
            "vn1", 1));
    CreateRoutingInstance(ri1_cfg.get());

    // Lookup based on the vn index should succeed.
    TASK_UTIL_EXPECT_EQ("vn1", GetVirtualNetworkByVnIndex(1));
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(99));

    // Removing the higher target should not change anything.
    BgpTestUtil::UpdateBgpInstanceConfig(ri1_cfg.get(),
            "target:200:1", "target:200:1");
    UpdateRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("vn1", GetVirtualNetworkByVnIndex(1));

    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));
}

//
// One RI with two targets.
// Lookup based on the vn index should succeed.
// Removing the lower target should not change anything.
//
TEST_F(RoutingInstanceMgrTest, VirtualNetworkVnIndex08) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1 target:200:1",
            "target:100:1 target:200:1",
            "vn1", 1));
    CreateRoutingInstance(ri1_cfg.get());

    // Lookup based on the vn index should succeed.
    TASK_UTIL_EXPECT_EQ("vn1", GetVirtualNetworkByVnIndex(1));
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(99));

    // Removing the lower target should not change anything.
    BgpTestUtil::UpdateBgpInstanceConfig(ri1_cfg.get(),
            "target:100:1", "target:100:1");
    UpdateRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("vn1", GetVirtualNetworkByVnIndex(1));

    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ("unresolved", GetVirtualNetworkByVnIndex(1));
}

//
// One RI with one target.
// Lookup based on the target should succeed.
//
TEST_F(RoutingInstanceMgrTest, VnIndexByExtCommunity01) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1", "target:100:1", "vn1", 1));

    CreateRoutingInstance(ri1_cfg.get());

    // Lookup based on the target should succeed.
    ExtCommunityPtr ext_community_1 = GetExtCommunity("target:100:1");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community_1));

    // Adding in extra target to the lookup should not change anything.
    ExtCommunityPtr ext_community_1x =
            GetExtCommunity("target:100:1 target:100:99");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community_1x));
    ExtCommunityPtr ext_community_1y =
            GetExtCommunity("target:1:99 target:100:1");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community_1y));

    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_1));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_1x));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_1y));
}

//
// One RI with one target.
// Lookup based on unrelated target should fail.
//
TEST_F(RoutingInstanceMgrTest, VnIndexByExtCommunity02) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1", "target:100:1", "vn1", 1));

    CreateRoutingInstance(ri1_cfg.get());

    // Lookup based on unrelated target should fail.
    ExtCommunityPtr ext_community_1x = GetExtCommunity("target:100:99");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_1x));
    ExtCommunityPtr ext_community_1y = GetExtCommunity("target:1:99");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_1y));

    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_1x));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_1y));
}

//
// One RI with two targets.
// Lookup based on either of the targets should succeed.
//
TEST_F(RoutingInstanceMgrTest, VnIndexByExtCommunity03) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1 target:200:1",
            "target:100:1 target:200:1",
            "vn1", 1));

    CreateRoutingInstance(ri1_cfg.get());

    // Lookup based on either of the targets should succeed.
    ExtCommunityPtr ext_community_1 = GetExtCommunity("target:100:1");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community_1));
    ExtCommunityPtr ext_community_2 = GetExtCommunity("target:200:1");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community_2));

    // Adding in extra target to the lookup should not change anything.
    ExtCommunityPtr ext_community_1x =
            GetExtCommunity("target:100:1 target:200:99");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community_1x));
    ExtCommunityPtr ext_community_2y =
            GetExtCommunity("target:1:99 target:200:1");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community_2y));

    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_1));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_2));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_1x));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_2y));
}

//
// One RI with two targets.
// Lookup based on both the targets should succeed.
//
TEST_F(RoutingInstanceMgrTest, VnIndexByExtCommunity04) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1 target:200:1",
            "target:100:1 target:200:1",
            "vn1", 1));

    CreateRoutingInstance(ri1_cfg.get());

    // Lookup based on both the targets should succeed.
    ExtCommunityPtr ext_community_12 =
            GetExtCommunity("target:100:1 target:200:1");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community_12));
    ExtCommunityPtr ext_community_21 =
            GetExtCommunity("target:200:1 target:100:1");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community_21));

    // Adding in extra target to the lookup should not change anything.
    ExtCommunityPtr ext_community_12x =
            GetExtCommunity("target:200:1 target:100:1 target:200:99");
    ExtCommunityPtr ext_community_12y=
            GetExtCommunity("target:1:99 target:100:1 target:200:1");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community_12x));
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community_12y));

    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_12));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_21));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_12x));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_12y));
}

//
// Two RIs with same target, same VN.
// Lookup based on the target should succeed.
//
TEST_F(RoutingInstanceMgrTest, VnIndexByExtCommunity05) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:99", "target:100:99", "vn99", 99));
    scoped_ptr<BgpInstanceConfigTest> ri2_cfg;
    ri2_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri2",
            "target:100:99", "target:100:99", "vn99", 99));

    CreateRoutingInstance(ri1_cfg.get());
    CreateRoutingInstance(ri2_cfg.get());

    // Lookup based on the target should succeed.
    ExtCommunityPtr ext_community = GetExtCommunity("target:100:99");
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community));

    // Adding in extra target to the lookup should not change anything.
    ExtCommunityPtr ext_community_x =
            GetExtCommunity("target:100:99 target:100:100");
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community_x));
    ExtCommunityPtr ext_community_y =
            GetExtCommunity("target:1:99 target:100:99");
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community_y));

    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community));
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community_x));
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community_y));
    DeleteRoutingInstance(ri2_cfg.get());
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_x));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_y));
}

// Unit test for RoutingInstance Trace buffers
//
// Lookup based on the target should succeed.
// Creation of RIs will trigger Tracebuffers being created in "Active Map" list
// Deletion of RIs will move the Trace buffers from Active to "Dormant Map" list
//
TEST_F(RoutingInstanceMgrTest, RoutingInstanceTraceBuffer_Test) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    bool   set_log_disable = false;
    SandeshTraceBufferPtr   trace_buf;

    TASK_UTIL_EXPECT_EQ(4U, GetRoutingInstanceDormantTraceBufferCapacity());
    TASK_UTIL_EXPECT_EQ(2U, GetRoutingInstanceDormantTraceBufferThreshold());

    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("TestRi#1"));
    scoped_ptr<BgpInstanceConfigTest> ri2_cfg;
    ri2_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("TestRi#2"));
    scoped_ptr<BgpInstanceConfigTest> ri3_cfg;
    ri3_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("TestRi#3"));
    scoped_ptr<BgpInstanceConfigTest> ri4_cfg;
    ri4_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("TestRi#4"));
    scoped_ptr<BgpInstanceConfigTest> ri5_cfg;
    ri5_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("TestRi#5"));
    scoped_ptr<BgpInstanceConfigTest> ri6_cfg;
    ri6_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("TestRi#6"));

    if (LoggingDisabled()) {
        set_log_disable = true;
        SetLoggingDisabled(false);
    }

    size_t active_tracebuf_count = GetRoutingInstanceActiveTraceBufSize();
    size_t dormant_tracebuf_count = GetRoutingInstanceDormantTraceBufSize();
    TASK_UTIL_EXPECT_EQ(false, HasRoutingInstanceActiveTraceBuf("TestRi#1"));
    TASK_UTIL_EXPECT_EQ(false, HasRoutingInstanceActiveTraceBuf("TestRi#2"));

    CreateRoutingInstance(ri1_cfg.get());
    CreateRoutingInstance(ri2_cfg.get());
    TASK_UTIL_EXPECT_EQ(true, HasRoutingInstanceActiveTraceBuf("TestRi#1"));
    TASK_UTIL_EXPECT_EQ(true, HasRoutingInstanceActiveTraceBuf("TestRi#2"));
    TASK_UTIL_EXPECT_EQ(false, HasRoutingInstanceDormantTraceBuf("TestRi#1"));
    TASK_UTIL_EXPECT_EQ(false, HasRoutingInstanceDormantTraceBuf("TestRi#2"));
    TASK_UTIL_EXPECT_EQ((active_tracebuf_count + 2),
                           GetRoutingInstanceActiveTraceBufSize());
    TASK_UTIL_EXPECT_EQ(dormant_tracebuf_count,
                           GetRoutingInstanceDormantTraceBufSize());

    trace_buf = GetActiveTraceBuffer("TestRi#1");
    DeleteRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ(false, HasRoutingInstanceActiveTraceBuf("TestRi#1"));
    TASK_UTIL_EXPECT_EQ(true,
            HasRoutingInstanceDormantTraceBuf("TestRi#1"));
    TASK_UTIL_EXPECT_EQ((dormant_tracebuf_count + 1),
            GetRoutingInstanceDormantTraceBufSize());
    TASK_UTIL_EXPECT_EQ(trace_buf, GetDormantTraceBuffer("TestRi#1"));
    TASK_UTIL_EXPECT_EQ((active_tracebuf_count + 1),
                GetRoutingInstanceActiveTraceBufSize());

    DeleteRoutingInstance(ri2_cfg.get());
    TASK_UTIL_EXPECT_EQ(false, HasRoutingInstanceActiveTraceBuf("TestRi#1"));
    TASK_UTIL_EXPECT_EQ(false, HasRoutingInstanceActiveTraceBuf("TestRi#2"));
    TASK_UTIL_EXPECT_EQ(true,
            HasRoutingInstanceDormantTraceBuf("TestRi#1"));
    TASK_UTIL_EXPECT_EQ(true,
            HasRoutingInstanceDormantTraceBuf("TestRi#2"));
    TASK_UTIL_EXPECT_EQ((dormant_tracebuf_count + 2),
            GetRoutingInstanceDormantTraceBufSize());
    TASK_UTIL_EXPECT_EQ(active_tracebuf_count,
            GetRoutingInstanceActiveTraceBufSize());

    CreateRoutingInstance(ri1_cfg.get());
    TASK_UTIL_EXPECT_EQ(true, HasRoutingInstanceActiveTraceBuf("TestRi#1"));
    TASK_UTIL_EXPECT_EQ((active_tracebuf_count + 1),
            GetRoutingInstanceActiveTraceBufSize());
    TASK_UTIL_EXPECT_EQ(trace_buf, GetActiveTraceBuffer("TestRi#1"));
    TASK_UTIL_EXPECT_EQ(false,
            HasRoutingInstanceDormantTraceBuf("TestRi#1"));
    TASK_UTIL_EXPECT_EQ((dormant_tracebuf_count + 1),
            GetRoutingInstanceDormantTraceBufSize());
    TASK_UTIL_EXPECT_NE(trace_buf, GetDormantTraceBuffer("TestRi#1"));

    CreateRoutingInstance(ri2_cfg.get());
    CreateRoutingInstance(ri3_cfg.get());
    CreateRoutingInstance(ri4_cfg.get());
    CreateRoutingInstance(ri5_cfg.get());
    CreateRoutingInstance(ri6_cfg.get());
    active_tracebuf_count = GetRoutingInstanceActiveTraceBufSize();
    dormant_tracebuf_count = GetRoutingInstanceDormantTraceBufSize();


    DeleteRoutingInstance(ri1_cfg.get());
    DeleteRoutingInstance(ri2_cfg.get());
    DeleteRoutingInstance(ri3_cfg.get());

    TASK_UTIL_EXPECT_EQ((dormant_tracebuf_count + 3),
            GetRoutingInstanceDormantTraceBufSize());
    DeleteRoutingInstance(ri4_cfg.get());
    // checking with threshold
    DeleteRoutingInstance(ri5_cfg.get());
    TASK_UTIL_EXPECT_EQ(3U, GetRoutingInstanceDormantTraceBufSize());
    TASK_UTIL_EXPECT_EQ(false,
            HasRoutingInstanceDormantTraceBuf("TestRi#1"));
    TASK_UTIL_EXPECT_EQ(true,
            HasRoutingInstanceDormantTraceBuf("TestRi#5"));

    if (set_log_disable == true) {
        SetLoggingDisabled(true);
    }

    unsetenv("CONTRAIL_ROUTING_INSTANCE_DORMANT_TRACE_BUFFER_SIZE");
    unsetenv("CONTRAIL_ROUTING_INSTANCE_DORMANT_TRACE_BUFFER_THRESHOLD");
}

//
// Two RIs with same target, different VNs.
// Lookup based on the target should fail.
//
TEST_F(RoutingInstanceMgrTest, VnIndexByExtCommunity06) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:99", "target:100:99", "vn1", 1));
    scoped_ptr<BgpInstanceConfigTest> ri2_cfg;
    ri2_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri2",
            "target:100:99", "target:100:99", "vn2", 2));

    CreateRoutingInstance(ri1_cfg.get());
    CreateRoutingInstance(ri2_cfg.get());

    // Lookup based on target should fail.
    ExtCommunityPtr ext_community = GetExtCommunity("target:100:99");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community));

    // Adding in extra target to the lookup should not change anything.
    ExtCommunityPtr ext_community_x =
            GetExtCommunity("target:100:99 target:100:999");
    ExtCommunityPtr ext_community_y =
            GetExtCommunity("target:100:1 target:100:99");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_x));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_y));

    DeleteRoutingInstance(ri1_cfg.get());
    DeleteRoutingInstance(ri2_cfg.get());
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community));
}

//
// Two RIs with different targets, different VNs.
// Lookup based on each of the individual targets should succeed.
//
TEST_F(RoutingInstanceMgrTest, VnIndexByExtCommunity07) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1", "target:100:1", "vn1", 1));
    scoped_ptr<BgpInstanceConfigTest> ri2_cfg;
    ri2_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri2",
            "target:100:2", "target:100:2", "vn2", 2));

    CreateRoutingInstance(ri1_cfg.get());
    CreateRoutingInstance(ri2_cfg.get());

    // Lookup based on each of the individual targets should succeed.
    ExtCommunityPtr ext_community_1 = GetExtCommunity("target:100:1");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community_1));
    ExtCommunityPtr ext_community_2 = GetExtCommunity("target:100:2");
    TASK_UTIL_EXPECT_EQ(2, GetVnIndexByExtCommunity(ext_community_2));

    // Adding in extra target to the lookups should not change anything.
    ExtCommunityPtr ext_community_1x =
            GetExtCommunity("target:100:1 target:100:99");
    ExtCommunityPtr ext_community_2y =
            GetExtCommunity("target:1:99 target:100:2");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community_1x));
    TASK_UTIL_EXPECT_EQ(2, GetVnIndexByExtCommunity(ext_community_2y));

    DeleteRoutingInstance(ri1_cfg.get());
    DeleteRoutingInstance(ri2_cfg.get());
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_1));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_2));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_1x));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_2y));
}

//
// Two RIs with different targets, different VNs.
// Lookup based on both targets should fail.
//
TEST_F(RoutingInstanceMgrTest, VnIndexByExtCommunity08) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1", "target:100:1", "vn1", 1));
    scoped_ptr<BgpInstanceConfigTest> ri2_cfg;
    ri2_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri2",
            "target:100:2", "target:100:2", "vn2", 2));

    CreateRoutingInstance(ri1_cfg.get());
    CreateRoutingInstance(ri2_cfg.get());

    // Lookup based on both targets should fail.
    ExtCommunityPtr ext_community_12 =
            GetExtCommunity("target:100:1 target:100:2");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_12));
    ExtCommunityPtr ext_community_21 =
            GetExtCommunity("target:100:2 target:100:1");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_21));

    // Adding in extra target to the lookup should not change anything.
    ExtCommunityPtr ext_community_12x =
            GetExtCommunity("target:100:1 target:100:2 target:100:99");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_12x));
    ExtCommunityPtr ext_community_12y =
            GetExtCommunity("target:1:99 target:100:1 target:100:2");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_12y));

    DeleteRoutingInstance(ri1_cfg.get());
    DeleteRoutingInstance(ri2_cfg.get());
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_12));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_21));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_12x));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_12y));
}

//
// Two RIs with same target, different VNs.
// Third RIs with different target, different VN.
// Third RI has smaller target the others RIs.
// Lookup based on both targets should fail.
//
TEST_F(RoutingInstanceMgrTest, VnIndexByExtCommunity09) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:99", "target:100:99", "vn1", 1));
    scoped_ptr<BgpInstanceConfigTest> ri2_cfg;
    ri2_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri2",
            "target:100:99", "target:100:99", "vn2", 2));
    scoped_ptr<BgpInstanceConfigTest> ri3_cfg;
    ri3_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri3",
            "target:100:3", "target:100:3", "vn3", 3));

    CreateRoutingInstance(ri1_cfg.get());
    CreateRoutingInstance(ri2_cfg.get());
    CreateRoutingInstance(ri3_cfg.get());

    // Lookup based on both targets should fail.
    ExtCommunityPtr ext_community_993 =
            GetExtCommunity("target:100:99 target:100:3");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_993));
    ExtCommunityPtr ext_community_399 =
            GetExtCommunity("target:100:3 target:100:99");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_399));

    // Adding in extra target to the lookup should not change anything.
    ExtCommunityPtr ext_community_993x =
            GetExtCommunity("target:100:99 target:100:3 target:100:999");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_993x));
    ExtCommunityPtr ext_community_399y =
            GetExtCommunity("target:1:99 target:100:3 target:100:99");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_399y));

    DeleteRoutingInstance(ri1_cfg.get());
    DeleteRoutingInstance(ri2_cfg.get());
    DeleteRoutingInstance(ri3_cfg.get());
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_993));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_399));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_993x));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_399y));
}

//
// Two RIs with same target, different VNs.
// Third RIs with different target, different VN.
// Third RI has bigger target the others RIs.
// Lookup based on both targets should fail.
//
TEST_F(RoutingInstanceMgrTest, VnIndexByExtCommunity10) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:99", "target:100:99", "vn1", 1));
    scoped_ptr<BgpInstanceConfigTest> ri2_cfg;
    ri2_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri2",
            "target:100:99", "target:100:99", "vn2", 2));
    scoped_ptr<BgpInstanceConfigTest> ri3_cfg;
    ri3_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri3",
            "target:1000:3", "target:1000:3", "vn3", 3));

    CreateRoutingInstance(ri1_cfg.get());
    CreateRoutingInstance(ri2_cfg.get());
    CreateRoutingInstance(ri3_cfg.get());

    // Lookup based on both targets should fail.
    ExtCommunityPtr ext_community_993 =
            GetExtCommunity("target:100:99 target:1000:3");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_993));
    ExtCommunityPtr ext_community_399 =
            GetExtCommunity("target:1000:3 target:100:99");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_399));

    // Adding in extra target to the lookup should not change anything.
    ExtCommunityPtr ext_community_993x =
            GetExtCommunity("target:100:99 target:1000:3 target:1000:999");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_993x));
    ExtCommunityPtr ext_community_399y =
            GetExtCommunity("target:1:99 target:1000:3 target:100:99");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_399y));

    DeleteRoutingInstance(ri1_cfg.get());
    DeleteRoutingInstance(ri2_cfg.get());
    DeleteRoutingInstance(ri3_cfg.get());
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_993));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_399));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_993x));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_399y));
}

//
// Two RIs with different targets, same VN.
// Lookup based on each of the individual targets should succeed.
//
TEST_F(RoutingInstanceMgrTest, VnIndexByExtCommunity11) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1", "target:100:1", "vn99", 99));
    scoped_ptr<BgpInstanceConfigTest> ri2_cfg;
    ri2_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri2",
            "target:100:2", "target:100:2", "vn99", 99));

    CreateRoutingInstance(ri1_cfg.get());
    CreateRoutingInstance(ri2_cfg.get());

    // Lookup based on each of the individual targets should succeed.
    ExtCommunityPtr ext_community_1 = GetExtCommunity("target:100:1");
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community_1));
    ExtCommunityPtr ext_community_2 = GetExtCommunity("target:100:2");
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community_2));

    // Adding in extra target to the lookups should not change anything.
    ExtCommunityPtr ext_community_1x =
            GetExtCommunity("target:100:1 target:100:99");
    ExtCommunityPtr ext_community_2y =
            GetExtCommunity("target:1:99 target:100:2");
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community_1x));
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community_2y));

    DeleteRoutingInstance(ri1_cfg.get());
    DeleteRoutingInstance(ri2_cfg.get());
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_1));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_2));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_1x));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_2y));
}

//
// Two RIs with different targets, same VN.
// Lookup based on both targets should succeed.
//
TEST_F(RoutingInstanceMgrTest, VnIndexByExtCommunity12) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1", "target:100:1", "vn99", 99));
    scoped_ptr<BgpInstanceConfigTest> ri2_cfg;
    ri2_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri2",
            "target:100:2", "target:100:2", "vn99", 99));

    CreateRoutingInstance(ri1_cfg.get());
    CreateRoutingInstance(ri2_cfg.get());

    // Lookup based on both targets should succeed.
    ExtCommunityPtr ext_community_12 =
            GetExtCommunity("target:100:1 target:100:2");
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community_12));
    ExtCommunityPtr ext_community_21 =
            GetExtCommunity("target:100:2 target:100:1");
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community_21));

    // Adding in extra target to the lookup should not change anything.
    ExtCommunityPtr ext_community_12x =
            GetExtCommunity("target:100:1 target:100:2 target:100:99");
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community_12x));
    ExtCommunityPtr ext_community_12y =
            GetExtCommunity("target:1:99 target:100:1 target:100:2");
    TASK_UTIL_EXPECT_EQ(99, GetVnIndexByExtCommunity(ext_community_12y));

    DeleteRoutingInstance(ri1_cfg.get());
    DeleteRoutingInstance(ri2_cfg.get());
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_12));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_21));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_12x));
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community_12y));
}

//
// Two RIs with a common export only target, different VNs.
// The 2 RIs also have their unique import + export targets.
// Lookup based on the common export target should fail.
// Lookup based on the unique target should succeed.
// Lookup based on the both targets should succeed.
//
TEST_F(RoutingInstanceMgrTest, VnIndexByExtCommunity13) {
    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
    ri1_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri1",
            "target:100:1", "target:100:1 target:100:99", "vn1", 1));
    scoped_ptr<BgpInstanceConfigTest> ri2_cfg;
    ri2_cfg.reset(BgpTestUtil::CreateBgpInstanceConfig("ri2",
            "target:100:2", "target:100:99 target:100:2", "vn2", 2));

    CreateRoutingInstance(ri1_cfg.get());
    CreateRoutingInstance(ri2_cfg.get());

    // Lookup based on common target should fail.
    ExtCommunityPtr ext_community = GetExtCommunity("target:100:99");
    TASK_UTIL_EXPECT_EQ(0, GetVnIndexByExtCommunity(ext_community));

    // Lookup based on unique targets should succeed.
    ExtCommunityPtr ext_community1 = GetExtCommunity("target:100:1");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community1));
    ExtCommunityPtr ext_community2 = GetExtCommunity("target:100:2");
    TASK_UTIL_EXPECT_EQ(2, GetVnIndexByExtCommunity(ext_community2));

    // Lookup based on common and unique targets should succeed.
    ExtCommunityPtr ext_community1x =
        GetExtCommunity("target:100:1 target:100:99");
    ExtCommunityPtr ext_community1y =
        GetExtCommunity("target:100:99 target:100:1");
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community1x));
    TASK_UTIL_EXPECT_EQ(1, GetVnIndexByExtCommunity(ext_community1y));

    // Lookup based on common and unique targets should succeed.
    ExtCommunityPtr ext_community2x =
        GetExtCommunity("target:100:2 target:100:99");
    ExtCommunityPtr ext_community2y =
        GetExtCommunity("target:100:99 target:100:2");
    TASK_UTIL_EXPECT_EQ(2, GetVnIndexByExtCommunity(ext_community2x));
    TASK_UTIL_EXPECT_EQ(2, GetVnIndexByExtCommunity(ext_community2y));

    DeleteRoutingInstance(ri1_cfg.get());
    DeleteRoutingInstance(ri2_cfg.get());
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    char   value[100];

    ControlNode::SetDefaultSchedulingPolicy();

    // Intialize the environmental variables for this test prior to
    // the creation of the Routing Instance Manager
    size_t dormant_trace_buf_size = TEST_DORMANT_TRACE_BUFFER_SIZE;
    snprintf(value, sizeof(value), "%zu", dormant_trace_buf_size);
    setenv("CONTRAIL_ROUTING_INSTANCE_DORMANT_TRACE_BUFFER_SIZE",
            value, true);

    size_t trace_buf_threshold = TEST_DORMANT_TRACE_BUFFER_THRESHOLD;
    snprintf(value, sizeof(value), "%zu", trace_buf_threshold);
    setenv("CONTRAIL_ROUTING_INSTANCE_DORMANT_TRACE_BUFFER_THRESHOLD",
            value, true);
}

static void TearDown() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
