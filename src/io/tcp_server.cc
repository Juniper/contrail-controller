/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "io/tcp_server.h"

#include <errno.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>

#include "base/logging.h"
#include "io/event_manager.h"
#include "io/tcp_session.h"
#include "io/io_log.h"
#include "io/io_utils.h"

using boost::asio::placeholders::error;
using boost::asio::socket_base;
using boost::bind;
using boost::system::error_code;

using boost::asio::ip::tcp;
using boost::asio::socket_base;
using std::ostringstream;
using std::string;

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

bool TcpServer::Initialize(unsigned short port) {
    acceptor_.reset(new tcp::acceptor(*evm_->io_service()));
    if (!acceptor_) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA, "Cannot create acceptor");
        return false;
    }

    tcp::endpoint localaddr(tcp::v4(), port);
    error_code ec;
    acceptor_->open(localaddr.protocol(), ec);
    if (ec) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA, "TCP open: " << ec.message());
        ResetAcceptor();
        return false;
    }

    acceptor_->set_option(socket_base::reuse_address(true), ec);
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

    acceptor_->listen(socket_base::max_connections, ec);
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
    error_code ec;

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
    TcpSession *session = AllocSession(false);
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
    set_accept_socket();
    acceptor_->async_accept(*accept_socket(),
        bind(&TcpServer::AcceptHandlerInternal, this,
            TcpServerPtr(this), error));
}

int TcpServer::GetPort() const {
    tbb::mutex::scoped_lock lock(mutex_);
    if (acceptor_.get() == NULL) {
        return -1;
    }
    error_code ec;
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
    error_code error;
    if (accept_socket()->available(error) > 0) {
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
    error_code ec;
    Endpoint local = acceptor_->local_endpoint(ec);
    if (ec) {
        return Endpoint();
    }
    return local;
}

TcpSession *TcpServer::AllocSession(bool server_session) {
    TcpSession *session;
    if (server_session) {
        session = AllocSession(so_accept_.get());

        // if session allocate succeeds release ownership to so_accept.
        if (session != NULL) {
            so_accept_.release();
        }
    } else {
        Socket *socket = new Socket(*evm_->io_service());
        session = AllocSession(socket);
    }

    return session;
}

TcpServer::Socket *TcpServer::accept_socket() const {
    return so_accept_.get();
}

void TcpServer::set_accept_socket() {
    so_accept_.reset(new Socket(*evm_->io_service()));
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
        const error_code& error) {
    tcp::endpoint remote;
    error_code ec;
    TcpSessionPtr session;
    bool need_close = false;

    if (error) {
        goto done;
    }

    remote = accept_socket()->remote_endpoint(ec);
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
        accept_socket()->close(ec);
        goto done;
    }

    session.reset(AllocSession(true));
    if (session == NULL) {
        TCP_SERVER_LOG_DEBUG(this, TCP_DIR_IN, "Session not created");
        goto done;
    }

    ec = session->SetSocketOptions();
    if (ec) {
        TCP_SESSION_LOG_ERROR(session, TCP_DIR_IN,
                              "Accept: Non-blocking error: " << ec.message());
        need_close = true;
        goto done;
    }

    session->SessionEstablished(remote, TcpSession::PASSIVE);
    AcceptHandlerComplete(session);

done:
    if (need_close) {
        session->CloseInternal(ec, false, false);
    }
    AsyncAccept();
}

void TcpServer::AcceptHandlerComplete(TcpSessionPtr session) {
    tcp::endpoint remote = session->remote_endpoint();
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
            error_code ec;
            session->CloseInternal(ec, false, false);
            return;
        }
    }

    session->Accepted();
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
                               const error_code &error) {
    if (error) {
        TCP_SERVER_LOG_UT_DEBUG(server, TCP_DIR_OUT,
                                "Connect failure: " << error.message());
        session->ConnectFailed();
        return;
    }

    ConnectHandlerComplete(session);
}

void TcpServer::ConnectHandlerComplete(TcpSessionPtr session) {
    error_code ec;
    Endpoint remote = session->socket()->remote_endpoint(ec);
    if (ec) {
        TCP_SERVER_LOG_INFO(this, TCP_DIR_OUT,
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
    }
}

void TcpServer::Connect(TcpSession *session, Endpoint remote) {
    assert(session->refcount_);
    Socket *socket = session->socket();
    socket->async_connect(remote,
        bind(&TcpServer::ConnectHandler, this, TcpServerPtr(this),
                    TcpSessionPtr(session), error));
}

int TcpServer::SetMd5SocketOption(int fd, uint32_t peer_ip,
                                  const string &md5_password) {
    assert(md5_password.size() <= TCP_MD5SIG_MAXKEYLEN);
    if (!peer_ip) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA, "Invalid peer IP");
        return 0;
    }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(peer_ip);

    struct tcp_md5sig md5sig;
    memset(&md5sig, 0, sizeof (md5sig));

    memcpy(md5sig.tcpm_key, md5_password.c_str(), md5_password.size());
    md5sig.tcpm_keylen = md5_password.size();
    memcpy(&md5sig.tcpm_addr, &local_addr, sizeof(local_addr));
    int retval = setsockopt(fd, IPPROTO_TCP, TCP_MD5SIG, &md5sig,
                            sizeof(md5sig));
    if (retval < 0) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA,
            "Failure in setting md5 key on the socket " +
            integerToString(fd) + " for peer " + integerToString(peer_ip) +
            " with errno " + strerror(errno));
    } else {
        TCP_SERVER_LOG_DEBUG(this, TCP_DIR_NA,
            "Success in setting md5 key on the socket " +
            integerToString(fd) + " for peer " + integerToString(peer_ip));
    }
    return retval;
}

int TcpServer::SetListenSocketMd5Option(uint32_t peer_ip,
                                        const string &md5_password) {
    int retval = 0;
    if (acceptor_) {
        retval = SetMd5SocketOption(acceptor_->native_handle(), peer_ip,
                                    md5_password);
    }
    return retval;
}

int TcpServer::SetDscpSocketOption(int fd, uint8_t value) {
    int retval = setsockopt(fd, IPPROTO_IP, IP_TOS, &value, sizeof(value));
    if (retval < 0) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA,
            "Failure in setting DSCP value on the socket " +
            integerToString(fd) + " for value " + integerToString(value) +
            " with errno " + strerror(errno));
    }
    return retval;
}

uint8_t TcpServer::GetDscpValue(int fd) const {
    uint8_t dscp = 0;
    unsigned int optlen = sizeof(dscp);
    int retval = getsockopt(fd, IPPROTO_IP, IP_TOS, &dscp, &optlen);
    if (retval < 0) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA,
            "Failure in getting DSCP value on the socket " +
            integerToString(fd) + " with errno " + strerror(errno));
    }
    return dscp;
}

void TcpServer::GetRxSocketStats(SocketIOStats *socket_stats) const {
    stats_.GetRxStats(socket_stats);
}

void TcpServer::GetTxSocketStats(SocketIOStats *socket_stats) const {
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
