/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_SESSION_H_
#define SRC_BGP_BGP_SESSION_H_

#include <boost/scoped_ptr.hpp>

#include <string>

#include "bgp/bgp_peer.h"
#include "io/tcp_session.h"

class BgpPeer;
class BgpSessionManager;

class BgpMessageReader : public TcpMessageReader {
public:
    BgpMessageReader(TcpSession *session, ReceiveCallback callback);
    virtual ~BgpMessageReader();

protected:
    virtual int MsgLength(Buffer buffer, int offset);

    virtual const int GetHeaderLenSize() {
        return kHeaderLenSize;
    }

    virtual const int GetMaxMessageSize() {
        return kMaxMessageSize;
    }

private:
    static const int kHeaderLenSize = 18;
    static const int kMaxMessageSize = 4096;

    DISALLOW_COPY_AND_ASSIGN(BgpMessageReader);
};

class BgpSession : public TcpSession {
public:
    BgpSession(BgpSessionManager *session_mgr, Socket *socket);
    virtual ~BgpSession();

    void SendNotification(int code, int subcode,
                          const std::string &data = std::string());
    virtual int GetSessionInstance() const { return index_; }
    void ProcessWriteReady();

    void set_peer(BgpPeer *peer) {
        peer_ = peer;
        index_ = peer_->GetIndex();
    }
    void clear_peer() {
        peer_ = NULL;
        index_ = -1;
    }
    BgpPeer *peer() { return peer_; }

protected:
    virtual void OnRead(Buffer buffer) {
        reader_->OnRead(buffer);
    }

private:
    bool ReceiveMsg(const u_int8_t *msg, size_t size) {
        return peer_->ReceiveMsg(this, msg, size);
    }
    virtual void WriteReady(const boost::system::error_code &error);

    BgpSessionManager *session_mgr_;
    BgpPeer *peer_;
    int index_;
    boost::scoped_ptr<BgpMessageReader> reader_;

    DISALLOW_COPY_AND_ASSIGN(BgpSession);
};

#endif  // SRC_BGP_BGP_SESSION_H_
