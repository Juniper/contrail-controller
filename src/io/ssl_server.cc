/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "ssl_server.h"
#include "ssl_session.h"

#include "io/event_manager.h"

SslServer::SslServer(EventManager *evm, boost::asio::ssl::context::method m)
    : TcpServer(evm), context_(*evm->io_service(), m) {
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

TcpServer::Socket *SslServer::accept_socket() const {
    // return tcp socket
    return &(so_ssl_accept_->next_layer());
}

void SslServer::set_accept_socket() {
    so_ssl_accept_.reset(new SslSocket(*event_manager()->io_service(),
                                       context_));
}

