/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_STATE_MACHINE_H__
#define __BGP_STATE_MACHINE_H__

#include <boost/statechart/state_machine.hpp>

#include "base/queue_task.h"
#include "base/timer.h"
#include "bgp/bgp_proto.h"
#include "io/tcp_session.h"

namespace sc = boost::statechart;

class BgpPeer;
class BgpSession;
struct BgpSessionDeleter;
class BgpPeerInfo;
class BgpMessage;
class StateMachine;

namespace fsm {
struct Idle;
struct EvBgpNotification;
}

typedef boost::function<bool(StateMachine *)> EvValidate;

class StateMachine : public sc::state_machine<StateMachine, fsm::Idle> {
public:
    static const int kOpenTime = 15;                // seconds
    static const int kConnectInterval = 30;         // seconds
    static const int kHoldTime = 90;                // seconds
    static const int kOpenSentHoldTime = 240;       // seconds
    static const int kIdleHoldTime = 5000;          // milliseconds
    static const int kMaxIdleHoldTime = 100 * 1000; // milliseconds
    static const int kJitter = 10;                  // percentage

    typedef boost::function<void(void)> EventCB;
    enum State {
        IDLE        = 0,
        ACTIVE      = 1,
        CONNECT     = 2,
        OPENSENT    = 3,
        OPENCONFIRM = 4,
        ESTABLISHED = 5
    };

    StateMachine(BgpPeer *peer);
    ~StateMachine();

    static const int GetDefaultHoldTime();

    void Initialize();
    void Shutdown();
    void SetAdminState(bool down);

    // State transitions
    template <class Ev, int code> void OnIdle(const Ev &event);
    void OnIdleNotification(const fsm::EvBgpNotification &event);
    template <class Ev, int code> void OnIdleError(const Ev &event);

    virtual void StartConnectTimer(int seconds);
    void CancelConnectTimer();
    void FireConnectTimer();
    bool ConnectTimerRunning();

    virtual void StartOpenTimer(int seconds);
    void CancelOpenTimer();
    void FireOpenTimer();
    bool OpenTimerRunning();

    virtual void StartHoldTimer();
    void CancelHoldTimer();
    void FireHoldTimer();
    bool HoldTimerRunning();

    virtual void StartIdleHoldTimer();
    void CancelIdleHoldTimer();
    void FireIdleHoldTimer();
    bool IdleHoldTimerRunning();

    void StartSession();
    void DeleteSession(BgpSession *session);

    // Feed session events into the state machine.
    void OnSessionEvent(TcpSession *session, TcpSession::Event event);

    // Receive Passive Open.
    bool PassiveOpen(BgpSession *session);

    // Receive incoming message
    void OnMessage(BgpSession *session, BgpProto::BgpMessage *msg);

    void OnSessionError(BgpSession *session, const ParseErrorContext *context);

    // transfer the ownership of the session to the peer.
    void AssignSession(bool active);

    // Calculate Timer value for active to connect transition.
    int GetConnectTime() const;

    void SendNotificationAndClose(BgpSession *session,
        int code, int subcode = 0, const std::string &data = std::string());
    bool ProcessNotificationEvent(BgpSession *session);
    void SetDataCollectionKey(BgpPeerInfo *peer_info) const;

    const std::string &StateName() const;
    const std::string &LastStateName() const;

    // getters and setters
    BgpPeer *peer() { return peer_; }
    BgpSession *active_session();
    void set_active_session(BgpSession *session);
    BgpSession *passive_session();
    void set_passive_session(BgpSession *session);

    void connect_attempts_inc() { attempts_++; }
    void connect_attempts_clear() { attempts_ = 0; }
    void set_hold_time(int hold_time);
    void reset_hold_time(); 
    int hold_time() const { return hold_time_; }
    int idle_hold_time() const { return idle_hold_time_; }
    void reset_idle_hold_time() { idle_hold_time_ = 0; }
    void set_idle_hold_time(int idle_hold_time) { 
        idle_hold_time_ = idle_hold_time; 
    }

    void set_state(State state);
    State get_state() { return state_; }
    const std::string last_state_change_at() const;

    void set_last_event(const std::string &event);
    const std::string &last_event() const { return last_event_; }
    void set_last_notification_in(int code, int subcode,
        const std::string &reason);
    void set_last_notification_out(int code, int subcode,
        const std::string &reason);
    const std::string last_notification_out_error() const;
    const std::string last_notification_in_error() const;
    void reset_last_info();

    void unconsumed_event(const sc::event_base &event) { }

private:
    friend class StateMachineTest;

    struct EventContainer {
        boost::intrusive_ptr<const sc::event_base> event;
        EvValidate validate;
    };

    void TimerErrorHanlder(std::string name, std::string error);
    bool ConnectTimerExpired();
    bool OpenTimerExpired();
    bool HoldTimerExpired();
    bool IdleHoldTimerExpired();
    void DeleteAllTimers();

    template <typename Ev> bool Enqueue(const Ev &event);
    bool DequeueEvent(EventContainer ec);

    bool IsValidSession() const;

    WorkQueue<EventContainer> work_queue_;
    BgpPeer *peer_;
    BgpSession *active_session_;
    BgpSession *passive_session_;
    Timer *connect_timer_;
    Timer *open_timer_;
    Timer *hold_timer_;
    Timer *idle_hold_timer_;
    int hold_time_;
    int idle_hold_time_;
    int attempts_;
    bool deleted_;
    State state_;
    std::string last_event_;
    uint64_t last_event_at_;
    State last_state_;
    uint64_t last_state_change_at_;
    std::pair<int, int> last_notification_in_;
    std::string last_notification_in_error_;
    uint64_t last_notification_in_at_;
    std::pair<int, int> last_notification_out_;
    std::string last_notification_out_error_;
    uint64_t last_notification_out_at_;

    DISALLOW_COPY_AND_ASSIGN(StateMachine);
};

std::ostream &operator<< (std::ostream &out, const StateMachine::State &state);

#endif
