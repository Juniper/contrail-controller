/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_channel_mux.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_connection.h"

using namespace std;
using namespace xmsm;

XmppChannelMux::XmppChannelMux(XmppConnection *connection) 
    : connection_(connection) {
}

XmppChannelMux::~XmppChannelMux() {
}

xmps::PeerState XmppChannelMux::GetPeerState() const {
    xmsm::XmState st = connection_->GetStateMcState();
    return (st == xmsm::ESTABLISHED) ? xmps::READY :
                                       xmps::NOT_READY;
}

void XmppChannelMux::WriteReady(const boost::system::error_code &ec) {
    tbb::mutex::scoped_lock lock(mutex_);

    WriteReadyCbMap::iterator iter = map_.begin();
    WriteReadyCbMap::iterator next = iter;
    for (; iter != map_.end(); iter = next) {
        ++next;
        SendReadyCb cb = iter->second;
        cb(ec);
        map_.erase(iter);
    }
}

bool XmppChannelMux::Send(const uint8_t *msg, size_t msgsize, 
                          xmps::PeerId id, 
                          SendReadyCb cb) {
    if (!connection_) return false;

    tbb::mutex::scoped_lock lock(mutex_);
    bool res = connection_->Send(msg, msgsize);
    if (res == false) {
        RegisterWriteReady(id, cb);
    }
    return res;
}

void XmppChannelMux::RegisterReceive(xmps::PeerId id, ReceiveCb cb) {
    rxmap_.insert(make_pair(id, cb));
}

void XmppChannelMux::UnRegisterReceive(xmps::PeerId id) {
    ReceiveCbMap::iterator it =  rxmap_.find(id);
    if (it != rxmap_.end()) {
        rxmap_.erase(it);
    }
    connection_->RetryDelete();
}

size_t XmppChannelMux::ReceiverCount() const {
    return rxmap_.size();
}

//
// To be called after acquiring mutex
//
void XmppChannelMux::RegisterWriteReady(xmps::PeerId id, SendReadyCb cb) {
    map_.insert(make_pair(id, cb));
}

//
// To be called after acquiring mutex
//
void XmppChannelMux::UnRegisterWriteReady(xmps::PeerId id) {
    map_.erase(id);
}

void XmppChannelMux::XmppChannelCleanup() {
    XmppServer *server = static_cast<XmppServer *>(connection_->server());
    server->RemoveConnection(connection_);
}

std::string XmppChannelMux::ToString() const {
    return connection_->ToString();
}

std::string XmppChannelMux::FromString() const {
    return connection_->FromString();
}

std::string XmppChannelMux::StateName() const {
    return connection_->StateName();
}

inline bool MatchCallback(string to, xmps::PeerId peer) {
    if ((to.find(XmppInit::kBgpPeer) != string::npos) && 
        (peer == xmps::BGP)) {
        return true;
    }
    if ((to.find(XmppInit::kConfigPeer) != string::npos) && 
        (peer == xmps::CONFIG)) {
        return true;
    }
    if ((to.find(XmppInit::kDnsPeer) != string::npos) && 
        (peer == xmps::DNS)) {
        return true;
    }
    if ((to.find(XmppInit::kOtherPeer) != string::npos) && 
        (peer == xmps::OTHER)) {
        return true;
    }
    return false;
}

void XmppChannelMux::ProcessXmppMessage(const XmppStanza::XmppMessage *msg) {
    ReceiveCbMap::iterator iter = rxmap_.begin();
    for (; iter != rxmap_.end(); ++iter) {
        if (MatchCallback(msg->to, iter->first)) {
            ReceiveCb cb = iter->second;
            cb(msg, GetPeerState());
        }
    }
}

void XmppChannelMux::HandleStateEvent(xmsm::XmState state) {

    xmps::PeerState st = (state == xmsm::ESTABLISHED) ? 
        xmps::READY : xmps::NOT_READY;

    if (connection_->IsClient()) {
        XmppClient *client = static_cast<XmppClient *>(connection_->server());
        client->NotifyConnectionEvent(this, st);
    } else {
        // Event to create the peer on server
        XmppServer *server = static_cast<XmppServer *>(connection_->server());
        server->NotifyConnectionEvent(this, st);
    }
}

std::string XmppChannelMux::LastStateName() const {
    return connection_->LastStateName();
}
std::string XmppChannelMux::LastStateChangeAt() const {
    return connection_->LastStateChangeAt();
}
std::string XmppChannelMux::LastEvent() const {
    return connection_->LastEvent();
}
uint32_t XmppChannelMux::rx_open() const {
    return connection_->rx_open();
}
uint32_t XmppChannelMux::rx_close() const {
    return connection_->rx_close();
}
uint32_t XmppChannelMux::rx_update() const {
    return connection_->rx_update();
}
uint32_t XmppChannelMux::rx_keepalive() const {
    return connection_->rx_keepalive();
}
uint32_t XmppChannelMux::tx_open() const {
    return connection_->tx_open();
}
uint32_t XmppChannelMux::tx_close() const {
    return connection_->tx_close();
}
uint32_t XmppChannelMux::tx_update() const {
    return connection_->tx_update();
}
uint32_t XmppChannelMux::tx_keepalive() const {
    return connection_->tx_keepalive();
}
uint32_t XmppChannelMux::FlapCount() const {
    return connection_->flap_count();
}
std::string XmppChannelMux::LastFlap() const {
    return connection_->last_flap_at();
}
