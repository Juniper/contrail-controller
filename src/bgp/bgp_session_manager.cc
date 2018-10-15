/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_session_manager.h"

#include "base/bgp_as_service_utils.h"
#include "base/task_annotations.h"
#include "base/address_util.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"

using namespace boost::asio::ip;

static const std::string kDefaultBgpSessionIp = "0.0.0.0";

BgpSessionManager::BgpSessionManager(EventManager *evm, BgpServer *server)
    : TcpServer(evm),
      server_(server),
      session_queue_(
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0,
          boost::bind(&BgpSessionManager::ProcessSession, this, _1)),
      write_ready_queue_(
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0,
          boost::bind(&BgpSessionManager::ProcessWriteReady, this, _1)) {

      boost::system::error_code ec;
      session_ip_ = address::from_string(kDefaultBgpSessionIp, ec);
      if(ec.value() != 0){
        boost::asio::io_service io_service;
        std::string address_string = GetHostIp(&io_service, kDefaultBgpSessionIp);
        session_ip_ = boost::asio::ip::address::from_string(address_string, ec);
      }
}

BgpSessionManager::~BgpSessionManager() {
}

//
// Start listening at the given port.
//
bool BgpSessionManager::Initialize(unsigned short port) {
    // Changes to already bound bgp listen port number is not supported.
    Endpoint local_endpoint = LocalEndpoint();
    if (local_endpoint != Endpoint()) {
        Endpoint new_endpoint(session_ip_, port);
        if (local_endpoint != new_endpoint) {
            BGP_LOG_WARNING_STR(BgpSocket, BGP_LOG_FLAG_ALL,
                "Cannot change already bound " << local_endpoint <<
                " to " << new_endpoint);
            return false;
        }
        return true;
    }

    LOG(DEBUG, "Starting Bgp Server at " << session_ip_ << ":" << port);
    bool r = TcpServer::Initialize(port, session_ip_);
    if (!r) {
        BGP_LOG_WARNING_STR(BgpSocket, BGP_LOG_FLAG_ALL,
                            "Cannot bind to bgp/tcp server ip:port " <<
                            session_ip_ << ":" << port);
    }
    return r;
}

//
// Start listening at the given ip:port.
//
bool BgpSessionManager::Initialize(unsigned short port, const IpAddress& ip) {
    session_ip_ = ip;
    return true;
}

//
// Called from BgpServer::DeleteActor's Shutdown method.
// Shutdown the TcpServer.
// Register an exit callback to the WorkQueues so that we can ask BgpServer
// to retry deletion when a WorkQueue becomes empty.
//
void BgpSessionManager::Shutdown() {
    CHECK_CONCURRENCY("bgp::Config");
    TcpServer::Shutdown();
    session_queue_.SetExitCallback(
        boost::bind(&BgpSessionManager::WorkQueueExitCallback, this, _1));
    write_ready_queue_.SetExitCallback(
        boost::bind(&BgpSessionManager::WorkQueueExitCallback, this, _1));
}

//
// Called when the BgpServer is being destroyed.
//
// The WorkQueues need to be shutdown as the last step to ensure that all
// entries get deleted. Note that there's no need to call DeleteSession on
// the sessions in the WorkQueues since ClearSessions does the same thing.
//
void BgpSessionManager::Terminate() {
    CHECK_CONCURRENCY("bgp::Config");
    server_ = NULL;
    ClearSessions();
    session_queue_.Shutdown();
    write_ready_queue_.Shutdown();
}

//
// Return true if all WorkQueues are empty.
//
bool BgpSessionManager::MayDelete() const {
    if (!session_queue_.IsQueueEmpty())
        return false;
    if (!write_ready_queue_.IsQueueEmpty())
        return false;
    return true;
}

//
// Add a BgpSession to the write ready WorkQueue.
// Take a reference to make sure that BgpSession doesn't get deleted before
// it's processed.
//
void BgpSessionManager::EnqueueWriteReady(BgpSession *session) {
    if (!server_ || server_->IsDeleted())
        return;
    write_ready_queue_.Enqueue(TcpSessionPtr(session));
}

//
// Handler for BgpSessions that are dequeued from the write ready WorkQueue.
//
// The BgpServer does not get destroyed if the WorkQueue is non-empty.
//
bool BgpSessionManager::ProcessWriteReady(TcpSessionPtr tcp_session) {
    BgpSession *session = static_cast<BgpSession *>(tcp_session.get());
    session->ProcessWriteReady();
    return true;
}

//
// Search for a matching BgpPeer.
// First look for a matching address in the master instance.
// Then look for a matching port in the EndpointPeerList in BgpServer.
//
BgpPeer *BgpSessionManager::FindPeer(Endpoint remote) {
    BgpPeer *peer = NULL;
    const RoutingInstance *instance =
        server_->routing_instance_mgr()->GetDefaultRoutingInstance();
    if (instance && !instance->deleted()) {
        peer = instance->peer_manager()->PeerLookup(remote);
    }
    if (!peer) {
        uint16_t port = BGPaaSUtils::DecodeBgpaasServicePort(remote.port(),
            server_->global_config()->bgpaas_port_start(),
            server_->global_config()->bgpaas_port_end()).first;
        peer = server_->FindPeer(TcpSession::Endpoint(Ip4Address(), port));
    }
    return peer;
}

//
// Create an active BgpSession.
//
TcpSession *BgpSessionManager::CreateSession() {
    TcpSession *session = TcpServer::CreateSession();
    Socket *socket = session->socket();

    boost::system::error_code ec;
    socket->open(boost::asio::ip::tcp::v4(), ec);
    if (ec || (ec = session->SetSocketOptions()) || socket_open_failure()) {
        BGP_LOG_WARNING_STR(BgpSocket, BGP_LOG_FLAG_ALL,
            "Failed to open bgp socket, error: " << ec.message());
        DeleteSession(session);
        return NULL;
    }

    if (session_ip_ != address::from_string(kDefaultBgpSessionIp, ec))  {
        tcp::endpoint localaddr(session_ip_, 0);
        socket->bind(localaddr, ec);
        if (ec) {
            BGP_LOG_WARNING_STR(BgpSocket, BGP_LOG_FLAG_ALL,
                "Failed to bind bgp socket to:" << session_ip_ <<
                ", error: " << ec.message());
            DeleteSession(session);
            return NULL;
        }
    }
    return session;
}

//
// Allocate a new BgpSession.
// Called via CreateSession or when the TcpServer accepts a passive session.
//
TcpSession *BgpSessionManager::AllocSession(Socket *socket) {
    TcpSession *session = new BgpSession(this, socket);
    return session;
}

//
// Accept incoming BgpSession and add to session WorkQueue for processing.
// This ensures that we don't try to access the BgpServer data structures
// from the IO thread while they are being modified from bgp::Config task.
//
// Stop accepting sessions after delete of the BgpServer gets triggered.
// Note that the BgpServer, and hence the BgpSessionManager will not get
// destroyed if the WorkQueue is non-empty.
//
bool BgpSessionManager::AcceptSession(TcpSession *tcp_session) {
    if (!server_ || server_->IsDeleted())
        return false;
    BgpSession *session = dynamic_cast<BgpSession *>(tcp_session);
    session->set_read_on_connect(false);
    session_queue_.Enqueue(session);
    return true;
}

//
// Handler for BgpSessions that are dequeued from the session WorkQueue.
//
// The BgpServer does not get destroyed if the WorkQueue is non-empty.
//
bool BgpSessionManager::ProcessSession(BgpSession *session) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpPeer *peer = FindPeer(session->remote_endpoint());

    // Ignore if server is being deleted.
    if (!server_ || server_->IsDeleted()) {
        session->SendNotification(BgpProto::Notification::Cease,
                                  BgpProto::Notification::PeerDeconfigured);
        DeleteSession(session);
        return true;
    }

    // Ignore if this server is being held administratively down.
    if (server_->admin_down()) {
        session->SendNotification(BgpProto::Notification::Cease,
                                  BgpProto::Notification::AdminShutdown);
        DeleteSession(session);
        return true;
    }

    // Ignore if this peer is not configured or is being deleted.
    if (peer == NULL || peer->deleter()->IsDeleted()) {
        session->SendNotification(BgpProto::Notification::Cease,
                                  BgpProto::Notification::PeerDeconfigured);
        BGP_LOG_WARNING_STR(BgpConfig, BGP_LOG_FLAG_TRACE,
                            "Remote end-point not found");
        DeleteSession(session);
        return true;
    }

    // Ignore if this peer is being held administratively down.
    if (peer->IsAdminDown()) {
        session->SendNotification(BgpProto::Notification::Cease,
                                  BgpProto::Notification::AdminShutdown);
        DeleteSession(session);
        return true;
    }

    // Ignore if the peer's prefix limit idle timer is running.
    if (peer->PrefixLimitIdleTimerRunning()) {
        session->SendNotification(BgpProto::Notification::Cease,
                                  BgpProto::Notification::MaxPrefixes);
        DeleteSession(session);
        return true;
    }

    // Ignore if this peer is being closed.
    if (peer->IsCloseInProgress()) {
        session->SendNotification(BgpProto::Notification::Cease,
                                  BgpProto::Notification::ConnectionRejected);
        DeleteSession(session);
        return true;
    }

    if (!peer->ProcessSession()) {
        session->SendNotification(BgpProto::Notification::Cease,
                                  BgpProto::Notification::ConnectionRejected);
        DeleteSession(session);
        return true;
    }

    peer->AcceptSession(session);
    return true;
}

//
// Exit callback for the session and write ready WorkQueues.
//
void BgpSessionManager::WorkQueueExitCallback(bool done) {
    server_->RetryDelete();
}

size_t BgpSessionManager::GetSessionQueueSize() const {
    return session_queue_.Length();
}

void BgpSessionManager::SetSessionQueueDisable(bool disabled) {
    session_queue_.set_disable(disabled);
}
