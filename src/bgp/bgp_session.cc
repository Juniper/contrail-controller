/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_session.h"

#include <algorithm>
#include <string>

#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/scheduling_group.h"

using std::string;

using boost::asio::mutable_buffer;

// Extract the total BGP message length. This is a 2 byte field after the
// 16 byte marker. If the buffer doesn't have 18 bytes available return -1.
int BgpMessageReader::MsgLength(Buffer buffer, int offset) {
    size_t size = TcpSession::BufferSize(buffer);
    int remain = size - offset;
    if (remain < BgpMessageReader::kHeaderLenSize) {
        return -1;
    }
    const uint8_t *data = TcpSession::BufferData(buffer) + offset;
    data += 16;
    int length = get_value(data, 2);
    return length;
}

BgpMessageReader::BgpMessageReader(TcpSession *session,
    ReceiveCallback callback)
    : TcpMessageReader(session, callback) {
}

BgpMessageReader::~BgpMessageReader() {
}

BgpSession::BgpSession(BgpSessionManager *session_mgr, Socket *socket)
    : TcpSession(session_mgr, socket),
      session_mgr_(session_mgr),
      peer_(NULL),
      index_(-1),
      reader_(new BgpMessageReader(this,
              boost::bind(&BgpSession::ReceiveMsg, this, _1, _2))) {
}

BgpSession::~BgpSession() {
}

//
// Concurrency: called in the context of io::Reader task.
//
bool BgpSession::ReceiveMsg(const u_int8_t *msg, size_t size) {
    return peer_->ReceiveMsg(this, msg, size);
}

//
// Concurrency: called in the context of bgp::Config task.
//
// Process write ready callback.
//
// 1. Tell SchedulingGroupManager that the IPeer is send ready.
// 2. Tell BgpPeer that it's send ready so that it can resume Keepalives.
//
void BgpSession::ProcessWriteReady() {
    if (!peer_)
        return;
    BgpServer *server = peer_->server();
    SchedulingGroupManager *sg_mgr = server->scheduling_group_manager();
    sg_mgr->SendReady(peer_);
    peer_->SetSendReady();
}

//
// Concurrency: called in the context of io thread.
//
// Handle write ready callback.  Enqueue the session to a WorkQueue in the
// BgpSessionManager.  The WorkQueue gets processed in the context of the
// bgp::Config task.  This ensures that we don't access the BgpPeer while
// the BgpPeer is trying to clear our back pointer to it.
//
// We can ignore any errors since the StateMachine will get informed of the
// TcpSession close independently and react to it.
//
void BgpSession::WriteReady(const boost::system::error_code &error) {
    if (error)
        return;
    session_mgr_->EnqueueWriteReady(this);
}

void BgpSession::SendNotification(int code, int subcode,
                                  const string &data) {
    BgpProto::Notification msg;
    msg.error = code;
    msg.subcode = subcode;
    msg.data = data;
    uint8_t buf[BgpProto::kMaxMessageSize];
    int msglen = BgpProto::Encode(&msg, buf, sizeof(buf));

    // Use SYS_DEBUG for connection collision, SYS_NOTICE for the rest.
    SandeshLevel::type log_level;
    if (code == BgpProto::Notification::Cease &&
        subcode == BgpProto::Notification::ConnectionCollision) {
        log_level = SandeshLevel::SYS_DEBUG;
    } else {
        log_level = SandeshLevel::SYS_NOTICE;
    }
    BGP_LOG(BgpPeerNotification, log_level, BGP_LOG_FLAG_ALL,
            peer_ ? peer_->ToUVEKey() : ToString(),
            BGP_PEER_DIR_OUT, code, subcode, msg.ToString());

    if (msglen > BgpProto::kMinMessageSize) {
        Send(buf, msglen, NULL);
    }
}

void BgpSession::set_peer(BgpPeer *peer) {
    peer_ = peer;
    index_ = peer_->GetIndex();
}

void BgpSession::clear_peer() {
    peer_ = NULL;
    index_ = -1;
}
