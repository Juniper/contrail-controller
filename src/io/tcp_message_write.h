/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __MESSAGE_WRITE_H__
#define __MESSAGE_WRITE_H__

#include <list>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/system/error_code.hpp>
#include <tbb/mutex.h>
#include "base/util.h"

using namespace boost::system;

class TcpSession;

class TcpMessageWriter {
public:
    typedef boost::asio::ip::tcp::socket Socket;
    static const int kDefaultBufferSize = 4 * 1024;
    explicit TcpMessageWriter(Socket *, TcpSession *session);
    ~TcpMessageWriter();

    // return false for send  
    int Send(const uint8_t *msg, size_t len, error_code &ec);

    typedef boost::function<void(const error_code &ec)> SendReadyCb;
    void RegisterNotification(SendReadyCb);

private:
    typedef boost::intrusive_ptr<TcpSession> TcpSessionPtr;
    typedef std::list<boost::asio::mutable_buffer> BufferQueue;
    void BufferAppend(const uint8_t *data, int len);
    void DeleteBuffer(boost::asio::mutable_buffer buffer); 
    void DeferWrite();
    void HandleWriteReady(TcpSessionPtr session_ref, const error_code &ec,
                          uint64_t block_start_time);

    BufferQueue buffer_queue_;
    SendReadyCb cb_;
    Socket *socket_;
    int offset_;
    TcpSession *session_;
};

#endif
