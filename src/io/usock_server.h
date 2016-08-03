/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

/*
 * Implement a UNIX domain socket interface using boost::asio.
 */
#ifndef SRC_IO_USOCK_SERVER_H_
#define SRC_IO_USOCK_SERVER_H_

#include <cstdio>
#include <iostream>
#include <list>
#include <string>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/function.hpp>

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

class UnixDomainSocketSession :
public boost::enable_shared_from_this<UnixDomainSocketSession> {
public:
    static const int kPDULen = 4096;
    static const int kPDUHeaderLen = 4;
    static const int kPDUDataLen = 4092;

    enum Event {
        EVENT_NONE,
        READY,
        CLOSE
    };

    typedef boost::function <void (UnixDomainSocketSession *, Event)>
      EventObserver;

    explicit UnixDomainSocketSession(boost::asio::io_service *io_service)
        : socket_(*io_service), session_id_(0) {
    }

    ~UnixDomainSocketSession();

    boost::asio::local::stream_protocol::socket &socket() {
        return socket_;
    }

    void set_observer(EventObserver observer) { observer_ = observer; }
    uint64_t session_id() { return session_id_; }
    void set_session_id(uint64_t id) { session_id_ = id; }
    void Start();
    void Send(const uint8_t * data, int data_len);

private:
    typedef std::list <boost::asio::mutable_buffer> BufferQueue;

    void WriteToSocket();
    void AppendBuffer(const uint8_t * data, int len);
    void DeleteBuffer(boost::asio::mutable_buffer);
    void HandleRead(const boost::system::error_code & error, size_t bytes);
    void HandleWrite(const boost::system::error_code & error);

    boost::asio::local::stream_protocol::socket socket_;
    BufferQueue buffer_queue_;
    uint8_t data_[kPDULen];
    EventObserver observer_;
    uint64_t session_id_;
};

typedef boost::shared_ptr <UnixDomainSocketSession> SessionPtr;

class UnixDomainSocketServer {
public:
    enum Event {
        EVENT_NONE,
        NEW_SESSION,
        DELETE_SESSION
    };

    typedef boost::function <void (UnixDomainSocketServer *,
                                   UnixDomainSocketSession *, Event) >
      EventObserver;

    UnixDomainSocketServer(boost::asio::io_service *io_service,
                           const std::string &file);

    void HandleAccept(SessionPtr new_session,
                      const boost::system::error_code &error);

    void set_observer(EventObserver observer) { observer_ = observer; }

private:
    boost::asio::io_service *io_service_;
    EventObserver observer_;
    boost::asio::local::stream_protocol::acceptor acceptor_;
    uint64_t session_idspace_;
};

#else  // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
#error Local sockets not available on this platform.
#endif  // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

#endif  // SRC_IO_USOCK_SERVER_H_
