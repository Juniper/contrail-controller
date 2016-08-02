/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "ssl_session.h"
#include "io/io_log.h"
#include "io/io_utils.h"

using boost::asio::async_write;
using boost::asio::buffer;
using boost::asio::mutable_buffer;
using boost::asio::null_buffers;
using boost::bind;
using boost::function;
using boost::system::error_code;
using std::size_t;
using std::srand;
using std::string;
using std::time;

using namespace boost::asio;

class SslSession::SslReader : public Task {
public:
    typedef function<void(Buffer)> ReadHandler;

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
            } else if (!session_->IsSslHandShakeInProgress()) {
                session_->AsyncReadStart();
            }
        }
        return true;
    }
    string Description() const { return "SslSession::SslReader"; }
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

Task* SslSession::CreateReaderTask(mutable_buffer buffer,
                                   size_t bytes_transferred) {

    Buffer rdbuf(buffer_cast<const uint8_t *>(buffer), bytes_transferred);
    SslReader *task = new SslReader(this->reader_task_id(),
        SslSessionPtr(this), bind(&SslSession::OnRead, this, _1), rdbuf);
    return (task);
}


TcpSession::Socket *SslSession::socket() const {
    // return tcp socket
    return &ssl_socket_->next_layer();
}

bool SslSession::ReadSome(mutable_buffer buffer,
                          size_t *bytes_transferred,
                          error_code &error) {
    // no processing needed if ssl handshake is not complete.
    if (!IsSslHandShakeSuccessLocked()) {
        if (!ssl_handshake_in_progress_)
            return TcpSession::ReadSome(buffer, bytes_transferred, error);
        return false;
    }

    // do ssl read here in IO context, ignore errors
    *bytes_transferred = ssl_socket_->read_some(mutable_buffers_1(buffer),
                                                error);

    return true;
}

void SslSession::AsyncReadSome() {
    if (IsSslHandShakeSuccessLocked() || !ssl_handshake_in_progress_)
        TcpSession::AsyncReadSome();
}

size_t SslSession::WriteSome(const uint8_t *data, size_t len,
                             error_code &error) {

    if (IsSslHandShakeSuccessLocked()) {
        return ssl_socket_->write_some(buffer(data, len), error);
    } else {
        return (TcpSession::WriteSome(data, len, error));
    }
}

void SslSession::AsyncWrite(const u_int8_t *data, size_t size) {
    if (IsSslHandShakeSuccessLocked()) {
        async_write(*ssl_socket_.get(), buffer(data, size),
                bind(&TcpSession::AsyncWriteHandler,
                TcpSessionPtr(this), placeholders::error));
    } else {
        return (TcpSession::AsyncWrite(data, size));
    }
}

void SslSession::SslHandShakeCallback(SslHandShakeCallbackHandler cb,
                                      SslSessionPtr session,
                                      const error_code &error) {

    session->ssl_handshake_in_progress_ = false;
    if (!error) {
        session->SetSslHandShakeSuccess();
    } else {
        session->SetSslHandShakeFailure();
    }

    if (session->socket() != NULL && !(session->IsClosed())) {
        cb(session, error);
    }
}

void SslSession::TriggerSslHandShakeInternal(
        SslSessionPtr session, SslHandShakeCallbackHandler cb) {
    srand(time(0));
    error_code ec;
    session->ssl_handshake_in_progress_ = true;
    if (session->IsServerSession()) {
        session->ssl_socket_->async_handshake(ssl::stream_base::server,
            bind(&SslSession::SslHandShakeCallback, cb, session,
            placeholders::error));
    } else {
        session->ssl_socket_->async_handshake(ssl::stream_base::client,
            bind(&SslSession::SslHandShakeCallback, cb, session,
                 placeholders::error));
    }
}

void SslSession::TriggerSslHandShake(SslHandShakeCallbackHandler cb) {
    server()->event_manager()->io_service()->post(
        bind(&TriggerSslHandShakeInternal, SslSessionPtr(this), cb));
}
