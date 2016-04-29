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

class PeerCloseTest : public IPeerClose {
public:
    explicit PeerCloseTest() :
            graceful_(false), ll_graceful_(false), is_ready_(false) {
    }
    virtual ~PeerCloseTest() { }

    // Printable name
    virtual IPeer *peer() const { return NULL; }
    virtual std::string ToString() const { return "PeerCloseTest"; }
    virtual bool IsCloseGraceful() const { return graceful_; }
    virtual bool IsCloseLongLivedGraceful() const { return ll_graceful_; }
    virtual PeerCloseManager *close_manager();
    virtual void CustomClose() { }
    virtual void CloseComplete() { }
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
    void set_is_ready(bool is_ready) { is_ready_ = is_ready; }

private:
    PeerCloseManagerTest *close_manager_;
    bool graceful_;
    bool ll_graceful_;
    bool is_ready_;
    MembershipRequest::NotifyCompletionFn completion_fn_;
};

class PeerCloseManagerTest : public PeerCloseManager {
public:
    explicit PeerCloseManagerTest(IPeerClose *peer_close,
            boost::asio::io_service &io_service) :
        PeerCloseManager(peer_close, io_service) {
    }
    ~PeerCloseManagerTest() { }
    State state() const { return state_; }
    Timer *stale_timer() const { return stale_timer_; }
    Timer *sweep_timer() const { return sweep_timer_; }
    void set_state (State state) { state_ = state; }
};

class BgpPeerCloseTest : public ::testing::Test {
public:
    virtual void SetUp() {
        peer_close_.reset(new PeerCloseTest());
        close_manager_.reset(new PeerCloseManagerTest(peer_close_.get(),
                                     io_service_));
        peer_close_->set_close_manager(close_manager_.get());
    }
    virtual void TearDown() {
    }

protected:
    boost::scoped_ptr<PeerCloseManagerTest> close_manager_;
    boost::scoped_ptr<PeerCloseTest> peer_close_;
    boost::asio::io_service io_service_;
};

PeerCloseManager *PeerCloseTest::close_manager() { return close_manager_; }

// Non graceful closure. Expect membership walk for ribin and ribout deletes.
TEST_F(BgpPeerCloseTest, State_NONE__Event_Close) {
    close_manager_->set_state(PeerCloseManager::NONE);
    peer_close_->set_graceful(false);
    peer_close_->set_ll_graceful(false);
    peer_close_->set_is_ready(false);

    close_manager_->Close();
    EXPECT_EQ(PeerCloseManager::DELETE, close_manager_->state());
    EXPECT_FALSE(close_manager_->stale_timer()->running());
    EXPECT_FALSE(close_manager_->sweep_timer()->running());

    // Trigger unregister peer rib walks completion.
    peer_close_->TriggerUnregisterPeerCompletion();
    EXPECT_EQ(PeerCloseManager::NONE, close_manager_->state());
}

TEST_F(BgpPeerCloseTest, State_NONE__Event_EorReceived) {
    close_manager_->set_state(PeerCloseManager::NONE);
    peer_close_->set_graceful(false);
    peer_close_->set_ll_graceful(false);
    peer_close_->set_is_ready(false);

    close_manager_->ProcessEORMarkerReceived(Address::INET);
    EXPECT_TRUE(peer_close_->completion_fn().empty());
    EXPECT_FALSE(close_manager_->stale_timer()->running());
    EXPECT_FALSE(close_manager_->sweep_timer()->running());
    EXPECT_EQ(PeerCloseManager::NONE, close_manager_->state());
}

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
