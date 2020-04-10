/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/scoped_ptr.hpp>

#include "base/misc_utils.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_session.h"
#include "control-node/control_node.h"
#include "config-client-mgr/config_client_manager.h"

// Use this test to mock BgpPeer and test selected functionality in BgpPeer as
// desired. e.g. EndOfRibSendTimerExpired() API.

class StateMachineMock : public StateMachine {
public:
    explicit StateMachineMock(BgpPeer *peer) : StateMachine(peer) { }
    virtual void DeleteSession(BgpSession *session) { }
};

class BgpSessionMock : public BgpSession {
public:
    BgpSessionMock(BgpSessionManager *manager)
        : BgpSession(manager, NULL), message_count_(0) {
    }
    ~BgpSessionMock() { }

    virtual bool Send(const u_int8_t *data, size_t size, size_t *sent) {
        message_count_++;
        return true;
    }

    uint64_t message_count() const { return message_count_; }

private:
    uint64_t message_count_;
};

class BgpPeerMock : public BgpPeer {
public:
    BgpPeerMock(BgpServer *server, const BgpNeighborConfig *config)
        : BgpPeer(server, NULL, config),
          elapsed_(0),
          output_q_depth_(0),
          is_ready_(true),
          starting_up_(false),
          rtarget_table_last_updated_(0),
          sent_eor_(false) {
    }

    void set_elapsed(time_t elapsed) {
        elapsed_ = elapsed;
    }
    void set_output_q_depth(uint64_t output_q_depth) {
        output_q_depth_ = output_q_depth;
    }
    void set_is_ready(bool is_ready) {
        is_ready_ = is_ready;
    }

    void TriggerPrefixLimitCheck() const { }
    virtual void StartKeepaliveTimerUnlocked() { }
    virtual time_t GetEorSendTimerElapsedTime() const { return elapsed_; }
    virtual bool IsReady() const { return is_ready_; }
    virtual void SendEndOfRIBActual(Address::Family family) {
        sent_eor_ = true;
    }
    virtual uint32_t GetOutputQueueDepth(Address::Family family) const {
        return output_q_depth_;
    }
    virtual bool IsServerStartingUp() const { return starting_up_; }
    virtual time_t GetRTargetTableLastUpdatedTimeStamp() const {
        return rtarget_table_last_updated_;
    }
    void set_starting_up(bool starting_up) { starting_up_ = starting_up; }
    void set_rtarget_table_last_updated(time_t rtarget_table_last_updated) {
        rtarget_table_last_updated_ = rtarget_table_last_updated;
    }

    time_t elapsed_;
    uint64_t output_q_depth_;
    bool is_ready_;
    bool starting_up_;
    time_t rtarget_table_last_updated_;
    bool sent_eor_;
};

class BgpPeerTest : public ::testing::Test {
protected:
    BgpPeerTest() : server_(&evm_), peer_(NULL) {
        unsetenv("BGP_PEER_BUFFER_SIZE");
    }

    void SetUp() {
        ConcurrencyScope scope("bgp::Config");
        session_.reset(new BgpSessionMock(server_.session_manager()));
        peer_.reset(new BgpPeerMock(&server_, &config_));
        peer_->set_session(session_.get());
        TASK_UTIL_EXPECT_EQ(0U, peer_->buffer_size());
    }

    void TearDown() {
        ConcurrencyScope scope("bgp::Config");
        peer_->clear_session();
        server_.Shutdown();
        task_util::WaitForIdle();
        peer_.reset();
    }

    void Reset(const BgpNeighborConfig *config) {
        ConcurrencyScope scope("bgp::Config");
        peer_->clear_session();
        peer_->PostCloseRelease();
        peer_.reset(new BgpPeerMock(&server_, config));
        peer_->set_session(session_.get());
    }

    RibExportPolicy BuildRibExportPolicy() {
        ConcurrencyScope scope("bgp::Config");
        return peer_->BuildRibExportPolicy(Address::INETVPN);
    }

    void UpdateConfig(BgpNeighborConfig *config) {
        ConcurrencyScope scope("bgp::Config");
        peer_->ConfigUpdate(config);
    }

    size_t BufferCapacity() { return peer_->buffer_capacity_; }

    BgpNeighborConfig config_;
    BgpServer server_;
    EventManager evm_;
    boost::scoped_ptr<BgpSessionMock> session_;
    boost::scoped_ptr<BgpPeerMock> peer_;
};

//
// Verify that private-as-action configuration is used when building the
// RibExportPolicy.
//
TEST_F(BgpPeerTest, PrivateAsAction) {
    BgpNeighborConfig config;
    RibExportPolicy policy = BuildRibExportPolicy();
    EXPECT_FALSE(policy.remove_private.enabled);
    EXPECT_FALSE(policy.remove_private.all);
    EXPECT_FALSE(policy.remove_private.replace);

    config.set_private_as_action("remove");
    UpdateConfig(&config);
    policy = BuildRibExportPolicy();
    EXPECT_TRUE(policy.remove_private.enabled);
    EXPECT_FALSE(policy.remove_private.all);
    EXPECT_FALSE(policy.remove_private.replace);

    config.set_private_as_action("remove-all");
    UpdateConfig(&config);
    policy = BuildRibExportPolicy();
    EXPECT_TRUE(policy.remove_private.enabled);
    EXPECT_TRUE(policy.remove_private.all);
    EXPECT_FALSE(policy.remove_private.replace);

    config.set_private_as_action("replace-all");
    UpdateConfig(&config);
    policy = BuildRibExportPolicy();
    EXPECT_TRUE(policy.remove_private.enabled);
    EXPECT_TRUE(policy.remove_private.all);
    EXPECT_TRUE(policy.remove_private.replace);

    config.set_private_as_action("remove-all");
    UpdateConfig(&config);
    policy = BuildRibExportPolicy();
    EXPECT_TRUE(policy.remove_private.enabled);
    EXPECT_TRUE(policy.remove_private.all);
    EXPECT_FALSE(policy.remove_private.replace);

    config.set_private_as_action("remove");
    UpdateConfig(&config);
    policy = BuildRibExportPolicy();
    EXPECT_TRUE(policy.remove_private.enabled);
    EXPECT_FALSE(policy.remove_private.all);
    EXPECT_FALSE(policy.remove_private.replace);

    config.set_private_as_action("");
    UpdateConfig(&config);
    policy = BuildRibExportPolicy();
    EXPECT_FALSE(policy.remove_private.enabled);
    EXPECT_FALSE(policy.remove_private.all);
    EXPECT_FALSE(policy.remove_private.replace);
}

//
// Default buffer capacity for regular peer.
//
TEST_F(BgpPeerTest, BufferCapacity1) {
    BgpNeighborConfig config;
    config.set_router_type("control-node");
    Reset(&config);
    size_t capacity = BgpPeer::kMaxBufferCapacity;
    TASK_UTIL_EXPECT_EQ(capacity, BufferCapacity());
}

//
// Default buffer capacity for bgpaas peer.
//
TEST_F(BgpPeerTest, BufferCapacity2) {
    BgpNeighborConfig config;
    config.set_router_type("bgpaas-client");
    Reset(&config);
    size_t capacity = BgpPeer::kMinBufferCapacity;
    TASK_UTIL_EXPECT_EQ(capacity, BufferCapacity());
}

//
// Buffer capacity set via env variable for regular peer.
//
TEST_F(BgpPeerTest, BufferCapacity3) {
    char value[8];
    snprintf(value, sizeof(value), "%zu", BgpPeer::kMaxBufferCapacity / 2);
    setenv("BGP_PEER_BUFFER_SIZE", value, true);
    BgpNeighborConfig config;
    config.set_router_type("control-node");
    Reset(&config);
    size_t capacity = BgpPeer::kMaxBufferCapacity / 2;
    TASK_UTIL_EXPECT_EQ(capacity, BufferCapacity());
}

//
// Buffer capacity set via env variable for for bgpaas peer.
//
TEST_F(BgpPeerTest, BufferCapacity4) {
    char value[8];
    snprintf(value, sizeof(value), "%zu", BgpPeer::kMaxBufferCapacity / 2);
    setenv("BGP_PEER_BUFFER_SIZE", value, true);
    BgpNeighborConfig config;
    config.set_router_type("bgpaas-client");
    Reset(&config);
    size_t capacity = BgpPeer::kMaxBufferCapacity / 2;
    TASK_UTIL_EXPECT_EQ(capacity, BufferCapacity());
}

//
// Buffer capacity set via env variable is too small.
//
TEST_F(BgpPeerTest, BufferCapacity5) {
    char value[8];
    snprintf(value, sizeof(value), "%zu", BgpPeer::kMinBufferCapacity - 1);
    setenv("BGP_PEER_BUFFER_SIZE", value, true);
    BgpNeighborConfig config;
    Reset(&config);
    size_t capacity = BgpPeer::kMinBufferCapacity;
    TASK_UTIL_EXPECT_EQ(capacity, BufferCapacity());
}

//
// Buffer capacity set via env variable is too big.
//
TEST_F(BgpPeerTest, BufferCapacity6) {
    char value[8];
    snprintf(value, sizeof(value), "%zu", BgpPeer::kMaxBufferCapacity + 1);
    setenv("BGP_PEER_BUFFER_SIZE", value, true);
    BgpNeighborConfig config;
    Reset(&config);
    size_t capacity = BgpPeer::kMaxBufferCapacity;
    TASK_UTIL_EXPECT_EQ(capacity, BufferCapacity());
}

//
// Buffer capacity set via env variable is invalid.
//
TEST_F(BgpPeerTest, BufferCapacity7) {
    setenv("BGP_PEER_BUFFER_SIZE", "xyz", true);
    BgpNeighborConfig config;
    Reset(&config);
    size_t capacity = BgpPeer::kMinBufferCapacity;
    TASK_UTIL_EXPECT_EQ(capacity, BufferCapacity());
}

//
// FlushUpdate with an empty buffer does not cause any problems.
//
TEST_F(BgpPeerTest, MessageBuffer1) {
    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0U, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(0U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());
}

//
// Single SendUpdate followed by FlushUpdate.
//
TEST_F(BgpPeerTest, MessageBuffer2) {
    static const size_t msgsize = 128;
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(1U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());

    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0U, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(1U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(1U, session_->message_count());
}

//
// Multiple SendUpdate followed by FlushUpdate.
// Buffer is not full after all SendUpdates.
//
TEST_F(BgpPeerTest, MessageBuffer3) {
    static const size_t msgsize = 128;
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(1U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(2 * msgsize, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(2U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(3 * msgsize, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(3U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());

    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0U, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(3U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(1U, session_->message_count());
}

//
// Multiple SendUpdate followed by FlushUpdate.
// Buffer has 1 byte left after all SendUpdates.
//
TEST_F(BgpPeerTest, MessageBuffer4) {
    static const size_t msgsize = BufferCapacity() - 128;
    static const size_t bufcap = BufferCapacity();
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(1U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());

    peer_->SendUpdate(msg, 127);
    TASK_UTIL_EXPECT_EQ(bufcap - 1, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(2U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());

    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0U, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(2U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(1U, session_->message_count());
}

//
// Multiple SendUpdate followed by FlushUpdate.
// Buffer is exactly full after all SendUpdates.
//
TEST_F(BgpPeerTest, MessageBuffer5) {
    static const size_t msgsize = BufferCapacity() - 128;
    static const size_t bufcap = BufferCapacity();
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(1U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());

    peer_->SendUpdate(msg, 128);
    TASK_UTIL_EXPECT_EQ(bufcap, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(2U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());

    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0U, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(2U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(1U, session_->message_count());
}

//
// SendUpdate causes call to FlushUpdate.
// Another FlushUpdate flushes the last update.
//
TEST_F(BgpPeerTest, MessageBuffer6) {
    static const size_t msgsize = BufferCapacity() - 128;
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(1U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());

    peer_->SendUpdate(msg, 129);
    TASK_UTIL_EXPECT_EQ(129U, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(2U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(1U, session_->message_count());

    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0U, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(2U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(2U, session_->message_count());
}

//
// Multiple SendUpdate followed by FlushUpdate.
// Buffer is full after all SendUpdates.
// Session gets cleared before the call to FlushUpdate.
//
TEST_F(BgpPeerTest, MessageBuffer7) {
    static const size_t msgsize = BufferCapacity() - 128;
    static const size_t bufcap = BufferCapacity();
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(1U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());

    peer_->SendUpdate(msg, 128);
    TASK_UTIL_EXPECT_EQ(bufcap, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(2U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());

    peer_->clear_session();
    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0U, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(2U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());
}

//
// SendUpdate causes call to FlushUpdate.
// Session gets cleared before the call to SendUpdate.
// Another FlushUpdate flushes the last update.
//
TEST_F(BgpPeerTest, MessageBuffer8) {
    static const size_t msgsize = BufferCapacity() - 128;
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(1U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());

    peer_->clear_session();
    peer_->SendUpdate(msg, 129);
    TASK_UTIL_EXPECT_EQ(129U, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(2U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());

    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0U, peer_->buffer_size());
    TASK_UTIL_EXPECT_EQ(2U, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0U, session_->message_count());
}

typedef std::tr1::tuple<time_t, uint64_t, bool, Address::Family, time_t, bool,
                        bool> TestParams;
class BgpPeerParamTest :
    public BgpPeerTest,
    public ::testing::WithParamInterface<TestParams> {
protected:
    BgpPeerParamTest() : elapsed_(0), output_q_depth_(0), is_ready_(false) {
    }

    void SetUp() {
        // Reset startup time for each test.
        MiscUtils::set_startup_time_secs();
        BgpPeerTest::SetUp();
        elapsed_ = std::tr1::get<0>(GetParam());
        output_q_depth_ = std::tr1::get<1>(GetParam());
        is_ready_ = std::tr1::get<2>(GetParam());
        family_ = std::tr1::get<3>(GetParam());
        rtarget_table_last_updated_ = std::tr1::get<4>(GetParam());
        starting_up_ = std::tr1::get<5>(GetParam());
        end_of_config_ = std::tr1::get<6>(GetParam());
        peer_->set_elapsed(elapsed_);
        peer_->set_output_q_depth(output_q_depth_);
        peer_->set_is_ready(is_ready_);
        peer_->set_starting_up(starting_up_);
        ConfigClientManager::set_end_of_rib_computed(end_of_config_);
        peer_->set_rtarget_table_last_updated(rtarget_table_last_updated_);
    }

    void TearDown() {
        BgpPeerTest::TearDown();
    }

    time_t elapsed_;
    uint64_t output_q_depth_;
    bool is_ready_;
    Address::Family family_;
    time_t rtarget_table_last_updated_;
    bool starting_up_;
    bool end_of_config_;
};

TEST_P(BgpPeerParamTest, SendEndOfRib) {
    // If peer is down, then timer must stop.
    if (!is_ready_) {
        EXPECT_FALSE(peer_->EndOfRibSendTimerExpired(family_));
        EXPECT_FALSE(peer_->sent_eor_);
        return;
    }

    // If elapsed time is more the max time, eor must be sent out.
    if (elapsed_ >= BgpGlobalSystemConfig::kEndOfRibTime) {
        EXPECT_FALSE(peer_->EndOfRibSendTimerExpired(family_));
        EXPECT_TRUE(peer_->sent_eor_);
        return;
    }

    // With pending updates, EOR should not be sent out.
    if (output_q_depth_) {
        EXPECT_TRUE(peer_->EndOfRibSendTimerExpired(family_));
        EXPECT_FALSE(peer_->sent_eor_);
        return;
    }

    // If restart phase is complete, EoR must be sent out.
    if (!starting_up_) {
        EXPECT_FALSE(peer_->EndOfRibSendTimerExpired(family_));
        EXPECT_TRUE(peer_->sent_eor_);
        return;
    }

    // If end_of_config is not precessed, EoR must not be sent out.
    if (!end_of_config_) {
        EXPECT_TRUE(peer_->EndOfRibSendTimerExpired(family_));
        EXPECT_FALSE(peer_->sent_eor_);
        return;
    }

    // In the start up phase, eor should not be sent out for any family which
    // is not route-target.
    if (family_ != Address::RTARGET) {
        EXPECT_TRUE(peer_->EndOfRibSendTimerExpired(family_));
        EXPECT_FALSE(peer_->sent_eor_);
        return;
    }

    // If rtarget table was recently updated, do not expect eor to be sent out.
    if (UTCTimestamp() - rtarget_table_last_updated_ <
            0.02 * BgpGlobalSystemConfig::kEndOfRibTime) {
        EXPECT_TRUE(peer_->EndOfRibSendTimerExpired(family_));
        EXPECT_FALSE(peer_->sent_eor_);
        return;
    }

    EXPECT_FALSE(peer_->EndOfRibSendTimerExpired(family_));
    EXPECT_TRUE(peer_->sent_eor_);
}

INSTANTIATE_TEST_CASE_P(
    BgpPeerTestWithParams,
    BgpPeerParamTest,
    testing::Combine(
        ::testing::Values(0,
                          BgpGlobalSystemConfig::kEndOfRibTime / 10,
                          BgpGlobalSystemConfig::kEndOfRibTime / 5,
                          BgpGlobalSystemConfig::kEndOfRibTime / 2,
                          BgpGlobalSystemConfig::kEndOfRibTime,
                          BgpGlobalSystemConfig::kEndOfRibTime * 2),
        ::testing::Values(0U, 100u),
        ::testing::Bool(),
        ::testing::Values(Address::INET, Address::RTARGET),
        ::testing::Values(
            UTCTimestamp(),
            UTCTimestamp() - BgpGlobalSystemConfig::kEndOfRibTime / 5,
            UTCTimestamp() - BgpGlobalSystemConfig::kEndOfRibTime / 2,
            UTCTimestamp() - BgpGlobalSystemConfig::kEndOfRibTime),
        ::testing::Bool(),
        ::testing::Bool()));

static void SetUp() {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineMock *>());
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
