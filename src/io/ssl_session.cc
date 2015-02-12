/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "ssl_session.h"

using namespace boost::asio;

SslSession::SslSession(SslServer *server, SslSocket *socket,
                       bool async_read_ready) :
                       TcpSession(server, NULL, async_read_ready),
                       ssl_socket_(socket) {
}

SslSession::SslSession(SslServer *server, Socket *socket,
                       bool async_read_ready) :
                       TcpSession(server, socket, async_read_ready),
                       ssl_socket_(NULL) {
}



SslSession::~SslSession() {
}

//TcpSession::Socket *SslSession::socket() const {
SslSession::Socket *SslSession::socket() const {

    // return tcp socket
    if (ssl_socket_) {
        return &ssl_socket_->next_layer();
    } 

    return (TcpSession::socket());
}

bool SslSession::Connected(Endpoint remote) {
    if (IsClosed()) {
        return false;
    }

    if (ssl_socket_) {
        // trigger ssl client handshake
        std::srand(std::time(0));
        ssl_socket_->async_handshake
            (boost::asio::ssl::stream_base::client,
             boost::bind(&SslSession::ConnectHandShakeHandler, TcpSessionPtr(this),
                         remote, boost::asio::placeholders::error));
        return true;
    } else {
        return (TcpSession::Connected(remote));
    }
}

void SslSession::Accepted() {
    
    if (ssl_socket_) {
        // trigger ssl server handshake
        std::srand(std::time(0));
        ssl_socket_->async_handshake
            (boost::asio::ssl::stream_base::server,
             boost::bind(&SslSession::AcceptHandShakeHandler, TcpSessionPtr(this),
                         boost::asio::placeholders::error));
    } else {
        TcpSession::Accepted();
    }
}

void SslSession::AcceptHandShakeHandler(TcpSessionPtr session,
        const boost::system::error_code& error) {
    SslSession *ssl_session = static_cast<SslSession *>(session.get());
    if (!error) {
        // on successful handshake continue with tcp session state machine.
        ssl_session->TcpSession::Accepted();
    } else {
        // close session on failure
        ssl_session->CloseInternal(false);
    }
}

void SslSession::ConnectHandShakeHandler(TcpSessionPtr session, Endpoint remote,
        const boost::system::error_code& error) {
    SslSession *ssl_session = static_cast<SslSession *>(session.get());
    bool ret = false;
    if (!error) {
        // on successful handshake continue with tcp session state machine.
        ret = ssl_session->TcpSession::Connected(remote);
    }
    if (ret == false) {
        // report connect failure and close the session
        ssl_session->ConnectFailed();
        ssl_session->CloseInternal(false);
    }
}


void SslSession::AsyncReadSome(boost::asio::mutable_buffer buffer) {
    if (ssl_socket_) {
        ssl_socket_->async_read_some(mutable_buffers_1(buffer),
            boost::bind(&TcpSession::AsyncReadHandler, TcpSessionPtr(this), buffer,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
    } else {
        TcpSession::AsyncReadSome(buffer);
    }
}

std::size_t SslSession::WriteSome(const uint8_t *data, std::size_t len,
                                  boost::system::error_code &error) {
    if (ssl_socket_) {
        return ssl_socket_->write_some(boost::asio::buffer(data, len), error);
    } else {
        return (TcpSession::WriteSome(data, len, error));
    }
}

void SslSession::AsyncWrite(const u_int8_t *data, std::size_t size) {
    if (ssl_socket_) {
        boost::asio::async_write(
            *ssl_socket_.get(), buffer(data, size),
            boost::bind(&TcpSession::AsyncWriteHandler, TcpSessionPtr(this),
                        boost::asio::placeholders::error));
    } else {
        TcpSession::AsyncWrite(data, size);
    }
}

