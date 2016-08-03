/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "io/ssl_server.h"
#include "io/ssl_session.h"

#include "io/event_manager.h"
#include "io/io_utils.h"
#include "io/io_log.h"

SslServer::SslServer(EventManager *evm, boost::asio::ssl::context::method m,
                     bool ssl_enabled, bool ssl_handshake_delayed)
    : TcpServer(evm), context_(*evm->io_service(), m),
      ssl_enabled_(ssl_enabled), ssl_handshake_delayed_(ssl_handshake_delayed) {
    boost::system::error_code ec;
    // By default set verify mode to none, to be set by derived class later.
    context_.set_verify_mode(boost::asio::ssl::context::verify_none, ec);
    assert(ec.value() == 0);
    context_.set_options(boost::asio::ssl::context::default_workarounds, ec);
    assert(ec.value() == 0);
}

SslServer::~SslServer() {
}

boost::asio::ssl::context *SslServer::context() {
    return &context_;
}

TcpSession *SslServer::AllocSession(bool server_session) {
    SslSession *session;
    if (server_session) {
        session = AllocSession(so_ssl_accept_.get());

        // if session allocate succeeds release ownership to so_accept.
        if (session != NULL) {
            so_ssl_accept_.release();
        }
    } else {
        SslSocket *socket = new SslSocket(*event_manager()->io_service(),
                                          context_);
        session = AllocSession(socket);
    }

    return session;
}

void SslServer::AcceptHandlerComplete(TcpSessionPtr session) {
    SslSession *ssl = static_cast<SslSession*>(session.get());
    if (ssl->IsSslDisabled() || ssl->IsSslHandShakeDelayed()) {
        TcpServer::AcceptHandlerComplete(session);
    } else {
        // trigger ssl server handshake
        std::srand(std::time(0));
        ssl->ssl_handshake_in_progress_ = true;
        ssl->ssl_socket_->async_handshake
            (boost::asio::ssl::stream_base::server,
             boost::bind(&SslServer::AcceptHandShakeHandler,
                         TcpServerPtr(this), TcpSessionPtr(ssl),
                         boost::asio::placeholders::error));
    }
}

void SslServer::AcceptHandShakeHandler(TcpServerPtr server,
                                       TcpSessionPtr session,
                                       const boost::system::error_code& error) {
    SslServer *ssl_server = static_cast<SslServer *>(server.get());
    SslSession *ssl_session = static_cast<SslSession *>(session.get());
    ssl_session->ssl_handshake_in_progress_ = false;
    if (!error) {
        // on successful handshake continue with tcp server state machine.
        ssl_session->SetSslHandShakeSuccess();
        ssl_server->TcpServer::AcceptHandlerComplete(session);
    } else {
        // close session on failure
        ssl_session->SetSslHandShakeFailure();
        TCP_SESSION_LOG_ERROR(ssl_session, TCP_DIR_OUT,
                              "SSL Handshake failed due to error: "
                              << error.value() << " category: "
                              << error.category().name()
                              << " message: " << error.message());
        ssl_session->CloseInternal(error, false, false);
    }
}

void SslServer::ConnectHandlerComplete(TcpSessionPtr session) {
    SslSession *ssl = static_cast<SslSession*>(session.get());
    if (ssl->IsSslDisabled() || ssl->IsSslHandShakeDelayed()) {
        TcpServer::ConnectHandlerComplete(session);
    } else {
        // trigger ssl client handshake
        std::srand(std::time(0));
        ssl->ssl_handshake_in_progress_ = true;
        ssl->ssl_socket_->async_handshake
            (boost::asio::ssl::stream_base::client,
             boost::bind(&SslServer::ConnectHandShakeHandler,
                         TcpServerPtr(this), TcpSessionPtr(ssl),
                         boost::asio::placeholders::error));
    }
}

void SslServer::ConnectHandShakeHandler(TcpServerPtr server,
        TcpSessionPtr session, const boost::system::error_code& error) {
    SslServer *ssl_server = static_cast<SslServer *>(server.get());
    SslSession *ssl_session = static_cast<SslSession *>(session.get());
    ssl_session->ssl_handshake_in_progress_ = false;
    if (!error) {
        // on successful handshake continue with tcp server state machine.
        ssl_session->SetSslHandShakeSuccess();
        ssl_server->TcpServer::ConnectHandlerComplete(session);
    } else {
        // report connect failure and close the session
        ssl_session->SetSslHandShakeFailure();
        ssl_session->CloseInternal(error, false, false);
        TCP_SESSION_LOG_ERROR(ssl_session, TCP_DIR_OUT,
                              "SSL Handshake failed due to error: "
                              << error.value() << " category: "
                              << error.category().name()
                              << " message: " << error.message());
        ssl_session->ConnectFailed();
    }
}

TcpServer::Socket *SslServer::accept_socket() const {
    // return tcp socket
    return &(so_ssl_accept_->next_layer());
}

void SslServer::set_accept_socket() {
    so_ssl_accept_.reset(new SslSocket(*event_manager()->io_service(),
                                       context_));
}

