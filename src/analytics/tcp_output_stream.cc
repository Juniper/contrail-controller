/*
 *  Copyright (c) 2014 Codilime.
 */

#include "analytics/tcp_output_stream.h"

#include <sstream>
#include <boost/bind.hpp>
#include <boost/statechart/state_machine.hpp>
#include <boost/statechart/simple_state.hpp>
#include <pugixml/pugixml.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "analytics/stream_manager.h"
#include "io/event_manager.h"
#include "base/logging.h"

namespace sc = boost::statechart;
using analytics::TCPOutputStream;

TCPOutputStream::TCPOutputStream(EventManager *evm,
    std::string task_id)
: // base class constructor with task name as an arg
  OutputStreamHandler(evm, task_id),
  state_machine_(this, evm),
  sm_evq_(task_id_, -1,
          boost::bind(&TCPOutputStream::SMEvDequeue, this, _1)),
  session_mgr_(evm, this),
  session_(NULL),
  reconnect_timeout_ms_(500) {
      sm_evq_.SetStartRunnerFunc(
          boost::bind(&TCPOutputStream::isReadyForSMEvDequeue, this));
      state_machine_.initiate();
}

TCPOutputStream::~TCPOutputStream() {
    session_->Close();
    session_mgr_.DeleteSession(session_);
    session_mgr_.Shutdown();
}

bool TCPOutputStream::Configure(StreamHandlerConfig &config) {
    boost::system::error_code malformed_ip;
    std::string addr_str = config.get<std::string>("stream.dest_addr");
    unsigned short port = config.get<unsigned short>("stream.dest_port");
    endpoint_ = boost::asio::ip::tcp::endpoint(
        boost::asio::ip::address::from_string(addr_str, malformed_ip) ,port);
    if (malformed_ip)
        return false;

    reconnect_timeout_ms_ =
        config.get<unsigned int>("stream.reconnect_timeout_ms");

    sm_evq_.Enqueue(SM_RECONFIGURE);
    return true;
}

TCPOutputStream::OutputStreamSession::OutputStreamSession(
    TcpServer *mgr, Socket *socket, TCPOutputStream *owner)
: TcpSession(mgr, socket),
  owner_(owner) {
    set_observer(boost::bind(&OutputStreamSession::OnEvent, this, _1, _2));
}

void TCPOutputStream::OutputStreamSession::OnEvent(TcpSession *session,
                                                        Event event) {
    switch (event) {
         case CONNECT_COMPLETE:
             owner_->sm_evq_.Enqueue(SM_CONNECT_COMPLETE);
             break;
         case CONNECT_FAILED:
             owner_->sm_evq_.Enqueue(SM_CONNECT_FAILED);
             break;
         case CLOSE:
             owner_->sm_evq_.Enqueue(SM_CLOSE);
             break;
         default:
             break;
    }
}

void TCPOutputStream::OutputStreamSession::WriteReady(
                const boost::system::error_code &ec) {
    // Dequeue one or more entries from WorkQueue, if it's not already
    // happening.
    if (!ec)
        owner_->output_workqueue_.MayBeStartRunner();
}

void TCPOutputStream::Connect() {
    session_mgr_.Connect(session_, endpoint_);
}

bool TCPOutputStream::SendRaw(const u_int8_t *data, size_t len) {
    stats_.messages_sent++;
    return session_->Send(data, len, NULL);
}

TCPOutputStream::ConnectionState::ConnectionState(
    TCPOutputStream *owner, EventManager *evm)
: owner(owner), evm(evm) {}

TCPOutputStream::SessionManager::SessionManager(EventManager *evm,
                                        TCPOutputStream *owner)
: TcpServer(evm),
  owner_(owner) {}

TcpSession *TCPOutputStream::SessionManager::AllocSession(Socket *socket) {
    return new OutputStreamSession(this, socket, owner_);
}

TCPOutputStream::Connecting::Connecting(my_context ctx)
: my_base(ctx) {
    TCPOutputStream *owner = context<ConnectionState>().owner;
    owner->session_ =
        static_cast<OutputStreamSession *>(owner->session_mgr_.CreateSession());
    owner->Connect();
}

TCPOutputStream::Reconnect::Reconnect(my_context ctx)
: my_base(ctx),
  reconnect_timer(
     TimerManager::CreateTimer(
        *context<ConnectionState>().evm->io_service(),
        "TCPOutputStreamReconnectTimer")) {
    LOG(DEBUG, "TCPOutputStream: RECONNECT SCHEDULE TIMER");
    reconnect_timer->Start(
        context<ConnectionState>().owner->reconnect_timeout_ms_,
        boost::bind(&Reconnect::TimeoutCallback, this));
}

TCPOutputStream::Reconnect::~Reconnect() {
   TimerManager::DeleteTimer(reconnect_timer);
}

bool TCPOutputStream::Reconnect::TimeoutCallback() {
   LOG(DEBUG, "TCPOutputStream: RECONNECT TIMEOUT");
   reconnect_timer->Cancel();
   context<ConnectionState>().owner->sm_evq_.Enqueue(SM_RECONNECT_TIMEOUT);
   return true;
}

TCPOutputStream::Connected::Connected(my_context ctx)
: my_base(ctx) {
    LOG(DEBUG, "TCPOutputStream: CONNECTED");
    ConnectionState &state_machine = context<ConnectionState>();
    state_machine.owner->output_workqueue_.MayBeStartRunner();
}

sc::result TCPOutputStream::Connected::react(const EvClosed &) {
    ConnectionState &state_machine = context<ConnectionState>();
    state_machine.owner->session_mgr_.DeleteSession(
        state_machine.owner->session_);
    return transit<Reconnect>();
}

sc::result TCPOutputStream::Connected::react(const EvReconfiguration &) {
    ConnectionState &state_machine = context<ConnectionState>();
    state_machine.owner->session_->Close();
    // A CLOSE event from TcpSession will cause the state machine
    // to transit from Connected to Reconnect. See: TCPEvDequeue.
    return discard_event();
}

bool TCPOutputStream::SMEvDequeue(SMEvent ev) {
    switch (ev) {
         case SM_CONNECT_COMPLETE:
             state_machine_.process_event(EvConnectCompleted());
             LOG(DEBUG, "TCPOutputStream: CONNECT_COMPLETE event (sm)\n");
             break;
         case SM_CONNECT_FAILED:
             state_machine_.process_event(EvConnectFailed());
             LOG(DEBUG, "TCPOutputStream: CONNECT_FAILED event (sm)\n");
             break;
         case SM_CLOSE:
             state_machine_.process_event(EvClosed());
             LOG(DEBUG, "TCPOutputStream: CLOSE event (sm)\n");
             break;
         case SM_RECONNECT_TIMEOUT:
             state_machine_.process_event(EvReconnectTimeout());
             LOG(DEBUG, "TCPOutputStream: RECONNECT event (sm)\n");
             break;
         case SM_RECONFIGURE:
             state_machine_.process_event(EvReconfiguration());
             LOG(DEBUG, "TCPOutputStream: RECONFIGURE event (sm)\n");
         default:
             break;
    }
    return true;
}
