/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_IO_TCP_MESSAGE_WRITE_H_
#define SRC_IO_TCP_MESSAGE_WRITE_H_

#include <tbb/mutex.h>

#include <list>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/system/error_code.hpp>
#include "base/util.h"

class TcpSession;

class TcpMessageWriter {
public:
    static const int kDefaultBufferSize = 4 * 1024;
    static const int kDefaultWriteBufferSize = 16 * 1024;
    static const int kMaxPendingBufferSize = 256 *1024;
    static const int kMinPendingBufferSize = 32 *1024;

    explicit TcpMessageWriter(TcpSession *session);
    ~TcpMessageWriter();

    // return false for send
    int Send(const uint8_t *msg, size_t len,
             boost::system::error_code *ec);

    int AsyncSend(const uint8_t *msg, size_t len,
                  boost::system::error_code *ec);

private:
    friend class TcpSession;
    typedef boost::intrusive_ptr<TcpSession> TcpSessionPtr;
    typedef std::list<boost::asio::mutable_buffer> BufferQueue;
    void BufferAppend(const uint8_t *data, int len);
    void DeleteBuffer(boost::asio::mutable_buffer buffer);
    bool FlushBuffer(size_t wrote, bool *send_ready);
    void HandleWriteReady(boost::system::error_code *ec);
    void TriggerAsyncWrite();

    BufferQueue buffer_queue_;
    uint offset_;
    uint last_write_;
    TcpSession *session_;
};

#endif  // SRC_IO_TCP_MESSAGE_WRITE_H_
