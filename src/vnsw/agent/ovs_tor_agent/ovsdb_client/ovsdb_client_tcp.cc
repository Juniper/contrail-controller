/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>

#include <oper/agent_sandesh.h>
#include <ovsdb_types.h>
#include <ovsdb_client_tcp.h>
#include <ovsdb_client_connection_state.h>

using OVSDB::OvsdbClientSession;
using OVSDB::OvsdbClientTcp;
using OVSDB::OvsdbClientTcpSession;
using OVSDB::OvsdbClientTcpSessionReader;
using OVSDB::ConnectionStateTable;

OvsdbClientTcp::OvsdbClientTcp(Agent *agent, IpAddress tor_ip, int tor_port,
        IpAddress tsn_ip, int keepalive_interval, int ha_stale_route_interval,
        OvsPeerManager *manager) : TcpServer(agent->event_manager()),
    OvsdbClient(manager, keepalive_interval, ha_stale_route_interval),
    agent_(agent), session_(NULL), server_ep_(tor_ip, tor_port),
    tsn_ip_(tsn_ip.to_v4()), shutdown_(false) {
}

OvsdbClientTcp::~OvsdbClientTcp() {
}

void OvsdbClientTcp::RegisterClients() {
    RegisterConnectionTable(agent_);
    session_ = CreateSession();
    Connect(session_, server_ep_);
}

TcpSession *OvsdbClientTcp::AllocSession(Socket *socket) {
    TcpSession *session = new OvsdbClientTcpSession(agent_, peer_manager_,
                                                    this, socket);
    session->set_observer(boost::bind(&OvsdbClientTcp::OnSessionEvent,
                                      this, _1, _2));
    return session;
}

void OvsdbClientTcp::OnSessionEvent(TcpSession *session,
        TcpSession::Event event) {
    OvsdbClientTcpSession *tcp = static_cast<OvsdbClientTcpSession *>(session);
    tcp->EnqueueEvent(event);
}

const std::string OvsdbClientTcp::protocol() {
    return "TCP";
}

const std::string OvsdbClientTcp::server() {
    return server_ep_.address().to_string();
}

uint16_t OvsdbClientTcp::port() {
    return server_ep_.port();
}

Ip4Address OvsdbClientTcp::tsn_ip() {
    return tsn_ip_;
}

void OvsdbClientTcp::shutdown() {
    if (shutdown_)
        return;
    shutdown_ = true;
    OvsdbClientTcpSession *tcp =
                static_cast<OvsdbClientTcpSession *>(session_);
    tcp->TriggerClose();
}

const boost::asio::ip::tcp::endpoint &OvsdbClientTcp::server_ep() const {
        return server_ep_;
}

OvsdbClientSession *OvsdbClientTcp::FindSession(Ip4Address ip, uint16_t port) {
    // match both ip and port with available session
    // if port is not provided match only ip
    if (server_ep_.address().to_v4() == ip &&
        (port == 0 || server_ep_.port() == port)) {
        return static_cast<OvsdbClientSession *>(
                static_cast<OvsdbClientTcpSession *>(session_));
    }
    return NULL;
}

OvsdbClientSession *OvsdbClientTcp::NextSession(OvsdbClientSession *session) {
    if (session_ == NULL) {
        return NULL;
    }
    return static_cast<OvsdbClientSession *>(
            static_cast<OvsdbClientTcpSession *>(session_));
}

void OvsdbClientTcp::AddSessionInfo(SandeshOvsdbClient &client){
    SandeshOvsdbClientSession session;
    std::vector<SandeshOvsdbClientSession> session_list;
    if (session_ != NULL) {
        OvsdbClientTcpSession *tcp =
            static_cast<OvsdbClientTcpSession *>(session_);
        tcp->AddSessionInfo(session);
    }
    session_list.push_back(session);
    client.set_sessions(session_list);
}

OvsdbClientTcpSession::OvsdbClientTcpSession(Agent *agent,
        OvsPeerManager *manager, TcpServer *server, Socket *sock,
        bool async_ready) : OvsdbClientSession(agent, manager),
    TcpSession(server, sock, async_ready), status_("Init"),
    client_reconnect_timer_(TimerManager::CreateTimer(
                *(agent->event_manager())->io_service(),
                "OVSDB Client TCP reconnect Timer",
                agent->task_scheduler()->GetTaskId("Agent::KSync"), 0)) {

    reader_ = new OvsdbClientTcpSessionReader(this, 
            boost::bind(&OvsdbClientTcpSession::RecvMsg, this, _1, _2));

    // Process session events in KSync workqueue task context,
    session_event_queue_ = new WorkQueue<OvsdbSessionEvent>(
            agent->task_scheduler()->GetTaskId("Agent::KSync"), 0,
            boost::bind(&OvsdbClientTcpSession::ProcessSessionEvent, this, _1));
    session_event_queue_->set_name("OVSDB TCP session event queue");
}

OvsdbClientTcpSession::~OvsdbClientTcpSession() {
    delete reader_;
    session_event_queue_->Shutdown();
    delete session_event_queue_;
    TimerManager::DeleteTimer(client_reconnect_timer_);
}

void OvsdbClientTcpSession::OnRead(Buffer buffer) {
    reader_->OnRead(buffer);
}

void OvsdbClientTcpSession::SendMsg(u_int8_t *buf, std::size_t len) {
    OVSDB_PKT_TRACE(Trace, "Sending: " + std::string((char *)buf, len));
    Send(buf, len, NULL);
}

bool OvsdbClientTcpSession::RecvMsg(const u_int8_t *buf, std::size_t len) {
    OVSDB_PKT_TRACE(Trace, "Received: " + std::string((const char*)buf, len));
    MessageProcess(buf, len);
    return true;
}

int OvsdbClientTcpSession::keepalive_interval() {
    OvsdbClientTcp *ovs_server = static_cast<OvsdbClientTcp *>(server());
    return ovs_server->keepalive_interval();
}

const boost::system::error_code &
OvsdbClientTcpSession::ovsdb_close_reason() const {
    return close_reason();
}

ConnectionStateTable *OvsdbClientTcpSession::connection_table() {
    OvsdbClientTcp *ovs_server = static_cast<OvsdbClientTcp *>(server());
    return ovs_server->connection_table();
}

KSyncObjectManager *OvsdbClientTcpSession::ksync_obj_manager() {
    OvsdbClientTcp *ovs_server = static_cast<OvsdbClientTcp *>(server());
    return ovs_server->ksync_obj_manager();
}

Ip4Address OvsdbClientTcpSession::tsn_ip() {
    OvsdbClientTcp *ovs_server = static_cast<OvsdbClientTcp *>(server());
    return ovs_server->tsn_ip();
}

void OvsdbClientTcpSession::OnCleanup() {
    OvsdbClientTcp *ovs_server = static_cast<OvsdbClientTcp *>(server());
    ovs_server->DeleteSession(this);
}

void OvsdbClientTcpSession::TriggerClose() {
    // Close the session and return
    Close();

    // tcp session will not notify event for self closed session
    // generate explicit event
    OvsdbSessionEvent ovs_event;
    ovs_event.event = TcpSession::CLOSE;
    session_event_queue_->Enqueue(ovs_event);
}

Ip4Address OvsdbClientTcpSession::remote_ip() const {
    return remote_endpoint().address().to_v4();
}

uint16_t OvsdbClientTcpSession::remote_port() const {
    return remote_endpoint().port();
}

bool OvsdbClientTcpSession::ProcessSessionEvent(OvsdbSessionEvent ovs_event) {
    OvsdbClientTcp *ovs_server =
        static_cast<OvsdbClientTcp *>(server());
    boost::system::error_code ec;
    switch (ovs_event.event) {
    case TcpSession::CONNECT_FAILED:
        assert(client_reconnect_timer_->fired() == false);
        /* Failed to Connect, Try Again! */
        if (client_reconnect_timer_->Start(TcpReconnectWait,
                boost::bind(&OvsdbClientTcpSession::ReconnectTimerCb, this)) == false ) {
            assert(0);
        }
        set_status("Reconnecting");
        break;
    case TcpSession::CLOSE:
        {
            // Trigger close for the current session, to allocate
            // and start a new one.
            OnClose();
            if (ovs_server->shutdown_ == false) {
                ovs_server->session_ = ovs_server->CreateSession();
                ovs_server->Connect(ovs_server->session_,
                                    ovs_server->server_ep());
            } else {
                ovs_server->session_ = NULL;
            }
        }
        break;
    case TcpSession::CONNECT_COMPLETE:
        if (!ovs_server->pre_connect_complete_cb_.empty()) {
            ovs_server->pre_connect_complete_cb_(this);
        }
        if (!IsClosed()) {
            ec = SetSocketOptions();
            assert(ec.value() == 0);
            set_status("Established");
            OnEstablish();
            if (!ovs_server->connect_complete_cb_.empty()) {
                ovs_server->connect_complete_cb_(this);
            }
        } else {
            OVSDB_SESSION_TRACE(Trace, this, "Skipping connection complete on"
                                " closed session");
        }
        break;
    default:
        break;
    }
    return true;
}

void OvsdbClientTcpSession::EnqueueEvent(TcpSession::Event event) {
    OvsdbSessionEvent ovs_event;
    ovs_event.event = event;
    session_event_queue_->Enqueue(ovs_event);
}

bool OvsdbClientTcpSession::ReconnectTimerCb() {
    OvsdbClientTcp *ovs_server = static_cast<OvsdbClientTcp *>(server());
    ovs_server->Connect(this, ovs_server->server_ep());
    return false;
}

OvsdbClientTcpSessionReader::OvsdbClientTcpSessionReader(
    TcpSession *session, ReceiveCallback callback) : 
    TcpMessageReader(session, callback) {
}

OvsdbClientTcpSessionReader::~OvsdbClientTcpSessionReader() {
}

int OvsdbClientTcpSessionReader::MsgLength(Buffer buffer, int offset) {
    size_t size = TcpSession::BufferSize(buffer);
    int remain = size - offset;
    if (remain < GetHeaderLenSize()) {
        return -1;
    }

    return remain;
}

