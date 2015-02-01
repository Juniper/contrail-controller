/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>

#include <oper/agent_sandesh.h>
#include <ovsdb_types.h>
#include <ovsdb_client_tcp.h>

#include <ovs_tor_agent/tor_agent_param.h>

using OVSDB::OvsdbClientSession;
using OVSDB::OvsdbClientTcp;
using OVSDB::OvsdbClientTcpSession;
using OVSDB::OvsdbClientTcpSessionReader;

OvsdbClientTcp::OvsdbClientTcp(Agent *agent, TorAgentParam *params,
        OvsPeerManager *manager) : TcpServer(agent->event_manager()),
    OvsdbClient(manager), agent_(agent), session_(NULL),
    server_ep_(IpAddress(params->tor_ip()), params->tor_port()),
    tsn_ip_(params->tsn_ip()) {
}

OvsdbClientTcp::~OvsdbClientTcp() {
}

void OvsdbClientTcp::RegisterClients() {
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

const boost::asio::ip::tcp::endpoint &OvsdbClientTcp::server_ep() const {
        return server_ep_;
}

OvsdbClientSession *OvsdbClientTcp::next_session(OvsdbClientSession *session) {
    return static_cast<OvsdbClientSession *>(
            static_cast<OvsdbClientTcpSession *>(session_));
}

void OvsdbClientTcp::AddSessionInfo(SandeshOvsdbClient &client){
    SandeshOvsdbClientSession session;
    std::vector<SandeshOvsdbClientSession> session_list;
    OvsdbClientTcpSession *tcp = static_cast<OvsdbClientTcpSession *>(session_);
    session.set_status(tcp->status());
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
                TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0)) {
    reader_ = new OvsdbClientTcpSessionReader(this, 
            boost::bind(&OvsdbClientTcpSession::RecvMsg, this, _1, _2));
    /*
     * Process the received messages in a KSync workqueue task context,
     * to assure only one thread is writting data to OVSDB client.
     */
    receive_queue_ = new WorkQueue<queue_msg>(
            TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0,
            boost::bind(&OvsdbClientTcpSession::ReceiveDequeue, this, _1));
    // Process session events in KSync workqueue task context,
    session_event_queue_ = new WorkQueue<OvsdbSessionEvent>(
            TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0,
            boost::bind(&OvsdbClientTcpSession::ProcessSessionEvent, this, _1));
}

OvsdbClientTcpSession::~OvsdbClientTcpSession() {
    delete reader_;
    receive_queue_->Shutdown();
    delete receive_queue_;
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
    queue_msg msg;
    msg.buf = (u_int8_t *)malloc(len);
    memcpy(msg.buf, buf, len);
    msg.len = len;
    receive_queue_->Enqueue(msg);
    return true;
}

bool OvsdbClientTcpSession::ReceiveDequeue(queue_msg msg) {
    MessageProcess(msg.buf, msg.len);
    free(msg.buf);
    return true;
}

KSyncObjectManager *OvsdbClientTcpSession::ksync_obj_manager() {
    OvsdbClientTcp *ovs_server = static_cast<OvsdbClientTcp *>(server());
    return ovs_server->ksync_obj_manager();
}

Ip4Address OvsdbClientTcpSession::tsn_ip() {
    OvsdbClientTcp *ovs_server = static_cast<OvsdbClientTcp *>(server());
    return ovs_server->tsn_ip();
}

bool OvsdbClientTcpSession::ProcessSessionEvent(OvsdbSessionEvent ovs_event) {
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
        /* TODO need to handle reconnects */
        OnClose();
        break;
    case TcpSession::CONNECT_COMPLETE:
        ec = SetSocketOptions();
        assert(ec.value() == 0);
        set_status("Established");
        OnEstablish();
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

