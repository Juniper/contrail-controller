/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/scoped_ptr.hpp>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_session.h"
#include "control-node/control_node.h"

// Use this test to mock BgpPeer and test selected functionality in BgpPeer as
// desired. e.g. EndOfRibSendTimerExpired() API.

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
          sent_eor_(false) {
    }

    void set_elapsed(uint64_t elapsed) {
        elapsed_ = elapsed;
    }
    void set_output_q_depth(uint64_t output_q_depth) {
        output_q_depth_ = output_q_depth;
    }
    void set_is_ready(bool is_ready) {
        is_ready_ = is_ready;
    }

    virtual void StartKeepaliveTimerUnlocked() { }
    virtual uint64_t GetElapsedTimeSinceLastStateChange() const {
        return elapsed_;
    }
    virtual bool IsReady() const { return is_ready_; }
    virtual void SendEndOfRIBActual(Address::Family family) {
        sent_eor_ = true;
    }
    virtual uint32_t GetOutputQueueDepth(Address::Family family) const {
        return output_q_depth_;
    }

    uint64_t elapsed_;
    uint64_t output_q_depth_;
    bool is_ready_;
    bool sent_eor_;
};

class BgpPeerTest : public ::testing::Test {
protected:
    BgpPeerTest() : server_(&evm_), peer_(NULL) {
    }

    void SetUp() {
        ConcurrencyScope scope("bgp::Config");
        session_.reset(new BgpSessionMock(server_.session_manager()));
        peer_.reset(new BgpPeerMock(&server_, &config_));
        peer_->set_session(session_.get());
        TASK_UTIL_EXPECT_EQ(0, peer_->buffer_len());
    }

    void TearDown() {
        peer_->clear_session();
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    BgpNeighborConfig config_;
    BgpServer server_;
    EventManager evm_;
    boost::scoped_ptr<BgpSessionMock> session_;
    boost::scoped_ptr<BgpPeerMock> peer_;
};

//
// FlushUpdate with an empty buffer does not cause any problems.
//
TEST_F(BgpPeerTest, MessageBuffer1) {
    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(0, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());
}

//
// Single SendUpdate followed by FlushUpdate.
//
TEST_F(BgpPeerTest, MessageBuffer2) {
    static const size_t msgsize = 128;
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(1, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());

    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(1, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(1, session_->message_count());
}

//
// Multiple SendUpdate followed by FlushUpdate.
// Buffer is not full after all SendUpdates.
//
TEST_F(BgpPeerTest, MessageBuffer3) {
    static const size_t msgsize = 128;
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(1, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(2 * msgsize, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(2, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(3 * msgsize, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(3, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());

    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(3, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(1, session_->message_count());
}

//
// Multiple SendUpdate followed by FlushUpdate.
// Buffer has 1 byte left after all SendUpdates.
//
TEST_F(BgpPeerTest, MessageBuffer4) {
    static const size_t msgsize = BgpPeer::kBufferSize - 128;
    static const size_t bufsize = BgpPeer::kBufferSize;
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(1, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());

    peer_->SendUpdate(msg, 127);
    TASK_UTIL_EXPECT_EQ(bufsize - 1, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(2, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());

    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(2, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(1, session_->message_count());
}

//
// Multiple SendUpdate followed by FlushUpdate.
// Buffer is exactly full after all SendUpdates.
//
TEST_F(BgpPeerTest, MessageBuffer5) {
    static const size_t msgsize = BgpPeer::kBufferSize - 128;
    static const size_t bufsize = BgpPeer::kBufferSize;
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(1, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());

    peer_->SendUpdate(msg, 128);
    TASK_UTIL_EXPECT_EQ(bufsize, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(2, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());

    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(2, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(1, session_->message_count());
}

//
// SendUpdate causes call to FlushUpdate.
// Another FlushUpdate flushes the last update.
//
TEST_F(BgpPeerTest, MessageBuffer6) {
    static const size_t msgsize = BgpPeer::kBufferSize - 128;
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(1, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());

    peer_->SendUpdate(msg, 129);
    TASK_UTIL_EXPECT_EQ(129, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(2, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(1, session_->message_count());

    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(2, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(2, session_->message_count());
}

//
// Multiple SendUpdate followed by FlushUpdate.
// Buffer is full after all SendUpdates.
// Session gets cleared before the call to FlushUpdate.
//
TEST_F(BgpPeerTest, MessageBuffer7) {
    static const size_t msgsize = BgpPeer::kBufferSize - 128;
    static const size_t bufsize = BgpPeer::kBufferSize;
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(1, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());

    peer_->SendUpdate(msg, 128);
    TASK_UTIL_EXPECT_EQ(bufsize, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(2, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());

    peer_->clear_session();
    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(2, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());
}

//
// SendUpdate causes call to FlushUpdate.
// Session gets cleared before the call to SendUpdate.
// Another FlushUpdate flushes the last update.
//
TEST_F(BgpPeerTest, MessageBuffer8) {
    static const size_t msgsize = BgpPeer::kBufferSize - 128;
    uint8_t msg[msgsize];

    peer_->SendUpdate(msg, msgsize);
    TASK_UTIL_EXPECT_EQ(msgsize, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(1, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());

    peer_->clear_session();
    peer_->SendUpdate(msg, 129);
    TASK_UTIL_EXPECT_EQ(129, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(2, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());

    peer_->FlushUpdate();
    TASK_UTIL_EXPECT_EQ(0, peer_->buffer_len());
    TASK_UTIL_EXPECT_EQ(2, peer_->get_tx_update());
    TASK_UTIL_EXPECT_EQ(0, session_->message_count());
}

typedef std::tr1::tuple<uint64_t, uint64_t, bool> TestParams;

class BgpPeerParamTest :
    public BgpPeerTest,
    public ::testing::WithParamInterface<TestParams> {
protected:
    BgpPeerParamTest() : elapsed_(0), output_q_depth_(0), is_ready_(false) {
    }

    void SetUp() {
        BgpPeerTest::SetUp();
        elapsed_ = std::tr1::get<0>(GetParam());
        output_q_depth_ = std::tr1::get<1>(GetParam());
        is_ready_ = std::tr1::get<2>(GetParam());
        peer_->set_elapsed(elapsed_);
        peer_->set_output_q_depth(output_q_depth_);
        peer_->set_is_ready(is_ready_);
    }

    void TearDown() {
        BgpPeerTest::TearDown();
    }

    uint64_t elapsed_;
    uint64_t output_q_depth_;
    bool is_ready_;
};

TEST_P(BgpPeerParamTest, SendEndOfRib) {

    // If peer is down, then timer must stop.
    if (!is_ready_) {
        EXPECT_FALSE(peer_->EndOfRibSendTimerExpired(Address::INET));
        EXPECT_FALSE(peer_->sent_eor_);
        return;
    }

    // If elapsed time is less than the kMinEndOfRibSendTimeUsecs, then the timer
    // should fire again.
    if (elapsed_ < BgpPeer::kMinEndOfRibSendTimeUsecs) {
        EXPECT_TRUE(peer_->EndOfRibSendTimerExpired(Address::INET));
        EXPECT_FALSE(peer_->sent_eor_);
        return;
    }

    // If elapsed time is more the max time, eor must be sent out.
    if (elapsed_ >= BgpPeer::kMaxEndOfRibSendTimeUsecs) {
        EXPECT_FALSE(peer_->EndOfRibSendTimerExpired(Address::INET));
        EXPECT_TRUE(peer_->sent_eor_);
        return;
    }

    // If output_q is empty, then EOR must be sent out.
    if (!output_q_depth_) {
        EXPECT_FALSE(peer_->EndOfRibSendTimerExpired(Address::INET));
        EXPECT_TRUE(peer_->sent_eor_);
        return;
    }

    // EOR should not be sent out and timer should fire again.
    EXPECT_TRUE(peer_->EndOfRibSendTimerExpired(Address::INET));
    EXPECT_FALSE(peer_->sent_eor_);
}

INSTANTIATE_TEST_CASE_P(BgpPeerTestWithParams, BgpPeerParamTest,
    testing::Combine(::testing::Values(
        0,
        BgpPeer::kMinEndOfRibSendTimeUsecs/2,
        BgpPeer::kMinEndOfRibSendTimeUsecs,
        (BgpPeer::kMinEndOfRibSendTimeUsecs +
            BgpPeer::kMaxEndOfRibSendTimeUsecs)/2,
        BgpPeer::kMaxEndOfRibSendTimeUsecs,
        BgpPeer::kMaxEndOfRibSendTimeUsecs * 2),
        ::testing::Values(0, 100),
        ::testing::Bool()));

static void SetUp() {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
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
