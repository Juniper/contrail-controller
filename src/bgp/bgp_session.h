/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_SESSION_H__
#define __BGP_SESSION_H__

#include <boost/scoped_ptr.hpp>

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
    BgpSession(BgpSessionManager *session, Socket *socket);
    virtual ~BgpSession();

    void SetPeer(BgpPeer *peer) { peer_ = peer; }
    BgpPeer *Peer() { return peer_; }
    void SendNotification(int code, int subcode,
                          const std::string &data = std::string());
    virtual int GetSessionInstance() const;

protected:
    virtual void OnRead(Buffer buffer) {
        reader_->OnRead(buffer);
    }

private:
    virtual void ReceiveMsg(const u_int8_t *msg, size_t size) {
        peer_->ReceiveMsg(this, msg, size);
    }
    virtual void WriteReady(const boost::system::error_code &error);

    BgpPeer *peer_;
    boost::scoped_ptr<BgpMessageReader> reader_;

    DISALLOW_COPY_AND_ASSIGN(BgpSession);
};

#endif
