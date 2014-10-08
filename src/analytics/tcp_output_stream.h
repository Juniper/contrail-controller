/*
 * Copyright (c) 2014 Codilime.
 */

#ifndef SRC_TCP_OUTPUT_STREAM_
#define SRC_TCP_OUTPUT_STREAM_

#include <boost/mpl/list.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/basic_socket.hpp>
#include <boost/statechart/state_machine.hpp>
#include <boost/statechart/simple_state.hpp>
#include <boost/statechart/state.hpp>
#include <boost/statechart/transition.hpp>
#include <boost/statechart/deferral.hpp>
#include <boost/statechart/custom_reaction.hpp>

#include "analytics/stream_handler.h"
#include "io/tcp_server.h"
#include "io/tcp_session.h"
#include "base/timer.h"
// WorkQueue
#include "base/queue_task.h"

namespace sc = boost::statechart;

class EventManager;

namespace analytics {

class TCPOutputStream : public OutputStreamHandler {
 public:
    // See: OutputStreamHandler
    static const char *handler_name() { return "TCPOutputStream"; }

    // EventManager for asio and reconnect timer.
    TCPOutputStream(EventManager *, std::string task_id);

    virtual bool Configure(StreamHandlerConfig &);

 protected:
    bool SendRaw(const u_int8_t *data, size_t len);

 private:
    class SessionManager : public TcpServer {
      public:
        SessionManager(EventManager *, TCPOutputStream *owner);

        // we need it to create an OutputStreamSession instance
        virtual TcpSession *AllocSession(Socket *socket);

      protected:
        // for callbacks
        TCPOutputStream *owner_;
    };

    class OutputStreamSession : public TcpSession {
      public:
        OutputStreamSession(TcpServer *, Socket *, TCPOutputStream *owner);

        virtual void OnRead(Buffer buffer) {};
        // registered in the base class
        void OnEvent(TcpSession *session, Event event);
        // a callback called when socket is ready for write
        virtual void WriteReady(const boost::system::error_code &);

      protected:
        TCPOutputStream *owner_;
    };

    // The connection state machine:
    struct ReconnectLoop;
    struct Disconnected;
    struct Connected;
    struct Connecting;
    struct Reconnect;

    struct ConnectionState :
        sc::state_machine<ConnectionState, ReconnectLoop> {
        ConnectionState(TCPOutputStream *owner, EventManager *evm);

        TCPOutputStream *owner;
        EventManager *evm;        
    };

    struct EvConnectCompleted : sc::event<EvConnectCompleted> {};
    struct EvConnectFailed : sc::event<EvConnectFailed> {};
    struct EvReconnectTimeout : sc::event<EvReconnectTimeout> {};
    struct EvClosed : sc::event<EvClosed> {};
    struct EvReconfiguration : sc::event<EvReconfiguration> {};

    struct ReconnectLoop :
        sc::simple_state<ReconnectLoop, ConnectionState, Disconnected>
    {};

    struct Connecting : sc::state<Connecting, ReconnectLoop>
    {
      typedef boost::mpl::list<
           sc::transition<EvConnectCompleted, Connected>,
           sc::transition<EvConnectFailed, Reconnect>,
           sc::deferral<EvReconfiguration> > reactions;

       Connecting(my_context ctx); 
    };

    struct Reconnect : sc::state<Reconnect, ReconnectLoop>
    {
        typedef sc::transition<EvReconnectTimeout, Connecting> reactions;

        Reconnect(my_context ctx);
        ~Reconnect();
        Timer *reconnect_timer;

      private:
        bool TimeoutCallback();
    };

    struct Connected : sc::state<Connected, ReconnectLoop>
    {
        typedef boost::mpl::list<
            sc::transition<EvClosed, Reconnect>,
            sc::custom_reaction<EvReconfiguration> > reactions;

        Connected(my_context ctx);

        sc::result react(const EvReconfiguration &);
    };

    struct Disconnected : sc::simple_state<Disconnected, ReconnectLoop>
    {
        typedef sc::transition<EvReconfiguration, Connecting> reactions;
    };

    // We'll process state machine events in the WorkQueue thread.
    enum SMEvent {
        SM_CONNECT_COMPLETE,
        SM_CONNECT_FAILED,
        SM_CLOSE,
        SM_RECONNECT_TIMEOUT,
        SM_RECONFIGURE
    };
    typedef WorkQueue<SMEvent> SMEventQueue;

    // This method is called by WorkQueue instance in OutputStreamHandler
    // to determine whether a Sandesh datagram should be dequeued. We'll
    // also notify the queue whenever TcpSession::WriteReady callback is
    // called.
    virtual bool isReadyForDequeue() { return session_->IsEstablished(); }

    void Connect();

    bool SMEvDequeue(SMEvent);
    bool isReadyForSMEvDequeue() { return true; }

    struct ConnectionState state_machine_;
    // State Machine events to be processed. 
    SMEventQueue sm_evq_;
    
    TcpServer::Endpoint endpoint_;
    SessionManager session_mgr_; 
    // owned by SessionManager
    OutputStreamSession *session_;
    unsigned int reconnect_timeout_ms_;
};

}

#endif
