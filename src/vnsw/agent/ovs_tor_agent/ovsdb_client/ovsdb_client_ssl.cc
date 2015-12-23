/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <base/logging.h>

#include <oper/agent_sandesh.h>
#include <ovsdb_types.h>
#include <ovsdb_client_ssl.h>
#include <ovsdb_client_connection_state.h>

#include <ovs_tor_agent/tor_agent_param.h>

using OVSDB::OvsdbClientSession;
using OVSDB::OvsdbClientSsl;
using OVSDB::OvsdbClientSslSession;
using OVSDB::OvsdbClientTcpSessionReader;
using OVSDB::ConnectionStateTable;

OvsdbClientSsl::OvsdbClientSsl(Agent *agent, IpAddress tor_ip, int tor_port,
                               IpAddress tsn_ip, int keepalive_interval,
                               int ha_stale_route_interval,
                               const std::string &ssl_cert,
                               const std::string &ssl_privkey,
                               const std::string &ssl_cacert,
                               OvsPeerManager *manager) :
    SslServer(agent->event_manager(), boost::asio::ssl::context::tlsv1_server),
    OvsdbClient(manager, keepalive_interval, ha_stale_route_interval),
    agent_(agent), ssl_server_port_(tor_port), tsn_ip_(tsn_ip.to_v4()),
    shutdown_(false) {
    // Get SSL context from base class and update
    boost::asio::ssl::context *ctx = context();
    boost::system::error_code ec;

    // Verify peer to have a valid certificate
    ctx->set_verify_mode((boost::asio::ssl::verify_peer |
                          boost::asio::ssl::verify_fail_if_no_peer_cert), ec);
    assert(ec.value() == 0);

    ctx->use_certificate_chain_file(ssl_cert, ec);
    if (ec.value() != 0) {
        LOG(ERROR, "Error : " << ec.message() << ", while using cert file : "
            << ssl_cert);
        exit(EINVAL);
    }

    ctx->use_private_key_file(ssl_privkey, boost::asio::ssl::context::pem, ec);
    if (ec.value() != 0) {
        LOG(ERROR, "Error : " << ec.message() << ", while using privkey file : "
            << ssl_privkey);
        exit(EINVAL);
    }

    ctx->load_verify_file(ssl_cacert, ec);
    if (ec.value() != 0) {
        LOG(ERROR, "Error : " << ec.message() << ", while using cacert file : "
            << ssl_cacert);
        exit(EINVAL);
    }
}

OvsdbClientSsl::~OvsdbClientSsl() {
}

void OvsdbClientSsl::RegisterClients() {
    RegisterConnectionTable(agent_);
    Initialize(ssl_server_port_);
}

SslSession *OvsdbClientSsl::AllocSession(SslSocket *socket) {
    SslSession *session = new OvsdbClientSslSession(agent_, peer_manager_,
                                                    this, socket);
    session->set_observer(boost::bind(&OvsdbClientSsl::OnSessionEvent,
                                      this, _1, _2));
    return session;
}

void OvsdbClientSsl::OnSessionEvent(TcpSession *session,
        TcpSession::Event event) {
    OvsdbClientSslSession *ssl = static_cast<OvsdbClientSslSession *>(session);
    ssl->EnqueueEvent(event);
}

const std::string OvsdbClientSsl::protocol() {
    return "PSSL";
}

const std::string OvsdbClientSsl::server() {
    return LocalEndpoint().address().to_string();
}

uint16_t OvsdbClientSsl::port() {
    return LocalEndpoint().port();
}

Ip4Address OvsdbClientSsl::tsn_ip() {
    return tsn_ip_;
}

void OvsdbClientSsl::shutdown() {
    if (shutdown_)
        return;
    shutdown_ = true;
    OvsdbClientSslSession *ssl =
        static_cast<OvsdbClientSslSession *>(NextSession(NULL));
    while (ssl != NULL) {
        if (!ssl->IsClosed()) {
            ssl->TriggerClose();
        }
        ssl = static_cast<OvsdbClientSslSession *>(NextSession(ssl));
    }
}

OvsdbClientSession *OvsdbClientSsl::FindSession(Ip4Address ip, uint16_t port) {
    SessionKey key(ip, port);
    SessionMap::iterator it;
    if (port != 0) {
        it = session_map_.find(key);
    } else {
        it = session_map_.upper_bound(key);
    }
    if (it != session_map_.end() && it->first.first == ip) {
        return it->second;
    }
    return NULL;
}

OvsdbClientSession *OvsdbClientSsl::NextSession(OvsdbClientSession *session) {
    SessionMap::iterator it;
    if (session == NULL) {
        it = session_map_.begin();
    } else {
        OvsdbClientSslSession *ssl =
            static_cast<OvsdbClientSslSession *>(session);
        SessionKey key(ssl->remote_endpoint().address().to_v4(),
                       ssl->remote_endpoint().port());
        it = session_map_.upper_bound(key);
    }
    if (it != session_map_.end()) {
        return it->second;
    }
    return NULL;
}

void OvsdbClientSsl::AddSessionInfo(SandeshOvsdbClient &client) {
    SandeshOvsdbClientSession session;
    std::vector<SandeshOvsdbClientSession> session_list;
    OvsdbClientSslSession *ssl = static_cast<OvsdbClientSslSession *>(NextSession(NULL));
    while (ssl != NULL) {
        ssl->AddSessionInfo(session);
        session_list.push_back(session);
        ssl = static_cast<OvsdbClientSslSession *>(NextSession(ssl));
    }
    client.set_sessions(session_list);
}

// AcceptSession callback from SSLserver, to accept a session
bool OvsdbClientSsl::AcceptSession(TcpSession *session) {
    if (shutdown_) {
        // don't accept session while shutting down
        return false;
    }
    return true;
}

OvsdbClientSslSession::OvsdbClientSslSession(Agent *agent,
        OvsPeerManager *manager, OvsdbClientSsl *server, SslSocket *sock,
        bool async_ready) : OvsdbClientSession(agent, manager),
    SslSession(server, sock, async_ready), status_("Init") {

    reader_ = new OvsdbClientTcpSessionReader(this, 
            boost::bind(&OvsdbClientSslSession::RecvMsg, this, _1, _2));

    // Process session events in KSync workqueue task context,
    session_event_queue_ = new WorkQueue<OvsdbSessionEvent>(
            agent->task_scheduler()->GetTaskId("Agent::KSync"), 0,
            boost::bind(&OvsdbClientSslSession::ProcessSessionEvent, this, _1));
    session_event_queue_->set_name("OVSDB ssl session event queue");
}

OvsdbClientSslSession::~OvsdbClientSslSession() {
    delete reader_;
    session_event_queue_->Shutdown();
    delete session_event_queue_;
}

void OvsdbClientSslSession::OnRead(Buffer buffer) {
    reader_->OnRead(buffer);
}

void OvsdbClientSslSession::SendMsg(u_int8_t *buf, std::size_t len) {
    OVSDB_PKT_TRACE(Trace, "Sending: " + std::string((char *)buf, len));
    Send(buf, len, NULL);
}

bool OvsdbClientSslSession::RecvMsg(const u_int8_t *buf, std::size_t len) {
    OVSDB_PKT_TRACE(Trace, "Received: " + std::string((const char*)buf, len));
    MessageProcess(buf, len);
    return true;
}

int OvsdbClientSslSession::keepalive_interval() {
    OvsdbClientSsl *ovs_server = static_cast<OvsdbClientSsl *>(server());
    return ovs_server->keepalive_interval();
}

const boost::system::error_code &
OvsdbClientSslSession::ovsdb_close_reason() const {
    return close_reason();
}

ConnectionStateTable *OvsdbClientSslSession::connection_table() {
    OvsdbClientSsl *ovs_server = static_cast<OvsdbClientSsl *>(server());
    return ovs_server->connection_table();
}

KSyncObjectManager *OvsdbClientSslSession::ksync_obj_manager() {
    OvsdbClientSsl *ovs_server = static_cast<OvsdbClientSsl *>(server());
    return ovs_server->ksync_obj_manager();
}

Ip4Address OvsdbClientSslSession::tsn_ip() {
    OvsdbClientSsl *ovs_server = static_cast<OvsdbClientSsl *>(server());
    return ovs_server->tsn_ip();
}

void OvsdbClientSslSession::OnCleanup() {
    OvsdbClientSsl *ovs_server = static_cast<OvsdbClientSsl *>(server());
    ovs_server->DeleteSession(this);
}

void OvsdbClientSslSession::TriggerClose() {
    // Close the session and return
    Close();

    // SSL session will not notify event for self closed session
    // generate explicit event
    EnqueueEvent(TcpSession::CLOSE);
}

Ip4Address OvsdbClientSslSession::remote_ip() const {
    return remote_endpoint().address().to_v4();
}

uint16_t OvsdbClientSslSession::remote_port() const {
    return remote_endpoint().port();
}

bool OvsdbClientSslSession::ProcessSessionEvent(OvsdbSessionEvent ovs_event) {
    boost::system::error_code ec;
    switch (ovs_event.event) {
    case TcpSession::CLOSE:
        {
            OvsdbClientSsl *ovs_server =
                static_cast<OvsdbClientSsl *>(server());
            boost::asio::ip::tcp::endpoint ep = remote_endpoint();
            OvsdbClientSsl::SessionKey key(ep.address().to_v4(), ep.port());
            ovs_server->session_map_.erase(key);

            OnClose();
        }
        break;
    case TcpSession::ACCEPT:
        {
            OvsdbClientSsl *ovs_server =
                static_cast<OvsdbClientSsl *>(server());
            boost::asio::ip::tcp::endpoint ep = remote_endpoint();
            OvsdbClientSsl::SessionKey key(ep.address().to_v4(), ep.port());
            std::pair<OvsdbClientSsl::SessionMap::iterator, bool> ret =
                ovs_server->session_map_.insert
                (std::pair<OvsdbClientSsl::SessionKey,
                           OvsdbClientSslSession *>(key, this));
            // assert if entry already exists
            assert(ret.second == true);
        }
        set_status("Established");
        OnEstablish();
        break;
    default:
        assert(false);
        break;
    }
    return true;
}

void OvsdbClientSslSession::EnqueueEvent(TcpSession::Event event) {
    OvsdbSessionEvent ovs_event;
    ovs_event.event = event;
    session_event_queue_->Enqueue(ovs_event);
}

