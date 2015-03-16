/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "ssl_session.h"
#include "io/io_utils.h"
#include "io/io_log.h"

using namespace boost::asio;

class SslSession::SslReader : public Task {
public:
    typedef boost::function<void(Buffer)> ReadHandler;

    SslReader(int task_id, SslSessionPtr session, ReadHandler read_fn,
              Buffer buffer)
       : Task(task_id, session->GetSessionInstance()),
         session_(session), read_fn_(read_fn), buffer_(buffer) {
    }
    virtual bool Run() {
        if (session_->IsEstablished()) {
            read_fn_(buffer_);
            if (session_->IsSslDisabled()) {
                session_->AsyncReadStart();
            } else if (session_->IsSslHandShakeSuccess()) {
                session_->AsyncReadStart();
            }
        }
        return true;
    }
private:
    SslSessionPtr session_;
    ReadHandler read_fn_;
    Buffer buffer_;
};

SslSession::SslSession(SslServer *server, SslSocket *ssl_socket,
                       bool async_read_ready)
    : TcpSession(server, NULL, async_read_ready),
      ssl_socket_(ssl_socket),
      ssl_handshake_in_progress_(false),
      ssl_handshake_success_(false),
      ssl_enabled_(true),
      ssl_handshake_delayed_(false) {

    if (server) {
        ssl_enabled_ = server->ssl_enabled_;
        ssl_handshake_delayed_ = server->ssl_handshake_delayed_;
    }
}

SslSession::~SslSession() {
}

Task* SslSession::CreateReaderTask(boost::asio::mutable_buffer buffer,
                                   size_t bytes_transferred) {

    Buffer rdbuf(buffer_cast<const uint8_t *>(buffer), bytes_transferred);
    SslReader *task = new SslReader(this->reader_task_id(),
        SslSessionPtr(this), boost::bind(&SslSession::OnRead, this, _1), rdbuf);
    return (task);
}


TcpSession::Socket *SslSession::socket() const {
    // return tcp socket
    return &ssl_socket_->next_layer();
}

bool SslSession::Connected(Endpoint remote) {
    if (IsClosed()) {
        return false;
    }

    if (IsSslDisabled() || IsSslHandShakeDelayed()) {
        return (TcpSession::Connected(remote));
    } else {
        // trigger ssl client handshake
        std::srand(std::time(0));
        ssl_handshake_in_progress_ = true;
        ssl_socket_->async_handshake
            (boost::asio::ssl::stream_base::client,
             boost::bind(&SslSession::ConnectHandShakeHandler, TcpSessionPtr(this),
                         remote, boost::asio::placeholders::error));
        return true;
    }
}

void SslSession::Accepted() {

    if (IsSslDisabled() || IsSslHandShakeDelayed()) {
        return (TcpSession::Accepted());
    } else {
        // trigger ssl server handshake
        std::srand(std::time(0));
        ssl_handshake_in_progress_ = true;
        ssl_socket_->async_handshake
            (boost::asio::ssl::stream_base::server,
             boost::bind(&SslSession::AcceptHandShakeHandler, TcpSessionPtr(this),
                         boost::asio::placeholders::error));
    }
}

void SslSession::AcceptHandShakeHandler(TcpSessionPtr session,
        const boost::system::error_code& error) {

    SslSession *ssl_session = static_cast<SslSession *>(session.get());
    ssl_session->ssl_handshake_in_progress_ = false;
    if (!error) {
        // on successful handshake continue with tcp session state machine.
        ssl_session->SetSslHandShakeSuccess();
        ssl_session->TcpSession::Accepted();
    } else {
        // close session on failure
        ssl_session->SetSslHandShakeFailure();
        ssl_session->CloseInternal(false);
    }
}

void SslSession::ConnectHandShakeHandler(TcpSessionPtr session, Endpoint remote,
        const boost::system::error_code& error) {

    SslSession *ssl_session = static_cast<SslSession *>(session.get());
    ssl_session->ssl_handshake_in_progress_ = false;
    bool ret = false;
    if (!error) {
        // on successful handshake continue with tcp session state machine.
        ssl_session->SetSslHandShakeSuccess();
        ret = ssl_session->TcpSession::Connected(remote);
    }
    if (ret == false) {
        // report connect failure and close the session
        ssl_session->SetSslHandShakeFailure();
        ssl_session->ConnectFailed();
        ssl_session->CloseInternal(false);
    }
}

void SslSession::AsyncReadSome(boost::asio::mutable_buffer buffer) {
    if (IsSslHandShakeSuccessLocked()) {
        ssl_socket_->async_read_some(mutable_buffers_1(buffer),
            boost::bind(&TcpSession::AsyncReadHandler, SslSessionPtr(this), buffer,
                         boost::asio::placeholders::error,
                         boost::asio::placeholders::bytes_transferred));
    } else {
        // No tcp socket read/write while ssl handshake is ongoing
        if (!ssl_handshake_in_progress_) {
            socket()->async_read_some(mutable_buffers_1(buffer),
                boost::bind(&TcpSession::AsyncReadHandler, SslSessionPtr(this), buffer,
                             boost::asio::placeholders::error,
                             boost::asio::placeholders::bytes_transferred));
        }
    }
}

std::size_t SslSession::WriteSome(const uint8_t *data, std::size_t len,
                                  boost::system::error_code &error) {

    if (IsSslHandShakeSuccessLocked()) {
        return ssl_socket_->write_some(boost::asio::buffer(data, len), error);
    } else {
        // No tcp socket read/write while ssl handshake is ongoing
        if (!ssl_handshake_in_progress_) {
            return (TcpSession::WriteSome(data, len, error));
        } else {
            return 0;
        }
    }
}

void SslSession::AsyncWrite(const u_int8_t *data, std::size_t size) {
    if (IsSslHandShakeSuccessLocked()) {
        boost::asio::async_write(
            *ssl_socket_.get(), buffer(data, size),
            boost::bind(&TcpSession::AsyncWriteHandler, TcpSessionPtr(this),
                         boost::asio::placeholders::error));
    } else {
        return (TcpSession::AsyncWrite(data, size));
    }
}

void SslSession::SslHandShakeCallback(SslHandShakeCallbackHandler cb,
    SslSessionPtr session,
    const boost::system::error_code &error) {

    session->ssl_handshake_in_progress_ = false;
    if (!error) {
        session->SetSslHandShakeSuccess();
    } else {
        session->SetSslHandShakeFailure();
    }

    cb(session, error);
}

void SslSession::TriggerSslHandShake(SslHandShakeCallbackHandler cb) {
    std::srand(std::time(0));
    ssl_handshake_in_progress_ = true;
    if (IsServerSession()) {
        ssl_socket_->async_handshake(boost::asio::ssl::stream_base::server,
            boost::bind(&SslSession::SslHandShakeCallback, cb, SslSessionPtr(this),
            boost::asio::placeholders::error));
    } else {
        ssl_socket_->async_handshake(boost::asio::ssl::stream_base::client,
            boost::bind(&SslSession::SslHandShakeCallback, cb, SslSessionPtr(this),
            boost::asio::placeholders::error));
    }
}
