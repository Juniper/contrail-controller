/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_session_manager.h"

#include "base/task_annotations.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_session.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"

using namespace std;
using namespace boost::asio;

BgpSessionManager::BgpSessionManager(EventManager *evm, BgpServer *server)
    : TcpServer(evm),
      server_(server),
      session_queue_(TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0,
          boost::bind(&BgpSessionManager::ProcessSession, this, _1)) {
}

BgpSessionManager::~BgpSessionManager() {
}

bool BgpSessionManager::Initialize(short port) {
    TcpServer::Initialize(port);
    return true;
}

//
// Called when the BgpServer is being destroyed.
//
// The WorkQueue needs to be shutdown as the last step to ensure that all
// entries get deleted. Note that there's no need to call DeleteSession on
// the sessions in the WorkQueue since ClearSessions does the same thing.
//
void BgpSessionManager::Terminate() {
    server_ = NULL;
    ClearSessions();
    session_queue_.Shutdown();
}

//
// Search in every routing instance for a matching BgpPeer.
//
BgpPeer *BgpSessionManager::FindPeer(Endpoint remote) {
    for (RoutingInstanceMgr::RoutingInstanceIterator it =
         server_->routing_instance_mgr()->begin();
         it != server_->routing_instance_mgr()->end(); ++it) {
        BgpPeer *peer = it->peer_manager()->PeerLookup(remote);
        if (peer)
            return peer;
    }
    return NULL;
}

TcpSession *BgpSessionManager::CreateSession() {
    TcpSession *session = TcpServer::CreateSession();
    Socket *socket = session->socket();

    boost::system::error_code ec;
    socket->open(ip::tcp::v4(), ec);
    if (ec || (ec = session->SetSocketOptions()) || socket_open_failure()) {
        BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Failed to open bgp socket, error: " << ec.message());
        DeleteSession(session);
        return NULL;
    }
    return session;
}

TcpSession *BgpSessionManager::AllocSession(Socket *socket) {
    TcpSession *session = new BgpSession(this, socket);
    return session;
}

//
// Accept incoming BgpSession and add it to the WorkQueue for processing.
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
// Handler for BgpSessions that are dequeued from the WorkQueue.
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

    // Ignore if this peer is not configured or is being deleted.
    if (peer == NULL || peer->deleter()->IsDeleted()) {
        session->SendNotification(BgpProto::Notification::Cease,
                                  BgpProto::Notification::PeerDeconfigured);
        BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_WARN,
                    BGP_LOG_FLAG_TRACE, "Remote end-point not found");
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

    // Ignore if this peer is being closed.
    if (peer->IsCloseInProgress()) {
        session->SendNotification(BgpProto::Notification::Cease,
                                  BgpProto::Notification::ConnectionRejected);
        DeleteSession(session);
        return true;
    }

    peer->AcceptSession(session);
    return true;
}

size_t BgpSessionManager::GetQueueSize() const {
    return session_queue_.Length();
}

void BgpSessionManager::SetQueueDisable(bool disabled) {
    session_queue_.set_disable(disabled);
}
