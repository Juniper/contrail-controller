/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

/*
 * The primary method implemented here is Send(), to transmit a
 * message over the Unix socket. It uses boost::asio::async_write to
 * send one message at a time over the socket, that is transmitted
 * asynchronously. The user can repeatedly call Send(). All those
 * buffers are tail-queued. Upon write_complete callback, the next
 * message from the front of the queue is sent.
 */
#include "io/usock_server.h"

using boost::asio::buffer_cast;
using boost::asio::buffer;
using boost::asio::mutable_buffer;

UnixDomainSocketSession::~UnixDomainSocketSession() {
    if (observer_) {
        observer_(this, CLOSE);
    }

    /* Free up any remaining buffers in the queue. */
    for (BufferQueue::iterator iter = buffer_queue_.begin();
         iter != buffer_queue_.end(); ++iter) {
        DeleteBuffer(*iter);
    }
    buffer_queue_.clear();
}

void UnixDomainSocketSession::Start() {
    if (observer_) {
        observer_(this, READY);
    }

    socket_.async_read_some(boost::asio::buffer(data_),
                            boost::bind(&UnixDomainSocketSession::
                                        HandleRead, shared_from_this(),
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::
                                        bytes_transferred));
}

void UnixDomainSocketSession::Send(const uint8_t * data, int data_len) {
    if (!data || !data_len) {
        return;
    }
    bool write_now = buffer_queue_.empty();
    AppendBuffer(data, data_len);
    if (write_now) {
        WriteToSocket();
    }
}

void UnixDomainSocketSession::WriteToSocket() {
    if (buffer_queue_.empty()) {
        return;
    }

    boost::asio::mutable_buffer head = buffer_queue_.front();
    boost::asio::async_write(socket_,
                             buffer(buffer_cast <const uint8_t *>(head),
                                    boost::asio::buffer_size(head)),
                             boost::bind(&UnixDomainSocketSession::
                                         HandleWrite, shared_from_this(),
                                         boost::asio::placeholders::error));
}

void UnixDomainSocketSession::AppendBuffer(const uint8_t *src, int bytes) {
    u_int8_t *data = new u_int8_t[bytes];
    memcpy(data, src, bytes);
    boost::asio::mutable_buffer buffer =
        boost::asio::mutable_buffer(data, bytes);
    buffer_queue_.push_back(buffer);
}

void UnixDomainSocketSession::DeleteBuffer(boost::asio::mutable_buffer buffer) {
    const uint8_t *data = buffer_cast <const uint8_t *>(buffer);
    delete []data;
    return;
}

void UnixDomainSocketSession::HandleRead(const boost::system::error_code &error,
                                         size_t bytes_transferred) {
    if (error) {
        return;
    }
    if (observer_) {
        observer_(this, READY);
    }
}

void UnixDomainSocketSession::HandleWrite(
        const boost::system::error_code &error) {
    /*
     * async_write() is atomic in that it returns success once the entire message
     * is sent. If there is an error, it's okay to return from here so that the
     * session gets closed.
     */
    if (error) {
        return;
    }

    /*
     * We are done with the buffer at the head of the queue. Delete it.
     */
    DeleteBuffer(buffer_queue_.front());
    buffer_queue_.pop_front();

    /*
     * Write the next message, if there.
     */
    WriteToSocket();

    /*
     * Engage on the socket to keep it alive.
     */
    socket_.async_read_some(boost::asio::buffer(data_),
                            boost::bind(&UnixDomainSocketSession::
                                        HandleRead, shared_from_this(),
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::
                                        bytes_transferred));
}

UnixDomainSocketServer::UnixDomainSocketServer(
        boost::asio::io_service *io, const std::string &file)
  : io_service_(io),
    acceptor_(*io, boost::asio::local::stream_protocol::endpoint(file)),
    session_idspace_(0) {
    SessionPtr new_session(new UnixDomainSocketSession(io_service_));
    acceptor_.async_accept(new_session->socket(),
                           boost::bind(&UnixDomainSocketServer::
                                       HandleAccept, this, new_session,
                                       boost::asio::placeholders::error));
}

void
UnixDomainSocketServer::HandleAccept(SessionPtr session,
                                     const boost::system::error_code &error) {
    UnixDomainSocketSession *socket_session = session.get();

    if (error) {
        if (observer_) {
            observer_(this, socket_session, DELETE_SESSION);
        }
        return;
    }

    socket_session->set_session_id(++session_idspace_);
    if (observer_) {
        observer_(this, socket_session, NEW_SESSION);
        session->Start();
    }

    SessionPtr new_session(new UnixDomainSocketSession(io_service_));
    acceptor_.async_accept(new_session->socket(),
                           boost::bind(&UnixDomainSocketServer::
                                       HandleAccept, this, new_session,
                                       boost::asio::placeholders::error));
}
