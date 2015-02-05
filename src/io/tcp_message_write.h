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
    static const int kDefaultBufferSize = 4 * 1024;
    explicit TcpMessageWriter(TcpSession *session);
    ~TcpMessageWriter();

    // return false for send  
    int Send(const uint8_t *msg, size_t len, error_code &ec);

private:
    friend class TcpSession;
    typedef boost::intrusive_ptr<TcpSession> TcpSessionPtr;
    typedef std::list<boost::asio::mutable_buffer> BufferQueue;
    void BufferAppend(const uint8_t *data, int len);
    void DeleteBuffer(boost::asio::mutable_buffer buffer); 
    void HandleWriteReady(boost::system::error_code &ec);

    BufferQueue buffer_queue_;
    int offset_;
    TcpSession *session_;
};

#endif
