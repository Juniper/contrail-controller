/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "tcp_server.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>

#include "base/logging.h"
#include "io/event_manager.h"
#include "io/tcp_session.h"
#include "io/io_log.h"

using namespace boost::asio::ip;
using namespace std;

TcpServer::TcpServer(EventManager *evm)
    : evm_(evm), socket_open_failure_(false) {
    refcount_ = 0;
    TcpServerManager::AddServer(this);
}

// TcpServer delete procedure:
// 1. Shutdown() to stop accepting incoming sessions.
// 2. Close and terminate current sessions. ASIO callbacks maybe in-flight.
// 3. Optionally: WaitForEmpty().
// 4. Destroy TcpServer.
TcpServer::~TcpServer() {
    assert(acceptor_ == NULL);
    assert(session_ref_.empty());
    assert(session_map_.empty());
}

void TcpServer::SetName(Endpoint local_endpoint) {
    ostringstream out;
    out << local_endpoint;
    name_ = out.str();
}

void TcpServer::ResetAcceptor() {
    acceptor_.reset();
    name_ = "";
}

bool TcpServer::Initialize(short port) {
    acceptor_.reset(new tcp::acceptor(*evm_->io_service()));
    if (!acceptor_) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA, "Cannot create acceptor");
        return false;
    }

    tcp::endpoint localaddr(tcp::v4(), port);
    boost::system::error_code ec;
    acceptor_->open(localaddr.protocol(), ec);
    if (ec) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA, "TCP open: " << ec.message());
        ResetAcceptor();
        return false;
    }

    acceptor_->set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA, "TCP reuse_address: "
                                                   << ec.message());
        ResetAcceptor();
        return false;
    }

    acceptor_->bind(localaddr, ec);
    if (ec) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA, "TCP bind(" << port << "): "
                                               << ec.message());
        ResetAcceptor();
        return false;
    }

    tcp::endpoint local_endpoint = acceptor_->local_endpoint(ec);
    if (ec) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA,
                             "Cannot retrieve acceptor local-endpont");
        ResetAcceptor();
        return false;
    }

    //
    // Server name can be set after local-endpoint information is available.
    //
    SetName(local_endpoint);

    acceptor_->listen(boost::asio::socket_base::max_connections, ec);
    if (ec) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA, "TCP listen(" << port << "): "
                                                   << ec.message());
        ResetAcceptor();
        return false;
    }

    TCP_SERVER_LOG_DEBUG(this, TCP_DIR_NA, "Initialization complete");
    AsyncAccept();

    return true;
}

void TcpServer::Shutdown() {
    tbb::mutex::scoped_lock lock(mutex_);
    boost::system::error_code ec;

    if (acceptor_) {
        acceptor_->close(ec);
        if (ec) {
            TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA, "Error during shutdown: "
                                                       << ec.message());
        }
        ResetAcceptor();
    }
}

// Close and remove references from all sessions. The application code must
// make sure it no longer holds any references to these sessions.
void TcpServer::ClearSessions() {
    tbb::mutex::scoped_lock lock(mutex_);
    SessionSet refs;
    session_map_.clear();
    refs.swap(session_ref_);
    lock.release();

    for (SessionSet::iterator iter = refs.begin(), next = iter;
         iter != refs.end(); iter = next) {
        ++next;
        TcpSession *session = iter->get();
        session->Close();
    }
    refs.clear();
    cond_var_.notify_all();
}

TcpSession *TcpServer::CreateSession() {
    Socket *socket = new Socket(*evm_->io_service());
    TcpSession *session = AllocSession(socket);
    {
        tbb::mutex::scoped_lock lock(mutex_);
        session_ref_.insert(TcpSessionPtr(session));
    }
    return session;
}

void TcpServer::DeleteSession(TcpSession *session) {
    // The caller will typically close the socket before deleting the
    // session.
    session->Close();
    {
        tbb::mutex::scoped_lock lock(mutex_);
        assert(session->refcount_);
        session_ref_.erase(TcpSessionPtr(session));
        if (session_ref_.empty()) {
            cond_var_.notify_all();
        }
    }
}

//
// Insert into SessionMap.
// Assumes that caller has the mutex.
//
void TcpServer::InsertSessionToMap(Endpoint remote, TcpSession *session) {
    session_map_.insert(make_pair(remote, session));
}

//
// Remove from SessionMap.
// Assumes that caller has the mutex.
// Return true if the session is found.
//
bool TcpServer::RemoveSessionFromMap(Endpoint remote, TcpSession *session) {
    for (SessionMap::iterator iter = session_map_.find(remote);
         iter != session_map_.end() && iter->first == remote; ++iter) {
        if (iter->second == session) {
            session_map_.erase(iter);
            return true;
        }
    }
    return false;
}

void TcpServer::OnSessionClose(TcpSession *session) {
    tbb::mutex::scoped_lock lock(mutex_);

    // CloseSessions closes and removes all the sessions from the map.
    if (session_map_.empty()) {
        return;
    }

    bool found = RemoveSessionFromMap(session->remote_endpoint(), session);
    assert(found);
}

// This method ensures that the application code requested the session to be
// deleted (which may be a delayed action). It does not guarantee that the
// session object has actually been freed yet as ASIO callbacks can be in
// progress.
void TcpServer::WaitForEmpty() {
    tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
    while (!session_ref_.empty()) {
        cond_var_.wait(lock);
    }
}

void TcpServer::AsyncAccept() {
    tbb::mutex::scoped_lock lock(mutex_);
    if (acceptor_ == NULL) {
        return;
    }
    so_accept_.reset(new Socket(*evm_->io_service()));
    acceptor_->async_accept(*so_accept_.get(),
        boost::bind(&TcpServer::AcceptHandlerInternal, this,
            TcpServerPtr(this), boost::asio::placeholders::error));
}

int TcpServer::GetPort() const {
    tbb::mutex::scoped_lock lock(mutex_);
    if (acceptor_.get() == NULL) {
        return -1;
    }
    boost::system::error_code ec;
    tcp::endpoint ep = acceptor_->local_endpoint(ec);
    if (ec) {
        return -1;
    }
    return ep.port();
}

bool TcpServer::HasSessions() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return !session_map_.empty();
}

bool TcpServer::HasSessionReadAvailable() const {
    tbb::mutex::scoped_lock lock(mutex_);
    boost::system::error_code error;
    if (so_accept_->available(error) > 0) {
        return  true;
    }
    for (SessionMap::const_iterator iter = session_map_.begin(); 
         iter != session_map_.end();
         ++iter) {
        if (iter->second->socket()->available(error) > 0) {
            return true;
        }
    }
    return false;
}

TcpServer::Endpoint TcpServer::LocalEndpoint() const {
    tbb::mutex::scoped_lock lock(mutex_);
    if (acceptor_.get() == NULL) {
        return Endpoint();
    }
    boost::system::error_code ec;
    Endpoint local = acceptor_->local_endpoint(ec);
    if (ec) {
        return Endpoint();
    }
    return local;
}

bool TcpServer::AcceptSession(TcpSession *session) {
    return true;
}

//
// concurrency: called from the event_manager thread.
//
// accept() tcp connections. Once done, must register with boost again
// via AsyncAccept() in order to process future accept calls
//
void TcpServer::AcceptHandlerInternal(TcpServerPtr server,
        const boost::system::error_code& error) {
    tcp::endpoint remote;
    boost::system::error_code ec;
    TcpSessionPtr session;
    auto_ptr<Socket> socket;
    bool need_close = false;

    if (error) {
        goto done;
    }

    socket.reset(so_accept_.release());
    remote = socket->remote_endpoint(ec);
    if (ec) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_IN,
                             "Accept: No remote endpoint: " << ec.message());
        goto done;
    }

    if (acceptor_ == NULL) {
        TCP_SESSION_LOG_DEBUG(session, TCP_DIR_IN,
                              "Session accepted after server shutdown: "
                                  << remote.address().to_string()
                                  << ":" << remote.port());
        socket->close(ec);
        goto done;
    }

    session.reset(AllocSession(socket.get()));
    if (session == NULL) {
        TCP_SERVER_LOG_DEBUG(this, TCP_DIR_IN, "Session not created");
        goto done;
    }
    socket.release();

    ec = session->SetSocketOptions();
    if (ec) {
        TCP_SESSION_LOG_ERROR(session, TCP_DIR_IN,
                              "Accept: Non-blocking error: " << ec.message());
        need_close = true;
        goto done;
    }

    session->SessionEstablished(remote, TcpSession::PASSIVE);
    {
        tbb::mutex::scoped_lock lock(mutex_);
        if (AcceptSession(session.get())) {
            TCP_SESSION_LOG_UT_DEBUG(session, TCP_DIR_IN,
                                     "Accepted session from "
                                         << remote.address().to_string()
                                         << ":" << remote.port());
            session_ref_.insert(session);
            InsertSessionToMap(remote, session.get());
        } else {
            TCP_SESSION_LOG_UT_DEBUG(session, TCP_DIR_IN,
                                     "Rejected session from "
                                         << remote.address().to_string()
                                         << ":" << remote.port());
            need_close = true;
            goto done;
        }
    }

    if (session->read_on_connect_) {
        session->AsyncReadStart();
    }

done:
    if (need_close) {
        session->CloseInternal(false, false);
    }
    AsyncAccept();
}

TcpSession *TcpServer::GetSession(Endpoint remote) {
    tbb::mutex::scoped_lock lock(mutex_);
    SessionMap::const_iterator iter = session_map_.find(remote);
    if (iter != session_map_.end()) {
        return iter->second;
    }
    return NULL;
}

void TcpServer::ConnectHandler(TcpServerPtr server, TcpSessionPtr session,
                               const boost::system::error_code &error) {
    if (error) {
        TCP_SERVER_LOG_UT_DEBUG(server, TCP_DIR_OUT,
                                "Connect failure: " << error.message());
        session->ConnectFailed();
        return;
    }

    boost::system::error_code ec;
    Endpoint remote = session->socket()->remote_endpoint(ec);
    if (ec) {
        TCP_SERVER_LOG_INFO(server, TCP_DIR_OUT,
                            "Connect getsockaddr: " << ec.message());
        session->ConnectFailed();
        return;
    }

    {
        tbb::mutex::scoped_lock lock(mutex_);
        InsertSessionToMap(remote, session.get());
    }

    // Connected verifies whether the session has been closed or is still
    // active.
    if (!session->Connected(remote)) {
        tbb::mutex::scoped_lock lock(mutex_);
        RemoveSessionFromMap(remote, session.get());
        return;
    }
}

void TcpServer::Connect(TcpSession *session, Endpoint remote) {
    assert(session->refcount_);
    Socket *socket = session->socket();
    socket->async_connect(remote,
        boost::bind(&TcpServer::ConnectHandler, this, TcpServerPtr(this),
                    TcpSessionPtr(session), boost::asio::placeholders::error));
}

void TcpServer::SocketStats::GetRxStats(TcpServerSocketStats &socket_stats) const {
    socket_stats.calls = read_calls;
    socket_stats.bytes = read_bytes;
    if (read_calls) {
        socket_stats.average_bytes = read_bytes/read_calls;
    }
}

void TcpServer::GetRxSocketStats(TcpServerSocketStats &socket_stats) const {
    stats_.GetRxStats(socket_stats);
}

void TcpServer::SocketStats::GetTxStats(TcpServerSocketStats &socket_stats) const {
    socket_stats.calls = write_calls;
    socket_stats.bytes = write_bytes;
    if (write_calls) {
        socket_stats.average_bytes = write_bytes/write_calls;
    }
    socket_stats.blocked_count = write_blocked;
    socket_stats.blocked_duration = duration_usecs_to_string(
        write_blocked_duration_usecs);
    if (write_blocked) {
        socket_stats.average_blocked_duration =
                 duration_usecs_to_string(
                     write_blocked_duration_usecs/
                     write_blocked);
    }
}

void TcpServer::GetTxSocketStats(TcpServerSocketStats &socket_stats) const {
    stats_.GetTxStats(socket_stats);
}

//
// TcpServerManager class routines
//
ServerManager<TcpServer, TcpServerPtr> TcpServerManager::impl_;

void TcpServerManager::AddServer(TcpServer *server) {
    impl_.AddServer(server);
}

void TcpServerManager::DeleteServer(TcpServer *server) {
    impl_.DeleteServer(server);
}

size_t TcpServerManager::GetServerCount() {
    return impl_.GetServerCount();
}
