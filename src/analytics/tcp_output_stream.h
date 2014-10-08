#ifndef SRC_TCP_OUTPUT_STREAM_
#define SRC_TCP_OUTPUT_STREAM_

#include <boost/mpl/list.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/basic_socket.hpp>
#include <boost/statechart/state_machine.hpp>
#include <boost/statechart/simple_state.hpp>
#include <boost/statechart/state.hpp>
#include <boost/statechart/transition.hpp>

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
    // EventManager for asio
    TCPOutputStream(EventManager *, boost::asio::ip::tcp::endpoint);

    // called after dequeue
    virtual bool ProcessMessage(const SandeshStreamData &);

 protected:
    // TODO try to rewrite Contrail's TcpServer implementation to clarify
    // the code
    // TODO not really a server
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

    struct EvConnect : sc::event< EvConnect > {};
    struct EvConnectCompleted : sc::event< EvConnectCompleted > {};
    struct EvConnectFailed : sc::event< EvConnectFailed > {};
    struct EvReconnectTimeout : sc::event< EvReconnectTimeout > {};
    struct EvClosed : sc::event< EvClosed > {};

    struct ReconnectLoop :
        sc::simple_state<ReconnectLoop, ConnectionState, Disconnected>
    {};

    struct Connecting : sc::state<Connecting, ReconnectLoop>
    {
       Connecting(my_context ctx); 

       typedef boost::mpl::list<
        sc::transition<EvConnectCompleted, Connected>,
        sc::transition<EvConnectFailed, Reconnect> > reactions;
    };

    struct Reconnect : sc::state<Reconnect, ReconnectLoop>
    {
        typedef sc::transition<EvReconnectTimeout, Connecting> reactions;

        Reconnect(my_context ctx);
        Timer reconnect_timer;        

      private:
        bool TimeoutCallback();
    };

    struct Connected : sc::state<Connected, ReconnectLoop>
    {
        typedef sc::transition<EvClosed, Reconnect> reactions;

        Connected(my_context ctx);
    };

    struct Disconnected : sc::simple_state<Disconnected, ReconnectLoop>
    {
          typedef sc::transition<EvConnect, Connecting> reactions;
    };

    // This method is called by WorkQueue instance in OutputStreamHandler
    // to determine whether a Sandesh datagram should be dequeued. We'll
    // also notify the queue whenever TcpSession::WriteReady callback is
    // called.
    virtual bool isReadyForDequeue() { return session_->IsEstablished(); }

    void Connect();
    bool SendRaw(const u_int8_t *data, size_t len);

    struct ConnectionState state_machine_;
    // For reconnect timer.
    EventManager *evm_;

    TcpServer::Endpoint endpoint_;
    SessionManager session_mgr_; 
    boost::scoped_ptr<OutputStreamSession> session_;
};

}

#endif
