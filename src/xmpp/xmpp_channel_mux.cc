/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_channel_mux.h"

#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_connection.h"

using namespace std;
using namespace xmsm;

XmppChannelMux::XmppChannelMux(XmppConnection *connection)
    : connection_(connection), rx_message_trace_cb_(NULL),
      tx_message_trace_cb_(NULL) {
        last_received_ = 0;
        last_sent_ = 0;
}

XmppChannelMux::~XmppChannelMux() {
    assert(map_.empty());
}

void XmppChannelMux::Close() {
    connection_->Clear();
}

bool XmppChannelMux::LastReceived(time_t duration) const {
    return (UTCTimestamp() - last_received_) <= duration;
}

bool XmppChannelMux::LastSent(time_t duration) const {
    return (UTCTimestamp() - last_sent_) <= duration;
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
                          const string *msg_str, xmps::PeerId id,
                          SendReadyCb cb) {
    if (!connection_) return false;

    tbb::mutex::scoped_lock lock(mutex_);
    last_sent_ = UTCTimestamp();
    bool res = connection_->Send(msg, msgsize, msg_str);
    if (res == false) {
        RegisterWriteReady(id, cb);
    }
    return res;
}

int XmppChannelMux::GetTaskInstance() const {
    return connection_->GetTaskInstance();
}

void XmppChannelMux::RegisterReferer(xmps::PeerId id) {
    referers_.insert(id);
}

void XmppChannelMux::UnRegisterReferer(xmps::PeerId id) {
    referers_.erase(id);
}

void XmppChannelMux::RegisterReceive(xmps::PeerId id, ReceiveCb cb) {
    rxmap_.insert(make_pair(id, cb));
}

void XmppChannelMux::UnRegisterReceive(xmps::PeerId id) {
    ReceiveCbMap::iterator it = rxmap_.find(id);
    if (it != rxmap_.end()) {
        rxmap_.erase(it);
    }

    if (ReceiverCount())
        return;

    XmppServerConnection *server_connection =
        dynamic_cast<XmppServerConnection *>(connection_);

    // If GracefulRestart helper mode close process is complete, restart the
    // state machine to form new session with the client.
    if (!connection_->IsDeleted() && server_connection &&
            server_connection->server()->IsGRHelperModeEnabled()) {
        server_connection->state_machine()->Initialize();
        return;
    }

    connection_->RetryDelete();
}

size_t XmppChannelMux::RefererCount() const {
    return referers_.size();
}

size_t XmppChannelMux::ReceiverCount() const {
    return rxmap_.size();
}

vector<string> XmppChannelMux::GetReceiverList() const {
    vector<string> receivers;
    BOOST_FOREACH(const ReceiveCbMap::value_type &value, rxmap_) {
        receivers.push_back(xmps::PeerIdToName(value.first));
    }
    return receivers;
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

const std::string &XmppChannelMux::ToString() const {
    return connection_->ToString();
}

const std::string &XmppChannelMux::FromString() const {
    return connection_->FromString();
}

std::string XmppChannelMux::StateName() const {
    return connection_->StateName();
}

std::string XmppChannelMux::AuthType() const {
    return connection_->GetXmppAuthenticationType();
}

std::string XmppChannelMux::PeerAddress() const {
    return connection_->endpoint_string();
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
    last_received_ = UTCTimestamp();
    ReceiveCbMap::iterator iter = rxmap_.begin();
    for (; iter != rxmap_.end(); ++iter) {
        if (MatchCallback(msg->to, iter->first)) {
            ReceiveCb cb = iter->second;
            cb(msg, GetPeerState());
        }
    }
}

void XmppChannelMux::HandleStateEvent(xmsm::XmState state) {
    CHECK_CONCURRENCY("xmpp::StateMachine");
    xmps::PeerState st = xmps::NOT_READY;
    if (state == xmsm::ESTABLISHED) {
        st = xmps::READY;
    } else if (state == xmsm::ACTIVE) {
        st = xmps::TIMEDOUT;
    }

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

void XmppChannelMux::RegisterRxMessageTraceCallback(RxMessageTraceCb cb) {
    rx_message_trace_cb_ = cb;
}
void XmppChannelMux::RegisterTxMessageTraceCallback(TxMessageTraceCb cb) {
    tx_message_trace_cb_ = cb;
}

bool XmppChannelMux::RxMessageTrace(const std::string &to_address,
                                    int port,
                                    int msg_size,
                                    const std::string &msg,
                                    const XmppStanza::XmppMessage *xmpp_msg) {
    if (rx_message_trace_cb_) {
        return rx_message_trace_cb_(to_address, port, msg_size, msg, xmpp_msg);
    }
    return false;
}

bool XmppChannelMux::TxMessageTrace(const std::string &to_address,
                                    int port,
                                    int msg_size,
                                    const std::string &msg,
                                    const XmppStanza::XmppMessage *xmpp_msg) {
    if (tx_message_trace_cb_) {
        return tx_message_trace_cb_(to_address, port, msg_size, msg, xmpp_msg);
    }
    return false;
}
