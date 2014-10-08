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
  tcp_evq_(task_id_, -1,                                       
          boost::bind(&TCPOutputStream::TCPEvDequeue, this, _1)), 
  session_mgr_(evm, this),
  session_(static_cast<OutputStreamSession *>(session_mgr_.CreateSession())),
  reconnect_timeout_ms_(500) {
      tcp_evq_.SetStartRunnerFunc(                           
          boost::bind(&TCPOutputStream::isReadyForTCPEvDequeue, this));
      state_machine_.initiate();
}

bool TCPOutputStream::Configure(StreamHandlerConfig &config) {
    std::string addr_str = config.get<std::string>("stream.dest_addr");
    unsigned short port = config.get<unsigned short>("stream.dest_port");
    endpoint_ = boost::asio::ip::tcp::endpoint(
        boost::asio::ip::address::from_string(addr_str) ,port);
    reconnect_timeout_ms_ =
        config.get<unsigned int>("stream.reconnect_timeout_ms");

    state_machine_.process_event(EvReconfiguration());
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
    owner_->tcp_evq_.Enqueue(event);
}

void TCPOutputStream::OutputStreamSession::WriteReady(
                const boost::system::error_code &ec) {
    // Dequeue one or more entries from WorkQueue, if it's not already
    // happening.
    if (!ec)
        owner_->output_workqueue_.MayBeStartRunner();
}

void TCPOutputStream::Connect() {
    session_mgr_.Connect(session_.get(), endpoint_);
}

bool TCPOutputStream::SendRaw(const u_int8_t *data, size_t len) {
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
    context<ConnectionState>().owner->Connect();
}

TCPOutputStream::Reconnect::Reconnect(my_context ctx)
: my_base(ctx),
  reconnect_timer(*context<ConnectionState>().evm->io_service(), 
    "TCPOutputStreamReconnectTimer",
    context<ConnectionState>().owner->task_id_,
    1) {
    LOG(DEBUG, "TCPOutputStream: RECONNECT SCHEDULE TIMER");
    reconnect_timer.Start(
        context<ConnectionState>().owner->reconnect_timeout_ms_,
        boost::bind(&Reconnect::TimeoutCallback, this));
}

bool TCPOutputStream::Reconnect::TimeoutCallback() {
   LOG(DEBUG, "TCPOutputStream: RECONNECT TIMEOUT");
   reconnect_timer.Cancel();
   post_event(EvReconnectTimeout());
   return true;
}

TCPOutputStream::Connected::Connected(my_context ctx)
: my_base(ctx) {
    LOG(DEBUG, "TCPOutputStream: CONNECTED");
    ConnectionState &state_machine = context<ConnectionState>();
    state_machine.owner->output_workqueue_.MayBeStartRunner();
}

sc::result TCPOutputStream::Connected::react(const EvReconfiguration &) {
    ConnectionState &state_machine = context<ConnectionState>();
    state_machine.owner->session_->Close();
    // A CLOSE event from TcpSession will cause the state machine
    // to transit from Connected to Reconnect. See: TCPEvDequeue.
    return discard_event();
}

bool TCPOutputStream::TCPEvDequeue(TcpSession::Event ev) {
    switch (ev) {
         case CONNECT_COMPLETE:
             state_machine_.process_event(EvConnectCompleted());
             LOG(DEBUG, "TCPOutputStream: CONNECT_COMPLETE event\n");
             break;
         case CONNECT_FAILED:
             state_machine_.process_event(EvConnectFailed());
             LOG(DEBUG, "TCPOutputStream: CONNECT_FAILED event");
             break;
         case CLOSE:
             state_machine_.process_event(EvClosed());
             LOG(DEBUG, "TCPOutputStream: CLOSE event");
             break;
         default:
             break;
    }
    return true;
}
