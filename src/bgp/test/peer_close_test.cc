/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer_close.h"
#include "bgp/bgp_peer_membership.h"

class PeerCloseManagerTest;

class IPeerCloseTest : public IPeerClose {
public:
    explicit IPeerCloseTest() :
            graceful_(false), ll_graceful_(false), is_ready_(false) {
    }
    virtual ~IPeerCloseTest() { }

    // Printable name
    virtual IPeer *peer() const { return NULL; }
    virtual std::string ToString() const { return "IPeerCloseTest"; }
    virtual bool IsCloseGraceful() const { return graceful_; }
    virtual bool IsCloseLongLivedGraceful() const { return ll_graceful_; }
    virtual PeerCloseManager *close_manager();
    virtual void CustomClose() { }
    virtual void CloseComplete() { }
    virtual void Close(bool non_graceful) { }
    virtual void Delete() { }
    virtual void GracefulRestartStale() { }
    virtual void GracefulRestartSweep() { }
    virtual void GetGracefulRestartFamilies(Families *) const { }
    virtual const int GetGracefulRestartTime() const { return 1; }
    virtual const int GetLongLivedGracefulRestartTime() const { return 1; }
    virtual bool IsReady() const { return is_ready_; }
    virtual void UnregisterPeer(
        MembershipRequest::NotifyCompletionFn completion_fn) {
        completion_fn_ = completion_fn;
    }
    void TriggerUnregisterPeerCompletion() {
        EXPECT_FALSE(completion_fn_.empty());
        completion_fn()(NULL, NULL);
        completion_fn_.clear();
    }

    void set_close_manager(PeerCloseManagerTest *close_manager) {
        close_manager_ = close_manager;
    }
    const MembershipRequest::NotifyCompletionFn completion_fn() const {
        return completion_fn_;
    }
    bool graceful() const { return graceful_; }
    void set_graceful(bool graceful) { graceful_ = graceful; }
    bool ll_graceful() const { return graceful_; }
    void set_ll_graceful(bool ll_graceful) { ll_graceful_ = ll_graceful; }
    bool is_ready() const { return is_ready_; }
    void set_is_ready(bool is_ready) { is_ready_ = is_ready; }
    bool close_graceful() const { return close_graceful_; }
    void set_close_graceful(bool close_graceful) {
        close_graceful_ = close_graceful;
    }

private:
    PeerCloseManagerTest *close_manager_;
    bool graceful_;
    bool ll_graceful_;
    bool is_ready_;
    bool close_graceful_;
    MembershipRequest::NotifyCompletionFn completion_fn_;
};

class PeerCloseManagerTest : public PeerCloseManager {
public:
    enum Event {
        BEGIN_EVENT,
        CLOSE = BEGIN_EVENT,
        EOR_RECEIVED,
        END_EVENT = EOR_RECEIVED
    };
    explicit PeerCloseManagerTest(IPeerClose *peer_close,
            boost::asio::io_service &io_service) :
        PeerCloseManager(peer_close, io_service) {
    }
    ~PeerCloseManagerTest() { }
    Timer *stale_timer() const { return stale_timer_; }
    Timer *sweep_timer() const { return sweep_timer_; }
private:
};

typedef std::tr1::tuple<int, int, bool, bool, bool, bool> TestParams;
class PeerCloseTest : public ::testing::TestWithParam<TestParams> {
public:
    virtual void SetUp() {
        peer_close_.reset(new IPeerCloseTest());
        close_manager_.reset(new PeerCloseManagerTest(peer_close_.get(),
                                     io_service_));
        peer_close_->set_close_manager(close_manager_.get());

        state_ = static_cast<PeerCloseManagerTest::State>(
                     std::tr1::get<0>(GetParam()));
        event_ = static_cast<PeerCloseManagerTest::Event>(
                     std::tr1::get<1>(GetParam()));
        peer_close_->set_is_ready(std::tr1::get<2>(GetParam()));
        peer_close_->set_graceful(std::tr1::get<3>(GetParam()));
        peer_close_->set_ll_graceful(std::tr1::get<4>(GetParam()));
        peer_close_->set_close_graceful(std::tr1::get<5>(GetParam()));
    }
    virtual void TearDown() { }

    void GotoState() {
        close_manager_->set_state(state_);
    }

    void TriggerEvent() {
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:
            close_manager_->Close(!peer_close_->close_graceful());
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            close_manager_->ProcessEORMarkerReceived(Address::INET);
            break;
        }
    }

protected:
    boost::scoped_ptr<PeerCloseManagerTest> close_manager_;
    boost::scoped_ptr<IPeerCloseTest> peer_close_;
    boost::asio::io_service io_service_;
    PeerCloseManagerTest::State state_;
    PeerCloseManagerTest::Event event_;
};

PeerCloseManager *IPeerCloseTest::close_manager() { return close_manager_; }

// Non graceful closure. Expect membership walk for ribin and ribout deletes.
TEST_P(PeerCloseTest, Test) {
    GotoState();
    TriggerEvent();

    switch (state_) {
    case PeerCloseManagerTest::NONE:
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:
            if (!peer_close_->graceful() || !peer_close_->close_graceful()) {
                EXPECT_EQ(PeerCloseManager::DELETE, close_manager_->state());
                EXPECT_FALSE(close_manager_->stale_timer()->running());
                EXPECT_FALSE(close_manager_->sweep_timer()->running());

                // Trigger unregister peer rib walks completion.
                peer_close_->TriggerUnregisterPeerCompletion();
                EXPECT_EQ(PeerCloseManager::NONE, close_manager_->state());
                break;
            }

            // Peer supports GR and Graceful closure has been triggered.
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            EXPECT_TRUE(peer_close_->completion_fn().empty());
            EXPECT_FALSE(close_manager_->stale_timer()->running());
            EXPECT_FALSE(close_manager_->sweep_timer()->running());
            EXPECT_EQ(PeerCloseManager::NONE, close_manager_->state());
            break;
        }
        break;
    case PeerCloseManagerTest::STALE:
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            break;
        }
        break;
    case PeerCloseManagerTest::GR_TIMER:
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            break;
        }
        break;
    case PeerCloseManagerTest::LLGR_STALE:
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            break;
        }
        break;
    case PeerCloseManagerTest::LLGR_TIMER:
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            break;
        }
        break;
    case PeerCloseManagerTest::SWEEP:
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            break;
        }
        break;
    case PeerCloseManagerTest::DELETE:
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            break;
        }
        break;
    }
}

INSTANTIATE_TEST_CASE_P(PeerCloseTestWithParams, PeerCloseTest,
    ::testing::Combine(
        ::testing::Range<int>(PeerCloseManagerTest::BEGIN_STATE,
                              PeerCloseManagerTest::END_STATE + 1),
        ::testing::Range<int>(PeerCloseManagerTest::BEGIN_EVENT,
                              PeerCloseManagerTest::END_EVENT + 1),
        ::testing::Bool(), ::testing::Bool(),
        ::testing::Bool(), ::testing::Bool()));

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
