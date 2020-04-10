/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>

#include "base/task_annotations.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "sandesh/sandesh_trace.h"

#define    TEST_DORMANT_TRACE_BUFFER_SIZE_ZERO   0
#define    TEST_DORMANT_TRACE_BUFFER_THRESHOLD   0

using namespace std;
using namespace boost;

class RoutingInstanceMgrTraceBufferSizeTest : public ::testing::Test {
protected:
    RoutingInstanceMgrTraceBufferSizeTest() : server_(&evm_) {
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

    void DeleteRoutingInstance(BgpInstanceConfigTest *cfg) {
        ConcurrencyScope scope("bgp::Config");

        TaskScheduler::GetInstance()->Stop();
        ri_mgr_->DeleteRoutingInstance(cfg->name());
        TaskScheduler::GetInstance()->Start();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(static_cast<const RoutingInstance *>(NULL),
                ri_mgr_->GetRoutingInstance(cfg->name()));
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

    EventManager evm_;
    BgpServer server_;
    RoutingInstanceMgr *ri_mgr_;
    scoped_ptr<BgpInstanceConfigTest> master_cfg_;
};

TEST_F(RoutingInstanceMgrTraceBufferSizeTest,
        RoutingInstanceTraceBufferSize_Test) {
    bool   set_log_disable = false;
    size_t dormant_tracebuf_capacity =
        GetRoutingInstanceDormantTraceBufferCapacity();

    TASK_UTIL_EXPECT_EQ(0U, dormant_tracebuf_capacity);

    scoped_ptr<BgpInstanceConfigTest> ri1_cfg;
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

    CreateRoutingInstance(ri1_cfg.get());
    CreateRoutingInstance(ri2_cfg.get());
    CreateRoutingInstance(ri3_cfg.get());
    CreateRoutingInstance(ri4_cfg.get());
    CreateRoutingInstance(ri5_cfg.get());
    CreateRoutingInstance(ri6_cfg.get());
    size_t active_tracebuf_count = GetRoutingInstanceActiveTraceBufSize();
    size_t dormant_tracebuf_count = GetRoutingInstanceDormantTraceBufSize();
    TASK_UTIL_EXPECT_EQ(0U, dormant_tracebuf_count);

    DeleteRoutingInstance(ri1_cfg.get());
    DeleteRoutingInstance(ri2_cfg.get());
    TASK_UTIL_EXPECT_EQ(false, HasRoutingInstanceActiveTraceBuf("TestRi#1"));
    TASK_UTIL_EXPECT_EQ(false, HasRoutingInstanceActiveTraceBuf("TestRi#2"));
    TASK_UTIL_EXPECT_EQ((active_tracebuf_count - 2),
                              GetRoutingInstanceActiveTraceBufSize());
    TASK_UTIL_EXPECT_EQ(false, HasRoutingInstanceDormantTraceBuf("TestRi#1"));
    TASK_UTIL_EXPECT_EQ(false, HasRoutingInstanceDormantTraceBuf("TestRi#2"));
    TASK_UTIL_EXPECT_EQ(0U, GetRoutingInstanceDormantTraceBufSize());

    if (set_log_disable == true) {
        SetLoggingDisabled(true);
    }

    unsetenv("CONTRAIL_ROUTING_INSTANCE_DORMANT_TRACE_BUFFER_SIZE");
    unsetenv("CONTRAIL_ROUTING_INSTANCE_DORMANT_TRACE_BUFFER_THRESHOLD");
}


class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();

    // Intialize the environmental variables for this test prior to
    // the creation of the Routing Instance Manager

    char   value[100];
    size_t dormant_trace_buf_size = TEST_DORMANT_TRACE_BUFFER_SIZE_ZERO;
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
