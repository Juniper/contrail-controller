/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/scoped_ptr.hpp>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"

// Use this test to mock BgpPeer and test selected functionality in BgpPeer as
// desired. e.g. EndOfRibSendTimerExpired() API.

class BgpPeerMock : public BgpPeer {
public:
    BgpPeerMock(BgpServer *server, const BgpNeighborConfig *config,
                uint64_t elapsed, bool is_ready, uint64_t output_q_depth) :
            BgpPeer(server, NULL, config),
            elapsed_(elapsed), output_q_depth_(output_q_depth),
            is_ready_(is_ready), sent_eor_(false) {
    }
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

typedef std::tr1::tuple<uint64_t, uint64_t, bool> TestParams;
class BgpPeerTest : public ::testing::TestWithParam<TestParams> {
protected:
    BgpPeerTest() : server_(&evm_), peer_(NULL), elapsed_(0), output_q_depth_(0),
                    is_ready_(false) {
    }

    void SetUp() {
        ConcurrencyScope scope("bgp::Config");
        elapsed_ = std::tr1::get<0>(GetParam());
        output_q_depth_ = std::tr1::get<1>(GetParam());
        is_ready_ = std::tr1::get<2>(GetParam());
        peer_.reset(new BgpPeerMock(&server_, &config_, elapsed_, is_ready_,
                                    output_q_depth_));
    }

    void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    BgpNeighborConfig config_;
    BgpServer server_;
    EventManager evm_;
    boost::scoped_ptr<BgpPeerMock> peer_;

    uint64_t elapsed_;
    uint64_t output_q_depth_;
    bool is_ready_;
};

TEST_P(BgpPeerTest, SendEndOfRib) {

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

INSTANTIATE_TEST_CASE_P(BgpPeerTestWithParams, BgpPeerTest,
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
