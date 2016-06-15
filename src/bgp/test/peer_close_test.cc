/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_peer_close.h"

class PeerCloseManagerTest;

class IPeerCloseTest : public IPeerClose {
public:
    IPeerCloseTest() : graceful_(false), ll_graceful_(false), is_ready_(false),
                       close_graceful_(false), deleted_(false), swept_(false),
                       stale_(false), llgr_stale_(false) {
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
    virtual void Delete() { deleted_ = true; }
    virtual void GracefulRestartStale() { stale_ = true; }
    virtual void LongLivedGracefulRestartStale() { llgr_stale_ = true; }
    virtual void GracefulRestartSweep() { swept_ = true; }
    virtual void GetGracefulRestartFamilies(Families *families) const {
        families->insert(Address::INET);
    }
    virtual const int GetGracefulRestartTime() const { return 123; }
    virtual const int GetLongLivedGracefulRestartTime() const { return 321; }
    virtual bool IsReady() const { return is_ready_; }
    virtual void UnregisterPeer() { }
    virtual void ReceiveEndOfRIB(Address::Family family);

    void set_close_manager(PeerCloseManagerTest *close_manager) {
        close_manager_ = close_manager;
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
    bool deleted() const { return deleted_; }
    bool swept() const { return swept_; }
    bool stale() const { return stale_; }
    bool llgr_stale() const { return llgr_stale_; }

private:
    PeerCloseManagerTest *close_manager_;
    bool graceful_;
    bool ll_graceful_;
    bool is_ready_;
    bool close_graceful_;
    bool deleted_;
    bool swept_;
    bool stale_;
    bool llgr_stale_;
};

class PeerCloseManagerTest : public PeerCloseManager {
public:
    enum Event {
        BEGIN_EVENT,
        CLOSE = BEGIN_EVENT,
        EOR_RECEIVED,
        MEMBERSHIP_REQUEST_COMPLETE_CALLBACK,
        TIMER_CALLBACK,
        END_EVENT = TIMER_CALLBACK
    };

    PeerCloseManagerTest(IPeerClose *peer_close,
                         boost::asio::io_service &io_service) :
            PeerCloseManager(peer_close, io_service), restart_time_(0),
            restart_timer_started_(false), restart_timer_fired_(false) {
    }
    ~PeerCloseManagerTest() { }
    Timer *stale_timer() const { return stale_timer_; }
    Timer *sweep_timer() const { return sweep_timer_; }
    int GetMembershipRequestCount() const {
        return membership_req_pending_;
    }
    virtual bool CanUseMembershipManager() const { return true; }
    virtual BgpMembershipManager *membership_mgr() const { return NULL; }
    virtual void StartRestartTimer(int time) {
        restart_time_ = time;
        restart_timer_started_ = true;
    }
    virtual void TriggerSweepStateActions() {
        ConcurrencyScope scope("bgp::Config");
        ProcessSweepStateActions();
    }
    virtual void StaleNotify() {
        ConcurrencyScope scope("bgp::Config");
        PeerCloseManager::NotifyStaleEvent();
    }
    uint32_t restart_time() const { return restart_time_; }
    bool restart_timer_started() const { return restart_timer_started_; }
    bool GRTimerFired() const { return restart_timer_fired_; }
    void set_restart_timer_fired(bool flag) { restart_timer_fired_ = flag; }

private:
    friend class PeerCloseTest;
    uint32_t restart_time_;
    bool restart_timer_started_;
    bool restart_timer_fired_;
};

void IPeerCloseTest::ReceiveEndOfRIB(Address::Family family) {
    close_manager_->ProcessEORMarkerReceived(family);
}

typedef std::tr1::tuple<int, int, bool, bool, bool, bool, int> TestParams;
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
        pending_requests_ = std::tr1::get<6>(GetParam());
        membership_request_complete_result_ = false;
    }
    virtual void TearDown() { }

    void GotoState() {
        close_manager_->set_state(state_);
        switch (state_) {
            case PeerCloseManager::NONE:
                break;
            case PeerCloseManager::GR_TIMER:
                peer_close_->GetGracefulRestartFamilies(
                    close_manager_->families());
                break;
            case PeerCloseManager::LLGR_TIMER:
                peer_close_->GetGracefulRestartFamilies(
                    close_manager_->families());
                break;
            case PeerCloseManager::SWEEP:
                break;
            case PeerCloseManager::DELETE:
                break;
            case PeerCloseManager::STALE:
                break;
            case PeerCloseManager::LLGR_STALE:
                break;
        }
    }

    void TriggerEvent() {
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:
            close_manager_->Close(!peer_close_->close_graceful());
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            close_manager_->ProcessEORMarkerReceived(Address::INET);
            break;
        case PeerCloseManagerTest::MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            close_manager_->set_membership_state(
                PeerCloseManager::MEMBERSHIP_IN_USE);
            close_manager_->set_membership_req_pending(pending_requests_);
            if (!(close_manager_->state() == PeerCloseManager::STALE ||
                  close_manager_->state() == PeerCloseManager::LLGR_STALE ||
                  close_manager_->state() == PeerCloseManager::SWEEP ||
                  close_manager_->state() == PeerCloseManager::DELETE)) {
                TASK_UTIL_EXPECT_DEATH(
                    close_manager_->MembershipRequestCallback(), ".*");
            } else {
                membership_request_complete_result_ =
                    close_manager_->MembershipRequestCallback();
            }
            break;
        case PeerCloseManagerTest::TIMER_CALLBACK:
            ConcurrencyScope scope("bgp::Config");
            close_manager_->set_restart_timer_fired(true);
            if (close_manager_->RestartTimerCallback())
                close_manager_->RestartTimerCallback();
            close_manager_->set_restart_timer_fired(false);
            break;
        }
    }

    void check_close_at_start() {
        if (!peer_close_->graceful() || !peer_close_->close_graceful()) {
            EXPECT_EQ(PeerCloseManager::DELETE, close_manager_->state());
            EXPECT_FALSE(close_manager_->stale_timer()->running());
            EXPECT_FALSE(close_manager_->sweep_timer()->running());

            // Expect Membership (walk) request.
            EXPECT_TRUE(close_manager_->GetMembershipRequestCount());

            // Trigger unregister peer rib walks completion.
            close_manager_->MembershipRequestCallback();
            EXPECT_EQ(PeerCloseManager::NONE, close_manager_->state());
        } else {

            // Peer supports GR and Graceful closure has been triggered.
            EXPECT_EQ(PeerCloseManager::STALE, close_manager_->state());
            EXPECT_TRUE(peer_close_->stale());
            EXPECT_FALSE(close_manager_->stale_timer()->running());
        }
    }

protected:
    boost::scoped_ptr<PeerCloseManagerTest> close_manager_;
    boost::scoped_ptr<IPeerCloseTest> peer_close_;
    boost::asio::io_service io_service_;
    PeerCloseManagerTest::State state_;
    PeerCloseManagerTest::Event event_;
    int pending_requests_;
    bool membership_request_complete_result_;
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
            check_close_at_start();
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            EXPECT_FALSE(close_manager_->stale_timer()->running());
            EXPECT_FALSE(close_manager_->sweep_timer()->running());

            // No change in state for eor reception in normal lifetime of peer.
            EXPECT_EQ(PeerCloseManager::NONE, close_manager_->state());
            break;
        case PeerCloseManagerTest::MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            break;
        case PeerCloseManagerTest::TIMER_CALLBACK:
            EXPECT_EQ(PeerCloseManager::NONE, close_manager_->state());
            break;
        }
        break;
    case PeerCloseManagerTest::STALE:
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:
            EXPECT_TRUE(close_manager_->close_again());
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            EXPECT_FALSE(close_manager_->stale_timer()->running());
            EXPECT_FALSE(close_manager_->sweep_timer()->running());

            // No change in state for eor reception in normal lifetime of peer.
            EXPECT_EQ(PeerCloseManager::STALE, close_manager_->state());
            break;
        case PeerCloseManagerTest::MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            if (pending_requests_ > 1) {
                EXPECT_FALSE(membership_request_complete_result_);
                break;
            }
            EXPECT_TRUE(membership_request_complete_result_);
            EXPECT_EQ(PeerCloseManager::GR_TIMER, close_manager_->state());
            EXPECT_TRUE(close_manager_->restart_timer_started());
            EXPECT_EQ(1000 * peer_close_->GetGracefulRestartTime(),
                      close_manager_->restart_time());
            break;
        case PeerCloseManagerTest::TIMER_CALLBACK:
            EXPECT_EQ(PeerCloseManager::STALE, close_manager_->state());
            break;
        }
        break;
    case PeerCloseManagerTest::GR_TIMER:
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:

            // Nested closures trigger GR restart
            check_close_at_start();
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:

            // Expect GR timer to start right away to exit from GR.
            EXPECT_TRUE(close_manager_->restart_timer_started());
            EXPECT_EQ(0, close_manager_->restart_time());
            break;
        case PeerCloseManagerTest::MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            break;
        case PeerCloseManagerTest::TIMER_CALLBACK:
            if (peer_close_->IsReady()) {
                EXPECT_EQ(PeerCloseManager::SWEEP, close_manager_->state());
                break;
            }

            if (peer_close_->IsCloseLongLivedGraceful()) {
                EXPECT_EQ(PeerCloseManager::LLGR_STALE,
                          close_manager_->state());
                EXPECT_TRUE(peer_close_->llgr_stale());
                break;
            }

            EXPECT_EQ(PeerCloseManager::DELETE, close_manager_->state());
            break;
        }
        break;
    case PeerCloseManagerTest::LLGR_STALE:
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:
            EXPECT_TRUE(close_manager_->close_again());
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            EXPECT_FALSE(close_manager_->stale_timer()->running());
            EXPECT_FALSE(close_manager_->sweep_timer()->running());
            break;
        case PeerCloseManagerTest::MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            if (pending_requests_ > 1) {
                EXPECT_FALSE(membership_request_complete_result_);
                break;
            }
            EXPECT_TRUE(membership_request_complete_result_);
            EXPECT_EQ(PeerCloseManager::LLGR_TIMER, close_manager_->state());
            EXPECT_TRUE(close_manager_->restart_timer_started());
            EXPECT_EQ(1000 * peer_close_->GetLongLivedGracefulRestartTime(),
                      close_manager_->restart_time());
            break;
        case PeerCloseManagerTest::TIMER_CALLBACK:
            EXPECT_EQ(PeerCloseManager::LLGR_STALE, close_manager_->state());
            break;
        }
        break;
    case PeerCloseManagerTest::LLGR_TIMER:
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:

            // Nested closures trigger GR restart
            check_close_at_start();
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:

            // Expect GR timer to start right away to exit from GR.
            EXPECT_TRUE(close_manager_->restart_timer_started());
            EXPECT_EQ(0, close_manager_->restart_time());
            break;
        case PeerCloseManagerTest::MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            break;
        case PeerCloseManagerTest::TIMER_CALLBACK:
            if (peer_close_->IsReady()) {
                EXPECT_EQ(PeerCloseManager::SWEEP, close_manager_->state());
                break;
            }

            if (peer_close_->IsCloseLongLivedGraceful()) {
                EXPECT_EQ(PeerCloseManager::DELETE, close_manager_->state());
                break;
            }

            EXPECT_EQ(PeerCloseManager::DELETE, close_manager_->state());
            break;
        }
        break;
    case PeerCloseManagerTest::SWEEP:
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:
            EXPECT_TRUE(close_manager_->close_again());
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            EXPECT_EQ(PeerCloseManager::SWEEP, close_manager_->state());
            break;
        case PeerCloseManagerTest::MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            if (pending_requests_ > 1) {
                EXPECT_FALSE(membership_request_complete_result_);
                break;
            }
            EXPECT_TRUE(membership_request_complete_result_);
            EXPECT_EQ(PeerCloseManager::NONE, close_manager_->state());
            EXPECT_TRUE(peer_close_->swept());
            break;
        case PeerCloseManagerTest::TIMER_CALLBACK:
            EXPECT_EQ(PeerCloseManager::SWEEP, close_manager_->state());
            break;
        }
        break;
    case PeerCloseManagerTest::DELETE:
        switch (event_) {
        case PeerCloseManagerTest::CLOSE:
            EXPECT_TRUE(close_manager_->close_again());
            break;
        case PeerCloseManagerTest::EOR_RECEIVED:
            EXPECT_EQ(PeerCloseManager::DELETE, close_manager_->state());
            break;
        case PeerCloseManagerTest::MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            if (pending_requests_ > 1) {
                EXPECT_FALSE(membership_request_complete_result_);
                break;
            }
            EXPECT_TRUE(membership_request_complete_result_);
            EXPECT_EQ(PeerCloseManager::NONE, close_manager_->state());
            EXPECT_TRUE(peer_close_->deleted());
            break;
        case PeerCloseManagerTest::TIMER_CALLBACK:
            EXPECT_EQ(PeerCloseManager::DELETE, close_manager_->state());
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
        ::testing::Bool(), ::testing::Bool(), ::testing::Bool(),
        ::testing::Bool(), ::testing::Range<int>(1, 2)));

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
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
