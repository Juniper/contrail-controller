/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_session.h"

#include <algorithm>

#include "base/logging.h"
#include "base/parse_object.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/scheduling_group.h"

using namespace std;

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

BgpMessageReader::BgpMessageReader(TcpSession *session, ReceiveCallback callback) :
        TcpMessageReader(session, callback) {
}

BgpMessageReader::~BgpMessageReader() {
}

BgpSession::BgpSession(BgpSessionManager *session, Socket *socket)
    : TcpSession(session, socket),
      peer_(NULL),
      reader_(new BgpMessageReader(this,
              boost::bind(&BgpSession::ReceiveMsg, this, _1, _2))) {
}

BgpSession::~BgpSession() {
}

//
// Handle write ready callback.
//
// 1. Tell SchedulingGroupManager that the IPeer is send ready.
// 2. Tell BgpPeer that it's send ready so that it can resume Keepalives.
//
// We can ignore any errors since the StateMachine will get informed of the
// TcpSession close independently and react to it.
//
void BgpSession::WriteReady(const boost::system::error_code &error) {
    if (error || !peer_)
        return;

    BgpServer *server = peer_->server();
    SchedulingGroupManager *sg_mgr = server->scheduling_group_manager();
    sg_mgr->SendReady(peer_);
    peer_->SetSendReady();
}

int BgpSession::GetSessionInstance() const {
    return peer_->GetIndex();
}

void BgpSession::SendNotification(int code, int subcode,
                                  const std::string &data) {
    BgpProto::Notification msg;
    msg.error = code;
    msg.subcode = subcode;
    msg.data = data;
    uint8_t buf[256];
    int result = BgpProto::Encode(&msg, buf, sizeof(buf));
    assert(result > BgpProto::kMinMessageSize);

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
    Send(buf, result, NULL);
}
