/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_connection.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <sstream>

#include "base/lifetime.h"
#include "base/task_annotations.h"
#include "io/event_manager.h"
#include "xml/xml_base.h"
#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_config.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/xmpp_log.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_session.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include "sandesh/xmpp_message_sandesh_types.h"
#include "sandesh/xmpp_state_machine_sandesh_types.h"
#include "sandesh/xmpp_trace_sandesh_types.h"
#include "sandesh/xmpp_peer_info_types.h"

using namespace std;
using boost::system::error_code;

XmppConnection::XmppConnection(TcpServer *server, 
                               const XmppChannelConfig *config)
    : server_(server),
      endpoint_(config->endpoint),
      local_endpoint_(config->local_endpoint),
      config_(NULL),
      session_(NULL),
      state_machine_(XmppObjectFactory::Create<XmppStateMachine>(
          this, config->ClientOnly())),
      keepalive_timer_(TimerManager::CreateTimer(
                           *server->event_manager()->io_service(),
                           "Xmpp keepalive timer")),
      log_uve_(config->logUVE),
      admin_down_(false), 
      from_(config->FromAddr),
      to_(config->ToAddr),
      mux_(XmppObjectFactory::Create<XmppChannelMux>(this)),
      keepalive_time_(GetDefaultkeepAliveTime()),
      disable_read_(false) {
}

XmppConnection::~XmppConnection() {
    StopKeepAliveTimer();
    TimerManager::DeleteTimer(keepalive_timer_);
    XMPP_UTDEBUG(XmppConnectionDelete, "XmppConnection destructor",
               FromString(), ToString());
    last_msg_.release();

}

void XmppConnection::SetConfig(const XmppChannelConfig *config) {
    config_ = config;
}

void XmppConnection::set_session(XmppSession *session) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    session_ = session;
}

const XmppSession *XmppConnection::session() const {
    return session_;
}

XmppSession *XmppConnection::session() {
    return session_;
}

void XmppConnection::WriteReady(const boost::system::error_code &ec) {
   mux_->WriteReady(ec);
}

void XmppConnection::Shutdown() {
    ManagedDelete();
}

bool XmppConnection::ShutdownPending() const {
    return deleter()->IsDeleted();
}

bool XmppConnection::MayDelete() const {
    size_t count = mux_->ReceiverCount();
    return (count == 0);
}

XmppSession *XmppConnection::CreateSession() {
    TcpSession *session = server_->CreateSession();
    XmppSession *xmpp_session = static_cast<XmppSession *>(session);
    xmpp_session->SetConnection(this);
    return xmpp_session;
}

xmsm::XmState XmppConnection::GetStateMcState() const {
    const XmppStateMachine *sm = state_machine();
    assert(sm);
    return sm->StateType();
}

boost::asio::ip::tcp::endpoint XmppConnection::endpoint() const {
    return endpoint_;
}

boost::asio::ip::tcp::endpoint XmppConnection::local_endpoint() const {
    return local_endpoint_;
}

string XmppConnection::FromString() const {
    return from_;
}

std::string XmppConnection::ToString() const {
    return to_;
}

std::string XmppConnection::ToUVEKey() const {
    std::ostringstream out;
    out << FromString() << ":" << endpoint().address().to_string();
    return out.str();
}

void XmppConnection::SetFrom(const string &from) {
    if (from_.size() == 0) {
        from_ = from;
        state_machine_->Initialize();
    }
}

void XmppConnection::SetTo(const string &to) {
    if ((to_.size() == 0) && (to.size() != 0)) {
        to_ = to;
        if (!logUVE()) return;
        XmppPeerInfoData peer_info;
        peer_info.set_name(ToUVEKey());
        peer_info.set_identifier(to_);
        XMPPPeerInfo::Send(peer_info);
    }
}

void XmppConnection::SetAdminDown(bool toggle) {
    // TODO: generate state machine event.
    admin_down_ = toggle;
}

bool XmppConnection::AcceptSession(XmppSession *session) {
    session->SetConnection(this);
    return state_machine_->PassiveOpen(session);
}

bool XmppConnection::Send(const uint8_t *data, size_t size) {
    size_t sent;
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (session_ == NULL) {
        return false;
    }
    XMPP_MESSAGE_TRACE(XmppTxStream, 
           session_->remote_endpoint().address().to_string(),
           session_->remote_endpoint().port(), size,
           string(reinterpret_cast<const char *>(data), size));

    stats_[1].update++;
    return session_->Send(data, size, &sent);
}

void XmppConnection::SendOpen(TcpSession *session) {
    if (!session) return;
    XmppProto::XmppStanza::XmppStreamMessage openstream;
    openstream.strmtype = XmppStanza::XmppStreamMessage::INIT_STREAM_HEADER;
    uint8_t data[256];
    int len = XmppProto::EncodeStream(openstream, to_, from_, data, 
                                      sizeof(data));
    assert(len > 0);
    XMPP_UTDEBUG(XmppOpen, len, from_, to_);
    session->Send(data, len, NULL);
    stats_[1].open++;
}

void XmppConnection::SendOpenConfirm(TcpSession *session) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (!session_) return;
    XmppStanza::XmppStreamMessage openstream;
    openstream.strmtype = XmppStanza::XmppStreamMessage::INIT_STREAM_HEADER_RESP;
    uint8_t data[256];
    int len = XmppProto::EncodeStream(openstream, to_, from_, data, 
                                      sizeof(data));
    assert(len > 0);
    XMPP_UTDEBUG(XmppOpenConfirm, len, from_, to_);
    session_->Send(data, len, NULL);
    stats_[1].open++;
}

void XmppConnection::SendClose(TcpSession *session) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (!session_) return;
    string str("</stream:stream>");
    uint8_t data[64];
    memcpy(data, str.data(), str.size());
    XMPP_UTDEBUG(XmppClose, str.size(), from_, to_);
    session_->Send(data, str.size(), NULL);
    stats_[1].close++;
}

void XmppConnection::LogMsg(std::string msg) {
    log4cplus::Logger logger = log4cplus::Logger::getRoot();
    LOG4CPLUS_DEBUG(logger, msg << ToString() << " " <<
         local_endpoint_.address() << ":" << local_endpoint_.port() << "::" <<
         endpoint_.address() << ":" << endpoint_.port());
}

void XmppConnection::LogKeepAliveSend() {
    static bool init_ = false;
    static bool log_ = false;

    if (!init_) {
        char *str = getenv("XMPP_ASSERT_ON_HOLD_TIMEOUT");
        if (str && strtoul(str, NULL, 0) != 0) log_ = true;
        init_ = true;
    }

    if (log_) LogMsg("SEND KEEPALIVE: ");
}

void XmppConnection::SendKeepAlive() {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (!session_) return;
    XmppStanza::XmppMessage msg(XmppStanza::WHITESPACE_MESSAGE_STANZA);
    uint8_t data[256];
    int len = XmppProto::EncodeStream(msg, data, sizeof(data));
    assert(len > 0);
    session_->Send(data, len, NULL);
    stats_[1].keepalive++;
    LogKeepAliveSend();
}

//
// Get the default keepalive time in seconds
//
const int XmppConnection::GetDefaultkeepAliveTime() {
    static bool init_ = false;
    static int time_ = keepAliveTime;

    if (!init_) {

        // XXX For testing only - Configure through environment variable
        char *time_str = getenv("XMPP_KEEPALIVE_SECONDS");
        if (time_str) {
            time_ = strtoul(time_str, NULL, 0);
        }
        init_ = true;
    }
    return time_;
}

bool XmppConnection::KeepAliveTimerExpired() {

    // TODO: check timestamp of last received packet.
    SendKeepAlive();

    //
    // Start the timer again, by returning true
    //
    return true;
}   

void XmppConnection::KeepaliveTimerErrorHanlder(string error_name,
                                                string error_message) {
    XMPP_WARNING(XmppKeepaliveTimeError, error_name, error_message);
}

void XmppConnection::StartKeepAliveTimer() {
    // TODO use negotiated holdtime.
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (!session_) return;

    if (!keepalive_time_) return;

    keepalive_timer_->Start(keepalive_time_ * 1000,
        boost::bind(&XmppConnection::KeepAliveTimerExpired, this),
        boost::bind(&XmppConnection::KeepaliveTimerErrorHanlder, this, _1, _2));
}

void XmppConnection::StopKeepAliveTimer() {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    keepalive_timer_->Cancel();
}

XmppStateMachine *XmppConnection::state_machine() {
    return state_machine_.get();
}

const XmppStateMachine *XmppConnection::state_machine() const {
    return state_machine_.get();
}

void XmppConnection::IncProtoStats(unsigned int type) {
    switch (type) {
        case XmppStanza::STREAM_HEADER:
            stats_[0].open++;
            break;
        case XmppStanza::WHITESPACE_MESSAGE_STANZA:
            stats_[0].keepalive++;
            break;
        case XmppStanza::IQ_STANZA:
            stats_[0].update++;
            break;
        case XmppStanza::MESSAGE_STANZA:
            stats_[0].update++;
            break;
    }
}

void XmppConnection::ReceiveMsg(XmppSession *session, const string &msg) {
    XmppStanza::XmppMessage *minfo = XmppDecode(msg);

    if (minfo) {
        session->IncStats((unsigned int)minfo->type, msg.size());
        if (minfo->type != XmppStanza::WHITESPACE_MESSAGE_STANZA) {
            XMPP_MESSAGE_TRACE(XmppRxStream, 
                  session->remote_endpoint().address().to_string(),
                  session->remote_endpoint().port(), msg.size(), msg);
        }   
        IncProtoStats((unsigned int)minfo->type);
        state_machine_->OnMessage(session, minfo);
    } else {
        session->IncStats(XmppStanza::INVALID, msg.size());
        XMPP_MESSAGE_TRACE(XmppRxStreamInvalid,
             session->remote_endpoint().address().to_string(),
             session->remote_endpoint().port(), msg.size(), msg);
    }
    return;
}

XmppStanza::XmppMessage *XmppConnection::XmppDecode(const string &msg) {
    auto_ptr<XmppStanza::XmppMessage> minfo(XmppProto::Decode(msg));
    if (minfo.get() == NULL) {
        Clear();
        return NULL;
    }

    if (minfo->type == XmppStanza::IQ_STANZA) {
        const XmppStanza::XmppMessageIq *iq = 
            static_cast<const XmppStanza::XmppMessageIq *>(minfo.get());


        if (iq->action.compare("publish") == 0) {
            last_msg_.reset(minfo.release());
            return NULL;
        }

        if (iq->action.compare("collection") == 0) {
            if (last_msg_.get() != NULL) {
                XmppStanza::XmppMessageIq *last_iq = 
                    static_cast<XmppStanza::XmppMessageIq *>(last_msg_.get());

                if (last_iq->node.compare(iq->as_node) == 0) {
                    XmlBase *impl = last_iq->dom.get();
                    impl->ReadNode("publish");
                    impl->ModifyAttribute("node", iq->node);
                    last_iq->node = impl->ReadAttrib("node");
                    last_iq->is_as_node = iq->is_as_node;
                    //Save the complete ass/dissociate node
                    last_iq->as_node = iq->as_node;
                } else {
                    XMPP_WARNING(XmppIqMessageInvalid);
                    goto error;
                }
            } else {
                XMPP_LOG(XmppIqCollectionError);
                goto error;
            }
            // iq message merged with collection info
            return last_msg_.release();
        }
    }
    return minfo.release();

error:
    last_msg_.reset();
    return NULL;
}

int XmppConnection::ProcessXmppChatMessage(
        const XmppStanza::XmppChatMessage *msg) {
    mux_->ProcessXmppMessage(msg);
    return 0;
}

int XmppConnection::ProcessXmppIqMessage(const XmppStanza::XmppMessage *msg) {
    mux_->ProcessXmppMessage(msg);
    return 0;
}

class XmppServerConnection::DeleteActor : public LifetimeActor {
public:
    DeleteActor(XmppServer *server, XmppServerConnection *parent)
        : LifetimeActor(server->lifetime_manager()),
          server_(server), parent_(parent) {
    }

    virtual bool MayDelete() const {
        return parent_->MayDelete();
    }

    virtual void Shutdown() {
        CHECK_CONCURRENCY("bgp::Config");

        // Move to XmppConnectionSet if it was in the XmppConnectionMap.
        if (server_->RemoveConnection(parent_))
            server_->DeleteConnection(parent_);

        // TODO: Separate xmps::NOT_READY and xmps:TERMINATE (for GR).
        if (parent_->session() || server_->IsPeerCloseGraceful()) {
            server_->NotifyConnectionEvent(parent_->ChannelMux(),
                                           xmps::NOT_READY);
        }

        if (parent_->logUVE()) {
            XmppPeerInfoData peer_info;
            peer_info.set_name(parent_->ToUVEKey());
            peer_info.set_deleted(true);
            XMPPPeerInfo::Send(peer_info);
        }

        XmppSession *session = NULL;
        if (parent_->state_machine()) {
            session = parent_->state_machine()->session();
            parent_->state_machine()->clear_session();
        }
        if (session) {
            server_->DeleteSession(session);
        }
    }

    virtual void Destroy() {
        parent_->Destroy();
    }

private:
    XmppServer *server_;
    XmppServerConnection *parent_;
};

XmppServerConnection::XmppServerConnection(XmppServer *server,
    const XmppChannelConfig *config)
    : XmppConnection(server, config), 
      deleter_(new DeleteActor(server, this)),
      server_delete_ref_(this, server->deleter()) {
    assert(!config->ClientOnly());
    XMPP_INFO(XmppConnectionCreate, "Server", FromString(), ToString());
    conn_endpoint_ =
        server->LocateConnectionEndpoint(endpoint().address().to_v4());
}

XmppServerConnection::~XmppServerConnection() {
    XMPP_INFO(XmppConnectionDelete, "Server", FromString(), ToString());
}

void XmppServerConnection::ManagedDelete() {
    XMPP_UTDEBUG(XmppConnectionDelete, "Managed server connection delete", 
                 FromString(), ToString());
    deleter_->Delete();
}

void XmppServerConnection::RetryDelete() {
    if (!deleter()->IsDeleted())
        return;
    deleter()->RetryDelete();
}

bool XmppServerConnection::IsClient() const {
    return false;
}

LifetimeManager *XmppServerConnection::lifetime_manager() {
    return static_cast<XmppServer *>(server())->lifetime_manager();
}

void XmppServerConnection::Destroy() {
    CHECK_CONCURRENCY("bgp::Config");
    (static_cast<XmppServer *>(server()))->DestroyConnection(this);
};

LifetimeActor *XmppServerConnection::deleter() {
    return deleter_.get();
}

const LifetimeActor *XmppServerConnection::deleter() const {
    return deleter_.get();
}

void XmppServerConnection::set_close_reason(const string &close_reason) {
    conn_endpoint_->set_close_reason(close_reason);

    if (!logUVE())
        return;

    XmppPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_close_reason(close_reason);
    XMPPPeerInfo::Send(peer_info);
}

uint32_t XmppServerConnection::flap_count() const {
    return conn_endpoint_->flap_count();
}

void XmppServerConnection::increment_flap_count() {
    conn_endpoint_->increment_flap_count();

    if (!logUVE())
        return;

    XmppPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    PeerFlapInfo flap_info;
    flap_info.set_flap_count(conn_endpoint_->flap_count());
    flap_info.set_flap_time(conn_endpoint_->last_flap());
    peer_info.set_flap_info(flap_info);
    XMPPPeerInfo::Send(peer_info);
}

const std::string XmppServerConnection::last_flap_at() const {
    return conn_endpoint_->last_flap_at();
}

class XmppClientConnection::DeleteActor : public LifetimeActor {
public:
    DeleteActor(XmppClient *client, XmppClientConnection *parent)
        : LifetimeActor(client->lifetime_manager()),
          client_(client), parent_(parent) {
    }

    virtual bool MayDelete() const {
        return (client_->ConnectionEventCount() == 0 || parent_->MayDelete());
    }

    virtual void Shutdown() {
        if (parent_->session()) {
            (static_cast<XmppClient *>(client_))->
                NotifyConnectionEvent(parent_->ChannelMux(), xmps::NOT_READY);
        }

        XmppSession *session = NULL;
        if (parent_->state_machine()) {
            session = parent_->state_machine()->session();
            parent_->state_machine()->clear_session();
        }

        if (session) {
            client_->DeleteSession(session);
        }
    }

    virtual void Destroy() {
        parent_->Destroy();
    }

private:
    XmppClient *client_;
    XmppClientConnection *parent_;
};

XmppClientConnection::XmppClientConnection(TcpServer *server,
    const XmppChannelConfig *config)
    : XmppConnection(server, config), 
      deleter_(new DeleteActor(static_cast<XmppClient *>(server), this)),
      server_delete_ref_(this, static_cast<XmppClient *>(server)->deleter()) {
    assert(config->ClientOnly());
    XMPP_UTDEBUG(XmppConnectionCreate, "Client", FromString(), ToString());
}

XmppClientConnection::~XmppClientConnection() {
    XMPP_INFO(XmppConnectionDelete, "Client", FromString(), ToString());
}

void XmppClientConnection::ManagedDelete() {
    XMPP_UTDEBUG(XmppConnectionDelete, "Managed Client Delete", 
                 FromString(), ToString());
    deleter_->Delete();
}

void XmppClientConnection::RetryDelete() {
    if (!deleter()->IsDeleted())
        return;
    deleter()->RetryDelete();
}

bool XmppClientConnection::IsClient() const {
    return true;
}

LifetimeManager *XmppClientConnection::lifetime_manager() {
    return static_cast<XmppClient *>(server())->lifetime_manager();
}

LifetimeActor *XmppClientConnection::deleter() {
    return deleter_.get();
}

const LifetimeActor *XmppClientConnection::deleter() const {
    return deleter_.get();
}

void XmppClientConnection::Destroy() {
    CHECK_CONCURRENCY("bgp::Config");
    (static_cast<XmppClient *>(server()))->RemoveConnection(this);
};

void XmppClientConnection::set_close_reason(const string &close_reason) {
    close_reason_ = close_reason;
    if (!logUVE())
        return;

    XmppPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_close_reason(close_reason_);
    XMPPPeerInfo::Send(peer_info);
}

uint32_t XmppClientConnection::flap_count() const {
    return flap_count_;
}

void XmppClientConnection::increment_flap_count() {
    flap_count_++;
    last_flap_ = UTCTimestampUsec();

    if (!logUVE())
        return;

    XmppPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    PeerFlapInfo flap_info;
    flap_info.set_flap_count(flap_count_);
    flap_info.set_flap_time(last_flap_);
    peer_info.set_flap_info(flap_info);
    XMPPPeerInfo::Send(peer_info);
}

const std::string XmppClientConnection::last_flap_at() const {
    return last_flap_ ? integerToString(UTCUsecToPTime(last_flap_)) : "";
}

XmppConnectionEndpoint::XmppConnectionEndpoint(Ip4Address address)
    : address_(address), flap_count_(0), last_flap_(0) {
}

void XmppConnectionEndpoint::set_close_reason(const string &close_reason) {
    close_reason_ = close_reason;
}

uint32_t XmppConnectionEndpoint::flap_count() const {
    return flap_count_;
}

void XmppConnectionEndpoint::increment_flap_count() {
    flap_count_++;
    last_flap_ = UTCTimestampUsec();
}

uint64_t XmppConnectionEndpoint::last_flap() const {
    return last_flap_;
}

const std::string XmppConnectionEndpoint::last_flap_at() const {
    return last_flap_ ? integerToString(UTCUsecToPTime(last_flap_)) : "";
}
