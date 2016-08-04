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
#include "bgp/inet/inet_table.h"
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
    virtual void MembershipRequestCallbackComplete() {
        membership_request_complete_result_ = true;
    }
    virtual const int GetGracefulRestartTime() const { return 123; }
    virtual const int GetLongLivedGracefulRestartTime() const { return 321; }
    virtual bool IsReady() const { return is_ready_; }
    virtual void UnregisterPeer() { }
    virtual void ReceiveEndOfRIB(Address::Family family);

    virtual const char *GetTaskName() const { return "bgp::StateMachine"; }
    virtual int GetTaskInstance() const { return 0; }

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
            restart_timer_started_(false), can_use_membership_manager_(false),
            is_registerd_(false), unregister_called_(false),
            walk_rib_in_called_(false), unregister_ribout_called_(false),
            unregister_ribin_called_(false), died_(false),
            table_(new InetTable(&db_, "inet.0")) {
    }

    ~PeerCloseManagerTest() {
    }

    Timer *stale_timer() const { return stale_timer_; }
    virtual bool CanUseMembershipManager() const {
        return can_use_membership_manager_;
    }
    virtual void StartRestartTimer(int time) {
        restart_time_ = time;
        restart_timer_started_ = true;
        PeerCloseManager::StartRestartTimer(time);
    }

    void GetRegisteredRibs(std::list<BgpTable *> *tables) {
        tables->push_back(table_.get());
    }

    virtual bool IsRegistered(BgpTable *table) const { return is_registerd_; }
    virtual void Unregister(BgpTable *table) { unregister_called_ = true; }
    virtual void WalkRibIn(BgpTable *table) { walk_rib_in_called_ = true; }
    virtual void UnregisterRibOut(BgpTable *table) {
        unregister_ribout_called_ = true;
    }
    virtual bool IsRibInRegistered(BgpTable *table) const {
        return !is_registerd_;
    }
    virtual void UnregisterRibIn(BgpTable *table) {
        unregister_ribin_called_ = true;
    }

    int restart_time() const { return restart_time_; }
    bool restart_timer_started() const { return restart_timer_started_; }
    void Run();
    static int GetBeginState() { return BEGIN_STATE; }
    static int GetEndState() { return END_STATE; }
    static int GetBeginEvent() { return BEGIN_EVENT; }
    static int GetEndEvent() { return END_EVENT; }
    static int GetBeginMembershipState() { return BEGIN_MEMBERSHIP_STATE; }
    static int GetEndMembershipState() { return END_MEMBERSHIP_STATE; }

    void SetUp(int state, int event, int pending_requests,
               bool can_use_membership_manager, int membership_state,
               bool is_registerd, bool unregister_called,
               bool walk_rib_in_called, bool unregister_ribout_called,
               bool unregister_ribin_called, IPeerCloseTest *peer_close) {
        test_state_ = static_cast<State>(state);
        test_event_ = static_cast<EventType>(event);
        test_membership_state_ = static_cast<MembershipState>(membership_state);
        can_use_membership_manager_ = can_use_membership_manager;
        pending_requests_ = pending_requests;
        peer_close_ = peer_close;
        is_registerd_ = is_registerd;
        unregister_called_= unregister_called;
        walk_rib_in_called_ = walk_rib_in_called;
        unregister_ribout_called_ = unregister_ribout_called;
        unregister_ribin_called_ = unregister_ribin_called;
    }

    void GotoState() {
        state_ = test_state_;
        membership_state_ = test_membership_state_;
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

    virtual bool AssertMembershipState(bool do_assert) {
        died_ = !PeerCloseManager::AssertMembershipState(false);
        return !died_;
    }

    virtual bool AssertMembershipReqCount(bool do_assert) {
        died_ = !PeerCloseManager::AssertMembershipReqCount(false);
        return !died_;
    }

    virtual bool AssertSweepState(bool do_assert) {
        died_ = !PeerCloseManager::AssertSweepState(false);
        return !died_;
    }

    virtual bool AssertMembershipManagerInUse(bool do_assert) {
        died_ = !PeerCloseManager::AssertMembershipManagerInUse(false);
        return !died_;
    }

    void CloseAndDie() {
        Close(!peer_close_->close_graceful());
        TASK_UTIL_EXPECT_TRUE(died_);
    }

    void ProcessEORMarkerReceivedAndDie() {
        ProcessEORMarkerReceived(Address::INET);
        TASK_UTIL_EXPECT_TRUE(died_);
    }

    void MembershipRequestAndDie() {
        MembershipRequest();
        TASK_UTIL_EXPECT_TRUE(died_);
    }

    virtual void MembershipRequestCallbackAndDie() {
        MembershipRequestCallback();
        TASK_UTIL_EXPECT_TRUE(died_);
    }

    void TriggerTimerCallbackAndDie() {
        task_util::TaskFire(
                boost::bind(&PeerCloseManager::RestartTimerCallback, this),
                            "timer::TimerTask");
        TASK_UTIL_EXPECT_TRUE(died_);
    }

    void TriggerEvent() {
        switch (test_event_) {
        case EVENT_NONE:
            break;
        case CLOSE:
            if ((test_state_ == NONE || test_state_ == GR_TIMER ||
                        test_state_ == LLGR_TIMER) &&
                    test_membership_state_ == MEMBERSHIP_IN_USE) {
                CloseAndDie();
                break;
            }
            Close(!peer_close_->close_graceful());
            break;
        case EOR_RECEIVED:
            if ((test_state_ == GR_TIMER || test_state_== LLGR_TIMER) &&
                    test_membership_state_ == MEMBERSHIP_IN_USE) {
                ProcessEORMarkerReceivedAndDie();
                break;
            }
            ProcessEORMarkerReceived(Address::INET);
            break;
        case MEMBERSHIP_REQUEST:
            membership_req_pending_ = pending_requests_;
            if (test_membership_state_ == MEMBERSHIP_IN_USE ||
                    (can_use_membership_manager_ && pending_requests_)) {
                MembershipRequestAndDie();
                break;
            }
            MembershipRequest();
            if (!can_use_membership_manager_)
                TASK_UTIL_EXPECT_EQ(MEMBERSHIP_IN_WAIT, membership_state_);
            else
                TASK_UTIL_EXPECT_EQ(MEMBERSHIP_IN_USE, membership_state_);
            break;
        case MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
            set_membership_state(MEMBERSHIP_IN_USE);
            membership_req_pending_ = pending_requests_;
            if (!(state() == STALE || state() == LLGR_STALE ||
                  state() == SWEEP || state() == DELETE)) {
                MembershipRequestCallbackAndDie();
                break;
            }
            MembershipRequestCallback();
            break;
        case TIMER_CALLBACK:
            if ((test_state_ == GR_TIMER || test_state_== LLGR_TIMER) &&
                    test_membership_state_ == MEMBERSHIP_IN_USE) {
                TriggerTimerCallbackAndDie();
                break;
            }
            task_util::TaskFire(
                    boost::bind(&PeerCloseManager::RestartTimerCallback, this),
                                "timer::TimerTask");
            task_util::WaitForIdle();
            break;
        }
    }

    void check_close_at_start() {
        if (died_)
            return;
        if (!peer_close_->graceful() || !peer_close_->close_graceful()) {
            if (!can_use_membership_manager_) {
                TASK_UTIL_EXPECT_EQ(MEMBERSHIP_IN_WAIT, membership_state_);
                return;
            }

            TASK_UTIL_EXPECT_EQ(DELETE, state());
            TASK_UTIL_EXPECT_FALSE(stale_timer()->running());

            // Expect Membership (walk) request.
            if (is_registerd_)
                TASK_UTIL_EXPECT_TRUE(unregister_called_);
            else
                TASK_UTIL_EXPECT_TRUE(unregister_ribin_called_);
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
    bool can_use_membership_manager_;
    State test_state_;
    EventType test_event_;
    MembershipState test_membership_state_;
    int pending_requests_;

    bool is_registerd_;
    bool unregister_called_;
    bool walk_rib_in_called_;
    bool unregister_ribout_called_;
    bool unregister_ribin_called_;
    bool died_;

    IPeerCloseTest *peer_close_;
    DB db_;
    boost::scoped_ptr<InetTable> table_;
};

void IPeerCloseTest::ReceiveEndOfRIB(Address::Family family) {
    close_manager_->ProcessEORMarkerReceived(family);
}
PeerCloseManager *IPeerCloseTest::close_manager() { return close_manager_; }

void PeerCloseManagerTest::Run() {
    if (died_)
        return;
    switch (test_state_) {
    case NONE:
        switch (test_event_) {
        case EVENT_NONE:
            break;
        case MEMBERSHIP_REQUEST:
            if (!can_use_membership_manager_)
                break;

            if (is_registerd_) {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(false, unregister_called_);
                TASK_UTIL_EXPECT_EQ(false, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(true, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribin_called_);
            } else {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(false, unregister_called_);
                TASK_UTIL_EXPECT_EQ(true, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribin_called_);
            }
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
            if (!can_use_membership_manager_)
                break;
            if (is_registerd_) {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(false, unregister_called_);
                TASK_UTIL_EXPECT_EQ(false, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(true, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribin_called_);
            } else {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(false, unregister_called_);
                TASK_UTIL_EXPECT_EQ(true, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribin_called_);
            }
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
            if (!can_use_membership_manager_)
                break;
            if (is_registerd_) {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(false, unregister_called_);
                TASK_UTIL_EXPECT_EQ(false, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(true, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribin_called_);
            } else {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(false, unregister_called_);
                TASK_UTIL_EXPECT_EQ(true, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribin_called_);
            }
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
            if (!can_use_membership_manager_)
                break;
            if (is_registerd_) {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(false, unregister_called_);
                TASK_UTIL_EXPECT_EQ(false, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(true, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribin_called_);
            } else {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(false, unregister_called_);
                TASK_UTIL_EXPECT_EQ(true, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribin_called_);
            }
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
            if (!can_use_membership_manager_)
                break;
            if (is_registerd_) {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(false, unregister_called_);
                TASK_UTIL_EXPECT_EQ(false, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(true, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribin_called_);
            } else {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(false, unregister_called_);
                TASK_UTIL_EXPECT_EQ(true, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribin_called_);
            }
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
            if (!can_use_membership_manager_)
                break;
            if (is_registerd_) {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(false, unregister_called_);
                TASK_UTIL_EXPECT_EQ(true, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribin_called_);
            } else {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(false, unregister_called_);
                TASK_UTIL_EXPECT_EQ(true, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribin_called_);
            }
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
            if (!can_use_membership_manager_)
                break;
            if (is_registerd_) {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(true, unregister_called_);
                TASK_UTIL_EXPECT_EQ(false, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribin_called_);
            } else {
                TASK_UTIL_EXPECT_EQ(false, is_registerd_);
                TASK_UTIL_EXPECT_EQ(false, unregister_called_);
                TASK_UTIL_EXPECT_EQ(false, walk_rib_in_called_);
                TASK_UTIL_EXPECT_EQ(false, unregister_ribout_called_);
                TASK_UTIL_EXPECT_EQ(true, unregister_ribin_called_);
            }
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

typedef std::tr1::tuple<int, int, bool, bool, bool, bool, int, bool, int, bool>
            TestParams;
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
                              std::tr1::get<6>(GetParam()),
                              std::tr1::get<7>(GetParam()),
                              std::tr1::get<8>(GetParam()),
                              std::tr1::get<9>(GetParam()),

                              // Reuse these params as gtest param limit is 10.
                              std::tr1::get<2>(GetParam()),
                              std::tr1::get<3>(GetParam()),
                              std::tr1::get<4>(GetParam()),
                              std::tr1::get<5>(GetParam()),
                              peer_close_.get());
        peer_close_->set_is_ready(std::tr1::get<2>(GetParam()));
        peer_close_->set_graceful(std::tr1::get<3>(GetParam()));
        peer_close_->set_ll_graceful(std::tr1::get<4>(GetParam()));
        peer_close_->set_close_graceful(std::tr1::get<5>(GetParam()));

        thread_.Start();
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        close_manager_->stale_timer()->Cancel();
        task_util::WaitForIdle();
        close_manager_.reset();
        task_util::WaitForIdle();
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
        ::testing::Bool(), ::testing::Range<int>(1, 2), ::testing::Bool(),
        ::testing::Range<int>(PeerCloseManagerTest::GetBeginMembershipState(),
                              PeerCloseManagerTest::GetEndMembershipState()+1),
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
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
