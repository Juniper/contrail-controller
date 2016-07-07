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
#include "io/test/event_manager_test.h"

class PeerCloseManagerTest;

class IPeerCloseTest : public IPeerClose {
public:
    IPeerCloseTest() : graceful_(false), ll_graceful_(false), is_ready_(false),
                       close_graceful_(false), deleted_(false), swept_(false),
                       stale_(false), llgr_stale_(false),
                       membership_request_complete_result_(false) {
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
    virtual void MembershipRequestCallbackComplete(bool result) {
        membership_request_complete_result_ = result;
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
    bool membership_request_complete_result() const {
        return membership_request_complete_result_;
    }

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
    bool membership_request_complete_result_;
};

class PeerCloseManagerTest : public PeerCloseManager {
public:
    PeerCloseManagerTest(IPeerClose *peer_close,
                         boost::asio::io_service &io_service) :
            PeerCloseManager(peer_close, io_service), restart_time_(0),
            restart_timer_started_(false) {
    }
    ~PeerCloseManagerTest() { }
    Timer *stale_timer() const { return stale_timer_; }
    int GetMembershipRequestCount() const {
        return membership_req_pending_;
    }
    virtual bool CanUseMembershipManager() const { return true; }
    virtual BgpMembershipManager *membership_mgr() const { return NULL; }
    virtual void StartRestartTimer(int time) {
        restart_time_ = time;
        restart_timer_started_ = true;
        PeerCloseManager::StartRestartTimer(time);
    }
    int restart_time() const { return restart_time_; }
    bool restart_timer_started() const { return restart_timer_started_; }
    void Run();
    static int GetBeginState() { return BEGIN_STATE; }
    static int GetEndState() { return END_STATE; }
    static int GetBeginEvent() { return BEGIN_EVENT; }
    static int GetEndEvent() { return END_EVENT; }

    void SetUp(int state, int event, int pending_requests,
               IPeerCloseTest *peer_close) {
        test_state_ = static_cast<State>(state);
        test_event_ = static_cast<EventType>(event);
        pending_requests_ = pending_requests;
        peer_close_ = peer_close;
    }

    void GotoState() {
        state_ = test_state_;
        switch (test_state_) {
            case NONE:
                break;
            case GR_TIMER:
                peer_close_->GetGracefulRestartFamilies(families());
                break;
            case LLGR_TIMER:
                peer_close_->GetGracefulRestartFamilies(families());
                break;
            case SWEEP:
                break;
            case DELETE:
                break;
            case STALE:
                break;
            case LLGR_STALE:
                break;
        }
    }

    virtual void MembershipRequestCallback() {
        PeerCloseManager::MembershipRequestCallback();
        task_util::WaitForIdle();
    }

    void TriggerEvent() {
        switch (test_event_) {
        case EVENT_NONE:
            break;
        case CLOSE:
            Close(!peer_close_->close_graceful());
            break;
        case EOR_RECEIVED:
            ProcessEORMarkerReceived(Address::INET);
            break;
        case MEMBERSHIP_REQUEST:
            break;
        case MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            set_membership_state(MEMBERSHIP_IN_USE);
            set_membership_req_pending(pending_requests_);
            if (!(state() == STALE || state() == LLGR_STALE ||
                  state() == SWEEP || state() == DELETE)) {
                TASK_UTIL_EXPECT_DEATH(MembershipRequestCallback(), ".*");
            } else {
                MembershipRequestCallback();
            }
            break;
        case TIMER_CALLBACK:
            task_util::TaskFire(
                    boost::bind(&PeerCloseManager::RestartTimerCallback, this),
                                "timer::TimerTask");
            task_util::WaitForIdle();
            break;
        }
    }

    void check_close_at_start() {
        if (!peer_close_->graceful() || !peer_close_->close_graceful()) {
            TASK_UTIL_EXPECT_EQ(DELETE, state());
            TASK_UTIL_EXPECT_FALSE(stale_timer()->running());

            // Expect Membership (walk) request.
            TASK_UTIL_EXPECT_TRUE(GetMembershipRequestCount());

            // Trigger unregister peer rib walks completion.
            MembershipRequestCallback();
            TASK_UTIL_EXPECT_EQ(NONE, state());
        } else {

            // Peer supports GR and Graceful closure has been triggered.
            TASK_UTIL_EXPECT_EQ(STALE, state());
            TASK_UTIL_EXPECT_TRUE(peer_close_->stale());
            TASK_UTIL_EXPECT_FALSE(stale_timer()->running());
        }
    }

private:
    friend class PeerCloseTest;
    int restart_time_;
    bool restart_timer_started_;
    State test_state_;
    EventType test_event_;
    int pending_requests_;
    IPeerCloseTest *peer_close_;
};

void IPeerCloseTest::ReceiveEndOfRIB(Address::Family family) {
    close_manager_->ProcessEORMarkerReceived(family);
}
PeerCloseManager *IPeerCloseTest::close_manager() { return close_manager_; }

void PeerCloseManagerTest::Run() {
    switch (test_state_) {
    case NONE:
        switch (test_event_) {
        case EVENT_NONE:
            break;
        case MEMBERSHIP_REQUEST:
            break;
        case CLOSE:
            check_close_at_start();
            break;
        case EOR_RECEIVED:
            TASK_UTIL_EXPECT_FALSE(stale_timer()->running());

            // No change in state for eor reception in normal lifetime of peer.
            TASK_UTIL_EXPECT_EQ(NONE, state());
            break;
        case MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            break;
        case TIMER_CALLBACK:
            TASK_UTIL_EXPECT_EQ(NONE, state());
            break;
        }
        break;
    case STALE:
        switch (test_event_) {
        case EVENT_NONE:
            break;
        case MEMBERSHIP_REQUEST:
            break;
        case CLOSE:
            TASK_UTIL_EXPECT_TRUE(close_again());
            break;
        case EOR_RECEIVED:
            TASK_UTIL_EXPECT_FALSE(stale_timer()->running());

            // No change in state for eor reception in normal lifetime of peer.
            TASK_UTIL_EXPECT_EQ(STALE, state());
            break;
        case MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            if (pending_requests_ > 1) {
                TASK_UTIL_EXPECT_FALSE(
                        peer_close_->membership_request_complete_result());
                break;
            }
            TASK_UTIL_EXPECT_TRUE(
                    peer_close_->membership_request_complete_result());
            TASK_UTIL_EXPECT_EQ(GR_TIMER, state());
            TASK_UTIL_EXPECT_TRUE(restart_timer_started());
            TASK_UTIL_EXPECT_EQ(1000 * peer_close_->GetGracefulRestartTime(),
                      restart_time());
            break;
        case TIMER_CALLBACK:
            TASK_UTIL_EXPECT_EQ(STALE, state());
            break;
        }
        break;
    case GR_TIMER:
        switch (test_event_) {
        case EVENT_NONE:
            break;
        case MEMBERSHIP_REQUEST:
            break;
        case CLOSE:

            // Nested closures trigger GR restart
            check_close_at_start();
            break;
        case EOR_RECEIVED:

            // Expect GR timer to start right away to exit from GR.
            TASK_UTIL_EXPECT_TRUE(restart_timer_started());
            TASK_UTIL_EXPECT_EQ(0, restart_time());
            break;
        case MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            break;
        case TIMER_CALLBACK:
            if (peer_close_->IsReady()) {
                TASK_UTIL_EXPECT_EQ(SWEEP, state());
                break;
            }

            if (peer_close_->IsCloseLongLivedGraceful()) {
                TASK_UTIL_EXPECT_EQ(LLGR_STALE,
                          state());
                TASK_UTIL_EXPECT_TRUE(peer_close_->llgr_stale());
                break;
            }

            TASK_UTIL_EXPECT_EQ(DELETE, state());
            break;
        }
        break;
    case LLGR_STALE:
        switch (test_event_) {
        case EVENT_NONE:
            break;
        case MEMBERSHIP_REQUEST:
            break;
        case CLOSE:
            TASK_UTIL_EXPECT_TRUE(close_again());
            break;
        case EOR_RECEIVED:
            TASK_UTIL_EXPECT_FALSE(stale_timer()->running());
            break;
        case MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            if (pending_requests_ > 1) {
                TASK_UTIL_EXPECT_FALSE(
                        peer_close_->membership_request_complete_result());
                break;
            }
            TASK_UTIL_EXPECT_TRUE(
                    peer_close_->membership_request_complete_result());
            TASK_UTIL_EXPECT_EQ(LLGR_TIMER, state());
            TASK_UTIL_EXPECT_TRUE(restart_timer_started());
            TASK_UTIL_EXPECT_EQ(
                    1000 * peer_close_->GetLongLivedGracefulRestartTime(),
                      restart_time());
            break;
        case TIMER_CALLBACK:
            TASK_UTIL_EXPECT_EQ(LLGR_STALE, state());
            break;
        }
        break;
    case LLGR_TIMER:
        switch (test_event_) {
        case EVENT_NONE:
            break;
        case MEMBERSHIP_REQUEST:
            break;
        case CLOSE:

            // Nested closures trigger GR restart
            check_close_at_start();
            break;
        case EOR_RECEIVED:

            // Expect GR timer to start right away to exit from GR.
            TASK_UTIL_EXPECT_TRUE(restart_timer_started());
            TASK_UTIL_EXPECT_EQ(0, restart_time());
            break;
        case MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            break;
        case TIMER_CALLBACK:
            if (peer_close_->IsReady()) {
                TASK_UTIL_EXPECT_EQ(SWEEP, state());
                break;
            }

            if (peer_close_->IsCloseLongLivedGraceful()) {
                TASK_UTIL_EXPECT_EQ(DELETE, state());
                break;
            }

            TASK_UTIL_EXPECT_EQ(DELETE, state());
            break;
        }
        break;
    case SWEEP:
        switch (test_event_) {
        case EVENT_NONE:
            break;
        case MEMBERSHIP_REQUEST:
            break;
        case CLOSE:
            TASK_UTIL_EXPECT_TRUE(close_again());
            break;
        case EOR_RECEIVED:
            TASK_UTIL_EXPECT_EQ(SWEEP, state());
            break;
        case MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            if (pending_requests_ > 1) {
                TASK_UTIL_EXPECT_FALSE(
                        peer_close_->membership_request_complete_result());
                break;
            }
            TASK_UTIL_EXPECT_TRUE(
                    peer_close_->membership_request_complete_result());
            TASK_UTIL_EXPECT_EQ(NONE, state());
            TASK_UTIL_EXPECT_TRUE(peer_close_->swept());
            break;
        case TIMER_CALLBACK:
            TASK_UTIL_EXPECT_EQ(SWEEP, state());
            break;
        }
        break;
    case DELETE:
        switch (test_event_) {
        case EVENT_NONE:
            break;
        case MEMBERSHIP_REQUEST:
            break;
        case CLOSE:
            TASK_UTIL_EXPECT_TRUE(close_again());
            break;
        case EOR_RECEIVED:
            TASK_UTIL_EXPECT_EQ(DELETE, state());
            break;
        case MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            if (pending_requests_ > 1) {
                TASK_UTIL_EXPECT_FALSE(
                        peer_close_->membership_request_complete_result());
                break;
            }
            TASK_UTIL_EXPECT_TRUE(
                    peer_close_->membership_request_complete_result());
            TASK_UTIL_EXPECT_EQ(NONE, state());
            TASK_UTIL_EXPECT_TRUE(peer_close_->deleted());
            break;
        case TIMER_CALLBACK:
            TASK_UTIL_EXPECT_EQ(DELETE, state());
            break;
        }
        break;
    }
}

typedef std::tr1::tuple<int, int, bool, bool, bool, bool, int> TestParams;
class PeerCloseTest : public ::testing::TestWithParam<TestParams> {
public:
    PeerCloseTest() : thread_(&evm_) { }

    virtual void SetUp() {
        peer_close_.reset(new IPeerCloseTest());
        close_manager_.reset(new PeerCloseManagerTest(peer_close_.get(),
                                     *(evm_.io_service())));
        peer_close_->set_close_manager(close_manager_.get());
        close_manager_->SetUp(std::tr1::get<0>(GetParam()),
                              std::tr1::get<1>(GetParam()),
                              std::tr1::get<6>(GetParam()), peer_close_.get());
        peer_close_->set_is_ready(std::tr1::get<2>(GetParam()));
        peer_close_->set_graceful(std::tr1::get<3>(GetParam()));
        peer_close_->set_ll_graceful(std::tr1::get<4>(GetParam()));
        peer_close_->set_close_graceful(std::tr1::get<5>(GetParam()));

        thread_.Start();
    }

    virtual void TearDown() {
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

protected:
    boost::scoped_ptr<PeerCloseManagerTest> close_manager_;
    boost::scoped_ptr<IPeerCloseTest> peer_close_;
    EventManager evm_;
    ServerThread thread_;
};

// Non graceful closure. Expect membership walk for ribin and ribout deletes.
TEST_P(PeerCloseTest, Test) {
    close_manager_->GotoState();
    close_manager_->TriggerEvent();
    close_manager_->Run();
}

INSTANTIATE_TEST_CASE_P(PeerCloseTestWithParams, PeerCloseTest,
    ::testing::Combine(
        ::testing::Range<int>(PeerCloseManagerTest::GetBeginState(),
                              PeerCloseManagerTest::GetEndState() + 1),
        ::testing::Range<int>(PeerCloseManagerTest::GetBeginEvent(),
                              PeerCloseManagerTest::GetEndEvent() + 1),
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
