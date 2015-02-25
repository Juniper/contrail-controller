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
            if (session_->IsSslDisabled() || 
                session_->IsSslHandShakeSuccess()) {
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
                       bool async_read_ready, bool ssl_disabled, 
                       bool ssl_handshake_delayed) :
                       TcpSession(server, NULL, async_read_ready),
                       server_(server),
                       ssl_socket_(ssl_socket),
                       ssl_handshake_success_(false),
                       ssl_disabled_(ssl_disabled),
                       ssl_handshake_delayed_(ssl_handshake_delayed) {
}

SslSession::~SslSession() {
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
        ssl_socket_->async_handshake
            (boost::asio::ssl::stream_base::server,
             boost::bind(&SslSession::AcceptHandShakeHandler, TcpSessionPtr(this),
                         boost::asio::placeholders::error));
    }
}

void SslSession::AcceptHandShakeHandler(TcpSessionPtr session,
        const boost::system::error_code& error) {

    SslSession *ssl_session = static_cast<SslSession *>(session.get());
    if (!error) {
        // on successful handshake continue with tcp session state machine.
        ssl_session->SetSslHandShakeStatus(true);
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
        ssl_session->SetSslHandShakeStatus(true);
        ret = ssl_session->TcpSession::Connected(remote);
    }
    if (ret == false) {
        // report connect failure and close the session
        ssl_session->ConnectFailed();
        ssl_session->CloseInternal(false);
    }
}

void SslSession::AsyncReadHandler(
    SslSessionPtr session, mutable_buffer buffer,
    const boost::system::error_code &error, size_t bytes_transferred) {

    tbb::mutex::scoped_lock lock(session->mutex_);
    if (session->IsClosedLocked()) {
        session->ReleaseBufferLocked(buffer);
        return;
    }

    if (IsSocketErrorHard(error)) {
        session->ReleaseBufferLocked(buffer);
        TCP_SESSION_LOG_UT_DEBUG(session, TCP_DIR_IN,
                                 "Read failed due to error " << error.value()
                                     << " : " << error.message());
        lock.release();
        session->CloseInternal(true);
        return;
    }

    // Update read statistics.
    session->stats_.read_calls++;
    session->stats_.read_bytes += bytes_transferred;
    session->server_->stats_.read_calls++;
    session->server_->stats_.read_bytes += bytes_transferred;

    Buffer rdbuf(buffer_cast<const uint8_t *>(buffer), bytes_transferred);
    SslReader *task = new SslReader(session->reader_task_id(),
        session, boost::bind(&SslSession::OnRead, session.get(), _1), rdbuf);
    // Starting a new task for the session
    if (task) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(task);
    }
}

void SslSession::AsyncReadSome(boost::asio::mutable_buffer buffer) {
    if (IsSslHandShakeSuccess()) {
        ssl_socket_->async_read_some(mutable_buffers_1(buffer),
            boost::bind(&SslSession::AsyncReadHandler, SslSessionPtr(this), buffer,
                         boost::asio::placeholders::error,
                         boost::asio::placeholders::bytes_transferred));
    } else {
        socket()->async_read_some(mutable_buffers_1(buffer),
            boost::bind(&SslSession::AsyncReadHandler, SslSessionPtr(this), buffer,
                         boost::asio::placeholders::error,
                         boost::asio::placeholders::bytes_transferred));
    }
}

std::size_t SslSession::WriteSome(const uint8_t *data, std::size_t len,
                                  boost::system::error_code &error) {

    if (IsSslHandShakeSuccess()) {
        return ssl_socket_->write_some(boost::asio::buffer(data, len), error);
    } else {
        return (TcpSession::WriteSome(data, len, error));
    }
}

void SslSession::AsyncWrite(const u_int8_t *data, std::size_t size) {
    if (IsSslHandShakeSuccess()) {
        boost::asio::async_write(
            *ssl_socket_.get(), buffer(data, size),
            boost::bind(&TcpSession::AsyncWriteHandler, TcpSessionPtr(this),
                         boost::asio::placeholders::error));
    } else {
        return (TcpSession::AsyncWrite(data, size));
    }
}

//Trigger ssl server handshake and register callback handler
void SslSession::TriggerServerSslHandShake(SslHandShakeCallbackHandler cb) {
    std::srand(std::time(0));
    ssl_socket_->async_handshake(boost::asio::ssl::stream_base::server,
        boost::bind(cb, SslSessionPtr(this), boost::asio::placeholders::error));
}

//Trigger ssl client handshake and register callback handler
void SslSession::TriggerClientSslHandShake(SslHandShakeCallbackHandler cb) {
    std::srand(std::time(0));
    ssl_socket_->async_handshake(boost::asio::ssl::stream_base::client,
        boost::bind(cb, SslSessionPtr(this), boost::asio::placeholders::error));
}
